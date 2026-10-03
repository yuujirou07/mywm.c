

#ifndef EDIT_INPUT_COMPLETE_H
#define EDIT_INPUT_COMPLETE_H

#include<editor_types.h>
#include<stdbool.h>
#include <wchar.h>
#include <wctype.h>

#define COMPLETE_WORLD_CANDIDACY_PART_DATA_MAX_LEN 256 // 入力中の候補文字列の配列要素数。終端文字を含む。
#define COMPLETE_WORLD_MAX_COUNT 256
struct editor_input_context;
struct editor_state;

// 補完候補の文字列ポインタ配列と、その使用数・確保予定数。
typedef struct{
    wchar_t **world; // 候補文字列へのポインタ配列。init_edit_complete_data()が領域を確保する。
    int world_num; // 使用中の候補数。
    int world_allocate_num; // 候補ポインタ配列の確保予定要素数。
}complete_world_data;

// 候補ウィンドウの配置方式。定数値は下のJSON文字列配列の添字と対応する。
typedef enum{
    EDIT_COMP_FIXED_TOP_RIGHT   = 0,
    EDIT_COMP_FIXED_TOP_LEFT    = 1,
    EDIT_COMP_FIXED_BOTTOM_RIGH = 2,
    EDIT_COMP_FIXED_BOTTOM_LEFT = 3,
    EDIT_COMP_TRACKING = 4,
    EDIT_COMP_UNKNOWN  = 5,
}EDIT_COMPLETE_POSITION_MODE;

// 設定JSONのwindow.position_modeで受け付ける文字列。列挙値と同じ順序で保持する。
static const char * const EDIT_COMP_POS_DEF_WORLD[6] =
    {
        "top_right",
        "top_left",
        "bottom_right",
        "bottom_left",
        "tracking",
        "unknown",
    };

// get_edit_comp_pos_def_world(): 配置方式に対応する設定文字列を返す。
// 引数: mode=0以上get_edit_comp_pos_def_world_num()未満の列挙値。範囲外は指定できない。
// 返り値: ヘッダ内の静的な文字列。呼び出し側は解放しない。
static inline const char *get_edit_comp_pos_def_world(EDIT_COMPLETE_POSITION_MODE mode){
    return EDIT_COMP_POS_DEF_WORLD[mode];
}

// get_edit_comp_pos_def_world_num(): 配置方式の設定文字列の個数を返す。
// 返り値: EDIT_COMP_POS_DEF_WORLDの要素数。
static inline int get_edit_comp_pos_def_world_num(){
    return (int)(sizeof(EDIT_COMP_POS_DEF_WORLD)/sizeof(EDIT_COMP_POS_DEF_WORLD[0]));
}


// 候補ウィンドウの表示寸法と配置方式。
typedef struct{
    struct pos size; // x=幅、y=高さ。枠を含む端末セル・行数。
    EDIT_COMPLETE_POSITION_MODE pos_mode; // 設定JSONから読み込んだ配置方式。
}complete_show_data;

// 入力途中の補完対象文字列。countは終端文字を含まない文字数。
typedef struct{
    wchar_t complete_world_candidacy_part_str[COMPLETE_WORLD_CANDIDACY_PART_DATA_MAX_LEN]; // NUL終端の入力文字列。
    int comp_w_cand_part_d_count; // 次に文字を書き込む位置。
}complete_world_candidacy_part_data;


// 実行中の補完候補、入力途中の文字列、言語と表示設定。
typedef struct{
    bool show; // 候補ウィンドウの表示状態。
    struct box box; // 現在の候補ウィンドウの画面上の矩形。
    complete_world_data word_data; // 補完候補の配列と件数。
    complete_world_candidacy_part_data comp_world_candidacy_part_data; // 入力途中の文字列。
    language lang; // 現在の補完対象言語。
    complete_show_data show_data; // 候補ウィンドウの寸法と配置方式。
    int now_select_complete_world_num;
}edit_input_complete_data;


int init_edit_complete_data(struct editor_input_context *ctx);

int change_edit_complete_lang(struct editor_input_context *ctx,language lang);

int set_edit_comp_candidacy_part_data(struct editor_state *state,wint_t ch);

int init_edit_comt_candidacy_part_data(struct editor_state *state);


int set_complete_str(struct editor_state *state,wchar_t *wchr,int line_num);
int reduce_edit_comp_candidacy_part_data(struct editor_state *state);
#endif
