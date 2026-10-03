

#include<stdio.h>
#include<stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "default_settings.h"
#include "editor_types.h"
#include "error_log.h"
#include"input_complete.h"
#include"txt_editor.h"


// init_edit_complete_data(): 設定値から補完表示データを初期化し、候補ポインタ配列を確保する。
// 引数: ctx=stateとsettings_dataが設定済みの入力context。
// 返り値: 成功時0、候補配列の確保失敗時-1。
// 所有権: 確保したword_data.worldはstate側に保持される。
int init_edit_complete_data(struct editor_input_context *ctx){
    edit_input_complete_data *tmp_comp_data =
        &ctx->state->edit_input_complete_data;

    auto_complete_settings_data *tmp_settings_comp_data = 
        &ctx->state->settings_data->auto_complete_settings_data;

    tmp_comp_data->show_data.size = tmp_settings_comp_data->auto_complete_window_size;
    tmp_comp_data->show_data.pos_mode = tmp_settings_comp_data->auto_complete_position_mode;
    tmp_comp_data->now_select_complete_world_num = 0;
    tmp_comp_data->word_data.world_num = 0;
    tmp_comp_data->comp_world_candidacy_part_data.comp_w_cand_part_d_count = 0;
    tmp_comp_data->comp_world_candidacy_part_data.complete_world_candidacy_part_str[0] = L'\0';

    //念の為二倍に確保しておく
    tmp_comp_data->word_data.world_allocate_num = 
        tmp_settings_comp_data->auto_complete_window_size.y * 2;

    tmp_comp_data->show = false;
    tmp_comp_data->box = (struct box){(struct pos){0,0},0,0};

    tmp_comp_data->word_data.world = malloc(sizeof(wchar_t *) * tmp_comp_data->word_data.world_allocate_num);
    if(tmp_comp_data->word_data.world == NULL){
        error_log("malloc");
        return -1;
    }

    for(int i = 0;i < tmp_comp_data->word_data.world_allocate_num;i++){
        tmp_comp_data->word_data.world[i] = malloc(sizeof(wchar_t) * COMPLETE_WORLD_MAX_COUNT);
        if(tmp_comp_data->word_data.world[i] == NULL){
            error_log("malloc");
            return -1;
        }
        tmp_comp_data->word_data.world[i][0] = L'\0';
    }

    tmp_comp_data->lang = get_env_language();
    return 0;
}

// change_edit_complete_lang(): 補完対象言語を更新し、入力途中の候補文字列を空にする。
// 引数: ctx=stateが設定済みの入力context、lang=新しい補完対象言語。
// 返り値: 候補文字列の初期化結果。現実装では0。
int change_edit_complete_lang(struct editor_input_context *ctx,language lang){
    ctx->state->edit_input_complete_data.lang = lang;
    return init_edit_comt_candidacy_part_data(ctx->state);
}

// set_edit_comp_candidacy_part_data(): 入力文字を補完対象文字列の末尾へ追加する。
// 引数: state=補完データを持つ状態、ch=追加するワイド文字。
// 返り値: 追加時0、長さ判定に失敗したとき-1。現状は文字数が一致する通常の状態では常に-1。
int set_edit_comp_candidacy_part_data(struct editor_state *state,wint_t ch){
    if(COMPLETE_WORLD_CANDIDACY_PART_DATA_MAX_LEN <=
        (int)wcslen(state->edit_input_complete_data.comp_world_candidacy_part_data.complete_world_candidacy_part_str)){
        return -1;
    }
    // 入力済み文字数の位置を指し、文字と終端文字を書き込む。
    wchar_t *tmp_str =
        &state->edit_input_complete_data.
            comp_world_candidacy_part_data.
                complete_world_candidacy_part_str
                    [state->edit_input_complete_data.
                        comp_world_candidacy_part_data.
                            comp_w_cand_part_d_count];
    
    tmp_str[0] = ch;
    tmp_str[1] = L'\0';

    state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            comp_w_cand_part_d_count++;
    return 0;
}
int reduce_edit_comp_candidacy_part_data(struct editor_state *state){
    if(state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            comp_w_cand_part_d_count <= 0)return 1;

    state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            complete_world_candidacy_part_str
                [state->edit_input_complete_data.
                    comp_world_candidacy_part_data.
                    comp_w_cand_part_d_count - 1] = L'\0';

    state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            comp_w_cand_part_d_count--;
    return 0;
}

// init_edit_comt_candidacy_part_data(): 入力途中の候補文字列と文字数を空に戻す。
// 引数: state=補完データを持つ状態。
// 返り値: 0。
int init_edit_comt_candidacy_part_data(struct editor_state *state){
    state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            complete_world_candidacy_part_str[0] = '\0';

    state->edit_input_complete_data.
        comp_world_candidacy_part_data.
            comp_w_cand_part_d_count = 0;
    
    state->edit_input_complete_data.show = false;
    return 0;
}



int set_complete_str(struct editor_state *state,wchar_t *wchr,int line_num){
    if(state->edit_input_complete_data.word_data.world_allocate_num <= line_num || line_num < 0)return 0;
    wchar_t *tmp_comp_world = state->edit_input_complete_data.word_data.world[line_num];    
    int str_len = wcslen(wchr);
    if(str_len <= 0 || str_len >= COMPLETE_WORLD_MAX_COUNT)return 0;
    wmemcpy(tmp_comp_world,wchr,str_len + 1);
    state->edit_input_complete_data.word_data.world_num++;
    return 0;
}
