#include <ncurses.h>
#include<stdlib.h>
#include<string.h>
#include<wctype.h>
#include "settings_screen.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"
#include"error_log.h"



settings_items_data *get_now_select_settings_item(settings_screen_data *screen_data);

// handle_settings_screen_input(): 設定画面の終了、選択行移動、入力エラーを処理する。
// Tabでは履歴上の遷移元へ戻り、履歴が無ければエラー画面へ遷移する。
// 引数: ctx=画面履歴と描画状態を持つcontext、ch=入力文字、input_result=get_wch()の結果。
// 返り値: 入力ループを続けるならtrue、qで終了するならfalse。
bool handle_settings_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result){
    struct editor_state *state = ctx->state;
    settings_screen_data *screen_data = &state->settings_screen_data;

    if(screen_data->value_input_mode){

        if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
            screen_data->value_input_mode = false;
            my_cur_set(state,false);
        }
        else if(ch == KEY_BACKSPACE){
            if(screen_data->input_value_len > 0){
                screen_data->input_value[--screen_data->input_value_len] = L'\0';
            }
        }
        else if(input_result == OK && iswprint(ch) &&
            screen_data->input_value_len < SETTINGS_INPUT_MAX - 1){

            screen_data->input_value[screen_data->input_value_len++] = ch;
            screen_data->input_value[screen_data->input_value_len] = L'\0';
        }
        state->render_flags |= RENDER_SETTINGS;
        return true;
    }
    else if(input_result == OK && iswprint(ch)){
        for(int i = 0;i < screen_data->settings_item_data_num;i++){
            if(screen_data->item_data[i].key_code == ch){
                screen_data->value_input_mode = true;
                screen_data->input_value_len = 0;
                screen_data->input_value[0] = L'\0';
                my_cur_set(state,true);
                state->render_flags |= RENDER_SETTINGS;
                int select_item_delta = i - screen_data->select_line;
                move_settings_select_line(screen_data,select_item_delta);
            }
        }
    }

    if(ch == 'q')return false;
    if(input_result == ERR)editor_error_screen(ctx->state,"key input error");
    if(ch == '\t'){
        enum now_screen_state old_state = editor_get_screen_state_log(state,1);
        if(old_state == screen_state_log_error)editor_error_screen(state,"editor screen error");
        else editor_set_screen_state(state,old_state);
    }
    if(ch == KEY_UP || ch == 'k'){
        move_settings_select_line(screen_data,-1);
        state->render_flags |= RENDER_SETTINGS;
    }
    if(ch == KEY_DOWN || ch == 'j'){
        move_settings_select_line(screen_data,1);
        state->render_flags |= RENDER_SETTINGS;
    }
    if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
        screen_data->value_input_mode = true;
        screen_data->input_value_len = 0;
        screen_data->input_value[0] = L'\0';
        my_cur_set(state,true);
        state->render_flags |= RENDER_SETTINGS;
    }
    return true;
}

// move_settings_select_line(): 設定画面の選択行をdelta分だけ動かす。
// 項目の範囲外へは出さず、端ではそのまま止める。
// 引数: settings_screen_data=選択行と項目数を持つ設定画面データ、delta=移動量。
// 返り値: なし。
void move_settings_select_line(settings_screen_data *settings_screen_data,int delta){
    if(settings_screen_data == NULL)return;

    int line_limit = settings_screen_data->settings_item_data_num;
    if(line_limit <= 0)return;

    int next_line = settings_screen_data->select_line + delta;
    if(next_line < 0)next_line = 0;
    if(next_line >= line_limit)next_line = line_limit - 1;
    settings_screen_data->select_line = next_line;
}


// add_settings_screen_item(): 設定項目配列を必要に応じて拡張し、末尾へ1項目追加する。
// 引数: settings_screen_data=追加先、item_data=値コピーする項目。
// 返り値: 成功時0、malloc()またはrealloc()失敗時-1。
// 所有権: item_data内の文字列ポインタは複製せず、成功後はsettings_screen_dataが保持する。
int add_settings_screen_item(settings_screen_data *settings_screen_data,settings_items_data item_data){
    if(settings_screen_data->item_data == NULL){
        settings_screen_data->settings_item_data_allocate_num = 16; 
        settings_screen_data->item_data = malloc(sizeof(settings_items_data) *
            settings_screen_data->settings_item_data_allocate_num);
        if(settings_screen_data->item_data == NULL){
            error_log("item data arry allocate error");
            return -1;
        }
    }
    if(settings_screen_data->settings_item_data_num >= settings_screen_data->settings_item_data_allocate_num){
        settings_items_data *tmp_item_data = realloc(settings_screen_data->item_data
            ,sizeof(settings_items_data) * (settings_screen_data->settings_item_data_num * 2));
        if(tmp_item_data == NULL){
            error_log("settings_item_data realloc error");
            return -1;
        }
        settings_screen_data->item_data = tmp_item_data;
        settings_screen_data->settings_item_data_allocate_num *= 2;
    }
    settings_screen_data->item_data[settings_screen_data->settings_item_data_num] = item_data;
    settings_screen_data->settings_item_data_num++;
    return 0;
}

// get_now_select_settings_item(): 現在選択中の設定項目を返す。
// 引数: screen_data=項目配列と選択位置を持つ設定画面データ。
// 返り値: 選択項目への借用ポインタ。引数や選択位置が不正な場合の動作は未定義。
settings_items_data *get_now_select_settings_item(settings_screen_data *screen_data){
    return &screen_data->item_data[screen_data->select_line];
}
