// codepoint_comb.c
// ------------------------------------------------------------------
// nvim などの TUI が枠線に使う罫線素片(U+2500〜U+257F)とブロック素片
// (U+2580〜U+259F)を、フォントグリフを使わずコードポイントから手続き的に
// 直接ピクセル描画するためのモジュール。
//
// 通常の端末はこれらの文字を専用フォントグリフとして持つが、本プロジェクトは
// ASCII 128 個分のグリフatlasしか読み込んでいない。罫線は単純な直線・矩形なので
// 線分を計算で描けば、フォントを増やさずに最小改変で枠線を表示できる。
// ------------------------------------------------------------------
#include "codepoint_comb.h"
#include <math.h>

// ---- UTF-8 デコード -------------------------------------------------
int utf8_decode(const unsigned char *s, int max_len, int *out_cp)
{
    if (max_len <= 0) { *out_cp = 0; return 0; }
    unsigned char c = s[0];

    int need;            // 後続バイト数
    int cp;
    if (c < 0x80)        { *out_cp = c; return 1; }      // ASCII
    else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
    else { *out_cp = c; return 1; }                      // 不正な先頭バイト

    if (need >= max_len) { *out_cp = c; return 1; }      // バッファ末尾で途中切れ
    for (int k = 1; k <= need; k++) {
        if ((s[k] & 0xC0) != 0x80) { *out_cp = c; return 1; } // 継続バイトでない
        cp = (cp << 6) | (s[k] & 0x3F);
    }
    *out_cp = cp;
    return need + 1;
}

// ---- 描画対象かどうかの判定 ----------------------------------------
bool is_box_codepoint(int cp)
{
    return cp >= 0x2500 && cp <= 0x259F;
}

// ---- 線の太さ計算 ---------------------------------------------------
// weight: 1=細線(light) 2=太線(heavy) 3=二重線(double, ここでは細線扱い)
// dim   : 線が広がる方向のセル寸法(水平線なら cell_h、垂直線なら cell_w)
static int stroke_thick(int weight, int dim)
{
    if (weight <= 0) return 0;
    if (weight == 2) { int t = dim / 4; return t < 2 ? 2 : t; }
    int t = dim / 8; return t < 1 ? 1 : t;               // light / double 1 本分
}

// その方向の線が中心付近で占める全幅(二重線は隙間込み)。交点を埋めるのに使う。
static int stroke_span(int weight, int dim)
{
    if (weight == 0) return 0;
    if (weight == 3) {
        int t = stroke_thick(1, dim);
        int g = dim / 6; if (g < 2) g = 2;
        return g + t;
    }
    return stroke_thick(weight, dim);
}

// ---- 低レベル描画 ---------------------------------------------------
static void fill_rect(uint8_t *buf, int sw, int sh, int bx, int by,
                      int cw, int ch, int x0, int y0, int x1, int y1,
                      Color c, int a)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cw) x1 = cw;
    if (y1 > ch) y1 = ch;
    for (int y = y0; y < y1; y++) {
        int sy = by + y; if (sy < 0 || sy >= sh) continue;
        for (int x = x0; x < x1; x++) {
            int sx = bx + x; if (sx < 0 || sx >= sw) continue;
            int idx = (sy * sw + sx) * 4;
            buf[idx + 0] = (uint8_t)(buf[idx + 0] + (c.b - buf[idx + 0]) * a / 255);
            buf[idx + 1] = (uint8_t)(buf[idx + 1] + (c.g - buf[idx + 1]) * a / 255);
            buf[idx + 2] = (uint8_t)(buf[idx + 2] + (c.r - buf[idx + 2]) * a / 255);
            buf[idx + 3] = 255;
        }
    }
}

static void put_px(uint8_t *buf, int sw, int sh, int bx, int by,
                   int cw, int ch, int x, int y, Color c)
{
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;
    int sx = bx + x, sy = by + y;
    if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) return;
    int idx = (sy * sw + sx) * 4;
    buf[idx + 0] = c.b; buf[idx + 1] = c.g; buf[idx + 2] = c.r; buf[idx + 3] = 255;
}

// 中心から指定辺へ伸びる水平方向の半線(side<0:左, side>0:右)
static void hstroke(uint8_t *buf, int sw, int sh, int bx, int by,
                    int cw, int ch, int side, int weight, int vjoin, Color c)
{
    if (!weight) return;
    int cx = cw / 2, cy = ch / 2;
    int x0 = side < 0 ? 0          : cx - vjoin;
    int x1 = side < 0 ? cx + vjoin : cw;
    if (weight == 3) {
        int t = stroke_thick(1, ch);
        int g = ch / 6; if (g < 2) g = 2;
        fill_rect(buf, sw, sh, bx, by, cw, ch, x0, cy - g / 2 - t / 2, x1, cy - g / 2 - t / 2 + t, c, 255);
        fill_rect(buf, sw, sh, bx, by, cw, ch, x0, cy + g / 2 - t / 2, x1, cy + g / 2 - t / 2 + t, c, 255);
    } else {
        int t = stroke_thick(weight, ch);
        fill_rect(buf, sw, sh, bx, by, cw, ch, x0, cy - t / 2, x1, cy - t / 2 + t, c, 255);
    }
}

