#ifndef CODEPOINT_COMB_H
#define CODEPOINT_COMB_H

#include <stdbool.h>
#include <stdint.h>
#include "pty_make.h"

// UTF-8 のバイト列を 1 つの Unicode コードポイントへデコードする。
// s         : デコード対象の先頭バイト
// max_len   : s から読み出してよい残りバイト数（バッファ末尾を越えないため）
// out_cp    : デコード結果のコードポイント格納先
// 戻り値    : 消費したバイト数（不正・途中切れなら 1 を返し out_cp に先頭バイトを格納）
int utf8_decode(const unsigned char *s, int max_len, int *out_cp);

// 与えられたコードポイントが「罫線・ブロック素片」で、手続き的に描画できるかを返す。
bool is_box_codepoint(int cp);

// 罫線・ブロック素片をステージングバッファへ直接描画する。
// is_box_codepoint(cp) が true のコードポイントのみ処理し、描画したら true を返す。
// buf は BGRA32 のスクリーン全体バッファ、(base_x,base_y) はセル左上のピクセル座標。
bool draw_box_codepoint(uint8_t *buf, int sw, int sh,
                        int base_x, int base_y, int cell_w, int cell_h,
                        int cp, Color fg);

#endif
