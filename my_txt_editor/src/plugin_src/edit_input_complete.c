

#include<stdio.h>
#include<stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "default_settings.h"
#include "editor_types.h"
#include "error_log.h"
#include"input_complete.h"
#include"txt_editor.h"


// ctxの設定値で補完状態を初期化し、候補ポインタ配列と各文字列領域をstateへ確保する。
// 返り値: 成功0、確保失敗-1。途中失敗でも確保済み領域は残り、成功後の領域はstate側で解放する。
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

// 補完対象言語を更新し、入力途中の候補文字列を空にする。
// 引数: ctx=stateが設定済みの入力context、lang=新しい補完対象言語。 返り値: 候補文字列の初期化結果。現実装では0。
int change_edit_complete_lang(struct editor_input_context *ctx,language lang){
    ctx->state->edit_input_complete_data.lang = lang;
    return init_edit_comt_candidacy_part_data(ctx->state);
}

// stateの補完入力文字列へワイド文字chを追加し、文字数とNUL終端を更新する。
// 返り値: 追加0、既存文字列長が上限以上なら-1。文字数と実長を一致させ、文字と終端の2要素分の空きが必要。
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
// stateの補完入力文字列の末尾1文字を削除し、文字数を減らす。
// 返り値: 削除0、既に空なら変更せず1。文字数と文字列は整合していること。
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

// stateの補完入力文字列と文字数を空にし、候補ウィンドウを非表示にする。
// 返り値: 常に0。候補配列そのものは解放・初期化しない。
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



// stateの確保済み候補配列のline_numへNUL終端のwchrをコピーし、候補数を1増やす。
// 返り値: 常に0。添字範囲外・空文字・長さ上限以上なら何もしない。上書きでも候補数は増える。
int set_complete_str(struct editor_state *state,wchar_t *wchr,int line_num){
    if(state->edit_input_complete_data.word_data.world_allocate_num <= line_num || line_num < 0)return 0;
    wchar_t *tmp_comp_world = state->edit_input_complete_data.word_data.world[line_num];    
    int str_len = wcslen(wchr);
    if(str_len <= 0 || str_len >= COMPLETE_WORLD_MAX_COUNT)return 0;
    wmemcpy(tmp_comp_world,wchr,str_len + 1);
    state->edit_input_complete_data.word_data.world_num++;
    return 0;
}


