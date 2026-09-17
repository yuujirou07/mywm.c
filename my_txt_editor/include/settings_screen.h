#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <ncurses.h>
#include <wchar.h>
#include "editor_types.h"

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
#define SETTINGS_INPUT_MAX 64

typedef enum{
    VALUE_TYPE_BOOL,
    VALUE_TYPE_INT,
    VALUE_TYPE_STR,
    VALUE_TYPE_UNKNOWN,
}settinge_value_type;//設定項目の入力タイプ


typedef struct{
    const char *name;
    wint_t key_code;
    settinge_value_type value_type;
    const char *explanation;
}settings_items_data;

typedef struct{
    struct box box;
    settings_items_data *item_data;
    int settings_item_data_num;
    int settings_item_data_allocate_num;
    int select_line;
    bool value_input_mode;
    wchar_t input_value[SETTINGS_INPUT_MAX];
    int input_value_len;
}settings_screen_data;


int add_settings_screen_item(settings_screen_data *settings_screen_data,settings_items_data item_data);
int load_settings_screen_items(settings_screen_data *settings_screen_data);
void move_settings_select_line(settings_screen_data *settings_screen_data,int delta);

#endif
