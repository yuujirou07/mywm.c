#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "my_cui_lib.h"

static int convert_eight_bit_rgb_color(enum eight_bit_rgb eight_bit_rgb);

static char   big[4096];
static size_t pos = 0;

// int一個を10進表記した最大長 ("-2147483648" の11文字)。
#define INT_DIGITS 11

// ===== 内部バッファ操作 =====
// 各シーケンスはsnprintf()を通さず、リテラルのmemcpyと手書きの整数変換で組み立てる。
// 追記前にreserve()で残量を確保し、足りなければ先に書き出すのでposがbigを越えることはない。

// buff_drain(): 溜まっているバイト列をstdoutへ渡し、バッファを空にする。
// 引数: なし。
// 返り値: なし。
static void buff_drain(void){
    if (pos) {
        fwrite(big, 1, pos, stdout);
        pos = 0;
    }
}

// reserve(): これから書くneedバイト分の空きを確保する。
// 引数: need=追記予定のバイト数(sizeof(big)以下であること)。
// 返り値: なし。
static inline void reserve(size_t need){
    if (sizeof(big) - pos < need) {
        buff_drain();
    }
}

// append_lit(): 長さが分かっている固定バイト列を追記する。
// 引数: lit=追記するバイト列、len=その長さ。
// 返り値: なし。
static inline void append_lit(const char *lit, size_t len){
    reserve(len);
    memcpy(big + pos, lit, len);
    pos += len;
}

// APPEND_LIT(): 文字列リテラル専用のappend_lit()。長さをsizeofでコンパイル時に決める。
#define APPEND_LIT(s) append_lit((s), sizeof(s) - 1)

// append_str(): NUL終端文字列を追記する。バッファに収まらない長さは直接書き出す。
// 引数: str=追記するNUL終端文字列。
// 返り値: なし。
static void append_str(const char *str){
    size_t len = strlen(str);
    if (len > sizeof(big)) {
        buff_drain();
        fwrite(str, 1, len, stdout);
        return;
    }
    reserve(len);
    memcpy(big + pos, str, len);
    pos += len;
}

// put_int(): 符号付き整数を10進表記でpへ書き、書き終わった次の位置を返す。
// 引数: p=書き込み先、value=変換する値。
// 返り値: 書き込み終端の次を指すポインタ。
static inline char *put_int(char *p, int value){
    char     tmp[INT_DIGITS];
    int      n = 0;
    unsigned u;

    if (value < 0) {
        *p++ = '-';
        u = (unsigned)-(long)value;   // INT_MINでも溢れないようlong経由で符号反転する
    } else {
        u = (unsigned)value;
    }
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (n) {
        *p++ = tmp[--n];
    }
    return p;
}

// csi_1num(): "ESC [ <num> <final>" 形式のCSIを追記する。
// 引数: value=数値パラメータ、final=シーケンス終端の1文字。
// 返り値: なし。
static inline void csi_1num(int value, char final){
    char *p;
    reserve(2 + INT_DIGITS + 1);
    p = big + pos;
    *p++ = '\x1b';
    *p++ = '[';
    p = put_int(p, value);
    *p++ = final;
    pos = (size_t)(p - big);
}

// csi_cup(): カーソル移動 "ESC [ <row> ; <col> H" を追記する。
// 引数: row=行、col=列。
// 返り値: なし。
static inline void csi_cup(int row, int col){
    char *p;
    reserve(2 + INT_DIGITS + 1 + INT_DIGITS + 1);
    p = big + pos;
    *p++ = '\x1b';
    *p++ = '[';
    p = put_int(p, row);
    *p++ = ';';
    p = put_int(p, col);
    *p++ = 'H';
    pos = (size_t)(p - big);
}

// sgr_true_color(): true colorのSGR "ESC [ <base> ;2; <r> ; <g> ; <b> m" を追記する。
// 引数: base=前景38/背景48、rgb=設定するRGB値。
// 返り値: なし。
static inline void sgr_true_color(int base, struct rgb rgb){
    char *p;
    reserve(2 + INT_DIGITS + 2 + (INT_DIGITS + 1) * 3 + 1);
    p = big + pos;
    *p++ = '\x1b';
    *p++ = '[';
    p = put_int(p, base);
    *p++ = ';';
    *p++ = '2';
    *p++ = ';';
    p = put_int(p, rgb.r);
    *p++ = ';';
    p = put_int(p, rgb.g);
    *p++ = ';';
    p = put_int(p, rgb.b);
    *p++ = 'm';
    pos = (size_t)(p - big);
}

// term_init(): ANSIエスケープシーケンスを溜める内部バッファを空にする。
// 引数: なし。
// 返り値: 初期化成功を表す0。
int term_init(){
    pos = 0;
    return 0;
}

// ===== カーソル制御シーケンス (CSI) =====

// move_cursor(): カーソルを指定座標へ移動するシーケンスを追加する。
// 引数: x=列、y=行。
// 返り値: なし。
void move_cursor(int x, int y){
    csi_cup(y, x);
}