// 中心から指定辺へ伸びる垂直方向の半線(side<0:上, side>0:下)
static void vstroke(uint8_t *buf, int sw, int sh, int bx, int by,
                    int cw, int ch, int side, int weight, int hjoin, Color c)
{
    if (!weight) return;
    int cx = cw / 2, cy = ch / 2;
    int y0 = side < 0 ? 0          : cy - hjoin;
    int y1 = side < 0 ? cy + hjoin : ch;
    if (weight == 3) {
        int t = stroke_thick(1, cw);
        int g = cw / 6; if (g < 2) g = 2;
        fill_rect(buf, sw, sh, bx, by, cw, ch, cx - g / 2 - t / 2, y0, cx - g / 2 - t / 2 + t, y1, c, 255);
        fill_rect(buf, sw, sh, bx, by, cw, ch, cx + g / 2 - t / 2, y0, cx + g / 2 - t / 2 + t, y1, c, 255);
    } else {
        int t = stroke_thick(weight, cw);
        fill_rect(buf, sw, sh, bx, by, cw, ch, cx - t / 2, y0, cx - t / 2 + t, y1, c, 255);
    }
}

// 角丸(U+256D〜U+2570)。vsign:+1下/-1上, hsign:+1右/-1左 へ伸びる細線を弧で繋ぐ。
static void draw_arc(uint8_t *buf, int sw, int sh, int bx, int by,
                     int cw, int ch, int vsign, int hsign, Color c)
{
    int cx = cw / 2, cy = ch / 2;
    int r = (cx < cy ? cx : cy); if (r < 2) r = 2;
    int t = stroke_thick(1, (cw < ch ? cw : ch));
    int ccx = cx + hsign * r, ccy = cy + vsign * r;

    int xa = cx < ccx ? cx : ccx, xb = cx < ccx ? ccx : cx;
    int ya = cy < ccy ? cy : ccy, yb = cy < ccy ? ccy : cy;
    for (int y = ya; y <= yb; y++) {
        for (int x = xa; x <= xb; x++) {
            double d = sqrt((double)((x - ccx) * (x - ccx) + (y - ccy) * (y - ccy)));
            if (fabs(d - r) <= t / 2.0 + 0.5) put_px(buf, sw, sh, bx, by, cw, ch, x, y, c);
        }
    }
    // 弧の端から各辺へ伸びる直線部
    int vx0 = cx - t / 2;
    fill_rect(buf, sw, sh, bx, by, cw, ch, vx0, vsign > 0 ? ccy : 0,
              vx0 + (t < 1 ? 1 : t), vsign > 0 ? ch : ccy, c, 255);
    int hy0 = cy - t / 2;
    fill_rect(buf, sw, sh, bx, by, cw, ch, hsign > 0 ? ccx : 0, hy0,
              hsign > 0 ? cw : ccx, hy0 + (t < 1 ? 1 : t), c, 255);
}

// 対角線(U+2571 '/'、U+2572 '\'、U+2573 '×')
static void draw_diag(uint8_t *buf, int sw, int sh, int bx, int by,
                      int cw, int ch, int dir, Color c)
{
    int t = stroke_thick(1, (cw < ch ? cw : ch)); if (t < 1) t = 1;
    for (int y = 0; y < ch; y++) {
        if (dir == 2 || dir == 3) {                 // '\'
            int x = y * cw / ch;
            fill_rect(buf, sw, sh, bx, by, cw, ch, x - t / 2, y, x - t / 2 + t, y + 1, c, 255);
        }
        if (dir == 1 || dir == 3) {                 // '/'
            int x = (ch - 1 - y) * cw / ch;
            fill_rect(buf, sw, sh, bx, by, cw, ch, x - t / 2, y, x - t / 2 + t, y + 1, c, 255);
        }
    }
}

