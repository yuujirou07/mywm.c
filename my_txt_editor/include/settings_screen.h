#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <ncurses.h>
#include <wchar.h>
#include "editor_types.h"

struct editor_state;

// 設定画面のキーとタイトルの色ペア。1〜3は本文・選択・エラー、4以降は構文色が
// 使うため、それと重ならない15を割り当てる。
#define SETTINGS_ACCENT_COLOR_PAIR 15
// "[q]"のように囲み括弧を含めたキー表示の桁数。
#define SETTINGS_ITEM_KEY_WIDTH 3
// 設定項目の1列あたりの最小幅。これ未満になるくらいなら列を増やさない。
#define SETTINGS_ITEM_COLUMN_MIN_WIDTH 24
// 設定項目の1列あたりの最大幅。枠が広くてもキーが右端まで離れないよう、
// 余った幅は左右の余白へ回して中央に寄せる。
#define SETTINGS_ITEM_COLUMN_MAX_WIDTH 32
// 設定画面の枠を画面いっぱいまで広げないための余白。画面サイズをこの値で
// 割った分を上下左右に残し、残りを枠の上限として使う。
#define SETTINGS_SCREEN_MARGIN_DIV 5
// 設定画面の枠の最小サイズ。項目が少なくてもこれ以上は確保し、見た目の
// 大きさが項目数で極端に変わらないようにする。
#define SETTINGS_SCREEN_MIN_W 44
#define SETTINGS_SCREEN_MIN_H 10
#define SETTINGS_INPUT_MAX 64 // 設定値入力欄の要素数。終端L'\0'を含む。

// 設定画面で受け付ける値の型。
typedef enum{
    VALUE_TYPE_BOOL, // trueまたはfalse。
    VALUE_TYPE_INT, // 整数。
    VALUE_TYPE_STR, // 文字列。
    VALUE_TYPE_UNKNOWN, // 未対応またはJSONで判別できなかった型。
}settinge_value_type;


// 設定画面へ表示する1項目。nameとexplanationは設定画面が所有する。
typedef struct{
    const char *name; // 画面に表示する設定項目名のNUL終端文字列。
    wint_t key_code; // この項目を選択する入力キー。
    settinge_value_type value_type; // 入力値の解釈方法。
    const char *explanation; // 選択時に表示する説明のNUL終端文字列。
}settings_items_data;

// 設定画面の形状、項目配列、選択位置、編集中の値。
typedef struct{
    struct box box; // 設定一覧を描く外枠。
    settings_items_data *item_data; // この構造体が所有する設定項目の動的配列。
    int settings_item_data_num; // item_dataに格納済みの有効要素数。
    int settings_item_data_allocate_num; // item_dataへ確保済みの要素数。
    int select_line; // 先頭を0とする現在の選択項目番号。
    bool value_input_mode; // 選択項目の値を入力中ならtrue。
    wchar_t input_value[SETTINGS_INPUT_MAX]; // 入力中のNUL終端ワイド文字列。
    int input_value_len; // input_valueに入っている終端を除く文字数。
}settings_screen_data;


int add_settings_screen_item(settings_screen_data *settings_screen_data,settings_items_data item_data);
int load_settings_screen_items(settings_screen_data *settings_screen_data);
void move_settings_select_line(settings_screen_data *settings_screen_data,int delta);
void set_settings_screen_box(struct editor_state *state);

#endif