// reset_cursor(): カーソルを画面左上へ移動するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void reset_cursor(){
    APPEND_LIT("\x1b[H");
}

// cursor_up(): カーソルを上方向へ移動するシーケンスを追加する。
// 引数: count=移動行数。
// 返り値: なし。
void cursor_up(int count){
    csi_1num(count, 'A');
}

// cursor_down(): カーソルを下方向へ移動するシーケンスを追加する。
// 引数: count=移動行数。
// 返り値: なし。
void cursor_down(int count){
    csi_1num(count, 'B');
}

// cursor_move_right(): カーソルを右方向へ移動するシーケンスを追加する。
// 引数: count=移動列数。
// 返り値: なし。
void cursor_move_right(int count){
    csi_1num(count, 'C');
}

// cursor_move_left(): カーソルを左方向へ移動するシーケンスを追加する。
// 引数: count=移動列数。
// 返り値: なし。
void cursor_move_left(int count){
    csi_1num(count, 'D');
}

// cursor_down_line_start(): カーソルを下の行頭方向へ移動するシーケンスを追加する。
// 引数: count=移動行数。
// 返り値: なし。
void cursor_down_line_start(int count){
    csi_1num(count, 'E');
}

// cursor_up_line_start(): カーソルを上の行頭方向へ移動するシーケンスを追加する。
// 引数: count=移動行数。
// 返り値: なし。
void cursor_up_line_start(int count){
    csi_1num(count, 'F');
}

// cursor_line_move(): カーソルを現在行の指定列へ移動するシーケンスを追加する。
// 引数: count=移動先の列番号。
// 返り値: なし。
void cursor_line_move(int count){
    csi_1num(count, 'G');
}

// rq_cursor_pos(): 端末へ現在カーソル位置の報告を要求するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void rq_cursor_pos(){
    APPEND_LIT("\x1b[6n");
}

// save_cursor_pos(): 現在のカーソル位置を保存するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void save_cursor_pos(){
    APPEND_LIT("\x1b[s");
}

// remove_cursor_pos(): 保存済みカーソル位置へ戻すシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void remove_cursor_pos(){
    APPEND_LIT("\x1b[u");
}

// ===== 画面・行の消去機能 =====

// erase_cursor_to_end_of_screen(): カーソル位置から画面末尾まで消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_cursor_to_end_of_screen(){
    APPEND_LIT("\x1b[J");
}

// erase_cursor_to_start_of_screen(): カーソル位置から画面先頭まで消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_cursor_to_start_of_screen(){
    APPEND_LIT("\x1b[1J");
}

// erase_screen(): 画面全体を消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_screen(){
    APPEND_LIT("\x1b[2J");
}

// erase_scroll_buff(): スクロールバックを消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_scroll_buff(){
    APPEND_LIT("\x1b[3J");
}

// erase_cursor_pos_to_line_end(): カーソル位置から行末まで消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_cursor_pos_to_line_end(){
    APPEND_LIT("\x1b[K");
}

// erase_cursor_pos_to_line_start(): カーソル位置から行頭まで消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_cursor_pos_to_line_start(){
    APPEND_LIT("\x1b[1K");
}

// erase_cursor_line(): 現在行全体を消去するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void erase_cursor_line(){
    APPEND_LIT("\x1b[2K");
}

// ===== 文字の書式設定と色 (SGR: Select Graphic Rendition) =====

// change_str_rgb_foreground_color_start(): ANSI 8色の前景色を開始するシーケンスを追加する。
// 引数: eight_bit_rgb=設定する前景色。
// 返り値: なし。
void change_str_rgb_foreground_color_start(enum eight_bit_rgb eight_bit_rgb){
    int rgb_color = convert_eight_bit_rgb_color(eight_bit_rgb);
    csi_1num(rgb_color, 'm');
}

// change_str_rgb_foreground_color_end(): 前景色を含む文字属性をリセットするシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void change_str_rgb_foreground_color_end(){
    APPEND_LIT("\x1b[0m");
}

// change_str_rgb_background_color_start(): ANSI 8色の背景色を開始するシーケンスを追加する。
// 引数: eight_bit_rgb=設定する背景色。
// 返り値: なし。
void change_str_rgb_background_color_start(enum eight_bit_rgb eight_bit_rgb){
    int rgb_color = convert_eight_bit_rgb_color(eight_bit_rgb);
    csi_1num(rgb_color + 10, 'm');
}

// change_str_rgb_background_color_end(): 背景色を既定値へ戻すシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void change_str_rgb_background_color_end(){
    APPEND_LIT("\x1b[49m");
}

// change_str_rgb_true_foreground_color_start(): true colorの前景色を開始するシーケンスを追加する。
// 引数: rgb=設定するRGB値。
// 返り値: なし。
void change_str_rgb_true_foreground_color_start(struct rgb rgb){
    sgr_true_color(38, rgb);
}

// change_str_rgb_true_foreground_color_end(): 前景色を既定値へ戻すシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void change_str_rgb_true_foreground_color_end(){
    APPEND_LIT("\x1b[39m");
}