// ---- 罫線素片(U+2500〜U+256C, U+2574〜U+257F)の方向・太さテーブル ----
// 各エントリは u,d,l,r の4方向の太さをニブルにパックしたもの。
// weight: 0=なし 1=細 2=太 3=二重
#define B(u, d, l, r) ((uint16_t)(((u) << 12) | ((d) << 8) | ((l) << 4) | (r)))
static const uint16_t box_tbl[0x80] = {
    /*2500*/ B(0,0,1,1), B(0,0,2,2), B(1,1,0,0), B(2,2,0,0),
    /*2504*/ B(0,0,1,1), B(0,0,2,2), B(1,1,0,0), B(2,2,0,0),
    /*2508*/ B(0,0,1,1), B(0,0,2,2), B(1,1,0,0), B(2,2,0,0),
    /*250C*/ B(0,1,0,1), B(0,1,0,2), B(0,2,0,1), B(0,2,0,2),
    /*2510*/ B(0,1,1,0), B(0,1,2,0), B(0,2,1,0), B(0,2,2,0),
    /*2514*/ B(1,0,0,1), B(1,0,0,2), B(2,0,0,1), B(2,0,0,2),
    /*2518*/ B(1,0,1,0), B(1,0,2,0), B(2,0,1,0), B(2,0,2,0),
    /*251C*/ B(1,1,0,1), B(1,1,0,2), B(2,1,0,1), B(1,2,0,1),
    /*2520*/ B(2,2,0,1), B(2,1,0,2), B(1,2,0,2), B(2,2,0,2),
    /*2524*/ B(1,1,1,0), B(1,1,2,0), B(2,1,1,0), B(1,2,1,0),
    /*2528*/ B(2,2,1,0), B(2,1,2,0), B(1,2,2,0), B(2,2,2,0),
    /*252C*/ B(0,1,1,1), B(0,1,2,1), B(0,1,1,2), B(0,1,2,2),
    /*2530*/ B(0,2,1,1), B(0,2,2,1), B(0,2,1,2), B(0,2,2,2),
    /*2534*/ B(1,0,1,1), B(1,0,2,1), B(1,0,1,2), B(1,0,2,2),
    /*2538*/ B(2,0,1,1), B(2,0,2,1), B(2,0,1,2), B(2,0,2,2),
    /*253C*/ B(1,1,1,1), B(1,1,2,1), B(1,1,1,2), B(1,1,2,2),
    /*2540*/ B(2,1,1,1), B(1,2,1,1), B(2,2,1,1), B(2,1,2,1),
    /*2544*/ B(2,1,1,2), B(1,2,2,1), B(1,2,1,2), B(2,1,2,2),
    /*2548*/ B(1,2,2,2), B(2,2,2,1), B(2,2,1,2), B(2,2,2,2),
    /*254C*/ B(0,0,1,1), B(0,0,2,2), B(1,1,0,0), B(2,2,0,0),
    /*2550*/ B(0,0,3,3), B(3,3,0,0), B(0,1,0,3), B(0,3,0,1),
    /*2554*/ B(0,3,0,3), B(0,1,3,0), B(0,3,1,0), B(0,3,3,0),
    /*2558*/ B(1,0,0,3), B(3,0,0,1), B(3,0,0,3), B(1,0,3,0),
    /*255C*/ B(3,0,1,0), B(3,0,3,0), B(1,1,0,3), B(3,3,0,1),
    /*2560*/ B(3,3,0,3), B(1,1,3,0), B(3,3,1,0), B(3,3,3,0),
    /*2564*/ B(0,1,3,3), B(0,3,1,1), B(0,3,3,3), B(1,0,3,3),
    /*2568*/ B(3,0,1,1), B(3,0,3,3), B(1,1,3,3), B(3,3,1,1),
    /*256C*/ B(3,3,3,3), 0, 0, 0,          /* 256D-2570 は弧で別処理 */
    /*2570*/ 0, 0, 0, 0,                   /* 2571-2573 は対角線で別処理 */
    /*2574*/ B(0,0,1,0), B(1,0,0,0), B(0,0,0,1), B(0,1,0,0),
    /*2578*/ B(0,0,2,0), B(2,0,0,0), B(0,0,0,2), B(0,2,0,0),
    /*257C*/ B(0,0,1,2), B(1,2,0,0), B(0,0,2,1), B(2,1,0,0),
};
#undef B