// change_str_rgb_true_background_color_start(): true colorの背景色を開始するシーケンスを追加する。
// 引数: rgb=設定するRGB値。
// 返り値: なし。
void change_str_rgb_true_background_color_start(struct rgb rgb){
    sgr_true_color(48, rgb);
}

// change_str_rgb_true_background_color_end(): 背景色を既定値へ戻すシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void change_str_rgb_true_background_color_end(){
    APPEND_LIT("\x1b[49m");
}

// ===== 文字出力 =====

// myprint(): 内部バッファへ通常文字列を追加する。
// 引数: str=追加するNUL終端文字列。
// 返り値: なし。
void myprint(char *str){
    append_str(str);
}

// myprint_at(): 指定位置へ移動するシーケンスと文字列を内部バッファへ追加する。
// 引数: x=列、y=行、str=指定位置に出すNUL終端文字列。
// 返り値: なし。
void myprint_at(int x, int y, const char *str){
    csi_cup(y, x);
    append_str(str);
}

// ===== ターミナルモードの変更 =====

// hidden_cursor(): カーソルを非表示にするシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void hidden_cursor(){
    APPEND_LIT("\x1b[?25l");
}

// show_cursor(): カーソルを表示するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void show_cursor(){
    APPEND_LIT("\x1b[?25h");
}

// change_buff_screen(): 代替画面バッファへ切り替えるシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void change_buff_screen(){
    APPEND_LIT("\x1b[?47h");
}

// back_forward_screen(): 通常画面バッファへ戻すシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void back_forward_screen(){
    APPEND_LIT("\x1b[?47l");
}

// report_focus_event(): フォーカスイベント報告を有効化するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void report_focus_event(){
    APPEND_LIT("\x1b[?1004h");
}

// not_report_focus_event(): フォーカスイベント報告を無効化するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void not_report_focus_event(){
    APPEND_LIT("\x1b[?1004l");
}

// on_bracket_paste_mode(): bracketed paste modeを有効化するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void on_bracket_paste_mode(){
    APPEND_LIT("\x1b[?2004h");
}

// off_bracket_paste_mode(): bracketed paste modeを無効化するシーケンスを追加する。
// 引数: なし。
// 返り値: なし。
void off_bracket_paste_mode(){
    APPEND_LIT("\x1b[?2004l");
}

// ===== オペレーティングシステムコマンド (OSC) =====

// change_window_name(): 端末ウィンドウ名を変更するOSCシーケンスを追加する。
// 引数: w_name=設定するウィンドウ名。
// 返り値: なし。
void change_window_name(char *w_name){
    APPEND_LIT("\x1b]0;");
    append_str(w_name);
    APPEND_LIT("ST");
}

// change_win_tab_name(): 端末タブ名を変更するOSCシーケンスを追加する。
// 引数: w_t_name=設定するタブ名。
// 返り値: なし。
void change_win_tab_name(char *w_t_name){
    APPEND_LIT("\x1b]2;");
    append_str(w_t_name);
    APPEND_LIT("ST");
}

// ===== エコーモード (POSIX termios) =====

// set_echo_mode(): termiosのECHOフラグを切り替え、端末入力のエコー表示を制御する。
// 引数: enable=0ならエコー無効、0以外ならエコー有効。
// 返り値: なし。
void set_echo_mode(int enable){
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    if (enable) {
        t.c_lflag |= ECHO;
    } else {
        t.c_lflag &= ~ECHO;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

// ===== フラッシュ =====

// screen_push(): 内部バッファに溜めた端末制御シーケンスをstdoutへまとめて出力する。
// 引数: なし。
// 返り値: なし。
void screen_push(){
    buff_drain();
    fflush(stdout);
}

// ===== 内部ヘルパー =====

// convert_eight_bit_rgb_color(): 独自enumの色名をANSI 8色のSGR番号へ変換する。
// 引数: eight_bit_rgb=red/greenなどの独自色enum。
// 返り値: ANSI SGRの前景色番号(30〜37)。
static int convert_eight_bit_rgb_color(enum eight_bit_rgb eight_bit_rgb){
    int color = 30;
    switch(eight_bit_rgb){
        case red:     color += MY_RED;     break;
        case green:   color += MY_GREEN;   break;
        case blue:    color += MY_BLUE;    break;
        case black:   color += MY_BLACK;   break;
        case yellow:  color += MY_YELLOW;  break;
        case magenta: color += MY_MAGENTA; break;
        case cyan:    color += MY_CYAN;    break;
        case white:   color += MY_WHITE;   break;
    }
    return color;
}

// get_terminal_size(): ioctlで現在の端末サイズを取得し、失敗時は80x24を返す。
// 引数: width=列数の書き込み先、height=行数の書き込み先。
// 返り値: なし。
void get_terminal_size(int *width, int *height){
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width  = ws.ws_col;
        *height = ws.ws_row;
    } else {
        *width  = 80;
        *height = 24;
    }
}