// ---- ブロック素片(U+2580〜U+259F) -----------------------------------
static bool draw_block(uint8_t *buf, int sw, int sh, int bx, int by,
                       int cw, int ch, int cp, Color c)
{
    if (cp == 0x2580) {                       // ▀ 上半分
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw, ch / 2, c, 255);
    } else if (cp >= 0x2581 && cp <= 0x2588) {// ▁..█ 下からの 1/8..8/8
        int n = cp - 0x2580;                  // 1..8
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, ch - ch * n / 8, cw, ch, c, 255);
    } else if (cp >= 0x2589 && cp <= 0x258F) {// ▉..▏ 左からの 7/8..1/8
        int n = 8 - (cp - 0x2588);            // 7..1
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw * n / 8, ch, c, 255);
    } else if (cp == 0x2590) {                // ▐ 右半分
        fill_rect(buf, sw, sh, bx, by, cw, ch, cw / 2, 0, cw, ch, c, 255);
    } else if (cp == 0x2591) {                // ░ 薄い網掛け
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw, ch, c, 64);
    } else if (cp == 0x2592) {                // ▒ 中間の網掛け
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw, ch, c, 128);
    } else if (cp == 0x2593) {                // ▓ 濃い網掛け
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw, ch, c, 192);
    } else if (cp == 0x2594) {                // ▔ 上 1/8
        fill_rect(buf, sw, sh, bx, by, cw, ch, 0, 0, cw, ch / 8, c, 255);
    } else if (cp == 0x2595) {                // ▕ 右 1/8
        fill_rect(buf, sw, sh, bx, by, cw, ch, cw - cw / 8, 0, cw, ch, c, 255);
    } else if (cp >= 0x2596 && cp <= 0x259F) {// 四分割ブロック
        // bit: 1=左上 2=右上 4=左下 8=右下
        static const unsigned char q[10] = {
            0x4, 0x8, 0x1, 0x1|0x4|0x8, 0x1|0x8,
            0x1|0x2|0x4, 0x1|0x2|0x8, 0x2, 0x2|0x4, 0x2|0x4|0x8,
        };
        unsigned char m = q[cp - 0x2596];
        int hw = cw / 2, hh = ch / 2;
        if (m & 0x1) fill_rect(buf, sw, sh, bx, by, cw, ch, 0,  0,  hw, hh, c, 255);
        if (m & 0x2) fill_rect(buf, sw, sh, bx, by, cw, ch, hw, 0,  cw, hh, c, 255);
        if (m & 0x4) fill_rect(buf, sw, sh, bx, by, cw, ch, 0,  hh, hw, ch, c, 255);
        if (m & 0x8) fill_rect(buf, sw, sh, bx, by, cw, ch, hw, hh, cw, ch, c, 255);
    } else {
        return false;
    }
    return true;
}

// ---- 公開関数 -------------------------------------------------------
bool draw_box_codepoint(uint8_t *buf, int sw, int sh,
                        int base_x, int base_y, int cell_w, int cell_h,
                        int cp, Color fg)
{
    if (!is_box_codepoint(cp) || cell_w <= 0 || cell_h <= 0) return false;

    if (cp >= 0x2580) return draw_block(buf, sw, sh, base_x, base_y, cell_w, cell_h, cp, fg);

    int idx = cp - 0x2500;

    // 角丸 U+256D ╭ / U+256E ╮ / U+256F ╯ / U+2570 ╰
    if (cp >= 0x256D && cp <= 0x2570) {
        int vsign = (cp == 0x256D || cp == 0x256E) ? +1 : -1;   // 下 or 上
        int hsign = (cp == 0x256D || cp == 0x2570) ? +1 : -1;   // 右 or 左
        draw_arc(buf, sw, sh, base_x, base_y, cell_w, cell_h, vsign, hsign, fg);
        return true;
    }
    // 対角線 U+2571 ╱ / U+2572 ╲ / U+2573 ╳
    if (cp >= 0x2571 && cp <= 0x2573) {
        draw_diag(buf, sw, sh, base_x, base_y, cell_w, cell_h, cp - 0x2570, fg);
        return true;
    }

    uint16_t v = box_tbl[idx];
    if (v == 0) return true;                 // 未割り当ては空白扱い(描画済みとする)

    int u = (v >> 12) & 0xF, d = (v >> 8) & 0xF;
    int l = (v >> 4) & 0xF, r = v & 0xF;

    // 交点を隙間なく埋めるため、各半線を中心から相手方向の太さの半分だけ延長する
    int vspan = stroke_span(u, cell_w); { int s = stroke_span(d, cell_w); if (s > vspan) vspan = s; }
    int hspan = stroke_span(l, cell_h); { int s = stroke_span(r, cell_h); if (s > hspan) hspan = s; }
    int vjoin = vspan / 2, hjoin = hspan / 2;

    hstroke(buf, sw, sh, base_x, base_y, cell_w, cell_h, -1, l, vjoin, fg);
    hstroke(buf, sw, sh, base_x, base_y, cell_w, cell_h, +1, r, vjoin, fg);
    vstroke(buf, sw, sh, base_x, base_y, cell_w, cell_h, -1, u, hjoin, fg);
    vstroke(buf, sw, sh, base_x, base_y, cell_w, cell_h, +1, d, hjoin, fg);
    return true;
}
