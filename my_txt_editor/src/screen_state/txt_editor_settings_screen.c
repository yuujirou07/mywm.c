#include <ncurses.h>
#include<stdlib.h>
#include<limits.h>
#include<string.h>
#include<unistd.h>
#include<wctype.h>
#include<cjson/cJSON.h>
#include "settings_screen.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"
#include"error_log.h"
#include"json_read.h"
#include"path_util.h"



const char *value_type_str_list[] =
    {
    "bool",
    "int",
    };


settinge_value_type cmb_value_str_to_enum(cJSON *value_type_item);
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

// load_settings_screen_items(): settings_items.jsonを読み込み、設定画面の項目配列へ追加する。
// 引数: settings_screen_data=項目配列と件数を保持する設定画面データ。
// 返り値: 成功時0、ファイル・JSON・必須項目・メモリ確保の失敗時-1。
// 所有権: nameとexplanationを複製し、追加成功後はsettings_screen_dataが所有する。
int load_settings_screen_items(settings_screen_data *settings_screen_data){
    const char *file_name = "settings_items.json";
    char exe_dir_path[PATH_MAX];
    const char *path = NULL;

    if(access(file_name,R_OK) == 0){
        path = file_name;
    }
    else if(editor_path_from_exe_dir(exe_dir_path,sizeof(exe_dir_path),file_name) != NULL &&
            access(exe_dir_path,R_OK) == 0){
        path = exe_dir_path;
    }
    else{
        return -1;
    }

    char *buf = read_file_all(path);
    if(buf == NULL)return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!cJSON_IsArray(root)){
        cJSON_Delete(root);
        return -1;
    }

    cJSON *json_item = NULL;
    cJSON_ArrayForEach(json_item,root){
        cJSON *name = cJSON_GetObjectItemCaseSensitive(json_item,"name");
        cJSON *key = cJSON_GetObjectItemCaseSensitive(json_item,"key");
        cJSON *explanation = cJSON_GetObjectItemCaseSensitive(json_item,"explanation");
        cJSON *value_type = cJSON_GetObjectItemCaseSensitive(json_item,"value");
        if(!cJSON_IsString(name) || !cJSON_IsString(key) ||
            !cJSON_IsString(explanation) || key->valuestring[0] == '\0' ||
            !cJSON_IsString(value_type) ||key->valuestring[1] != '\0'){
            cJSON_Delete(root);
            return -1;
        }



        settings_items_data item = {
            .name = strdup(name->valuestring),
            .key_code = (unsigned char)key->valuestring[0],
            .explanation = strdup(explanation->valuestring),
            .value_type = cmb_value_str_to_enum(value_type),
        };

        if(item.name == NULL || item.explanation == NULL ||
            add_settings_screen_item(settings_screen_data,item) < 0){
            free((char *)item.name);
            free((char *)item.explanation);
            cJSON_Delete(root);
            return -1;
        }
    }

    cJSON_Delete(root);
    return 0;
}

// cmb_value_str_to_enum(): JSON文字列の設定値型をsettinge_value_typeへ変換する。
// 引数: value_type_item=型名を保持するcJSON文字列項目。
// 返り値: 一致する型。未対応文字列ならVALUE_TYPE_UNKNOWN。
// 所有権: cJSON内の文字列を借用し、解放しない。
settinge_value_type cmb_value_str_to_enum(cJSON *value_type_item){
    char *value_type_str = cJSON_GetStringValue(value_type_item);
    for(size_t i = 0;i < sizeof(value_type_str_list)/sizeof(value_type_str_list[0]);i++){
        if(strcmp(value_type_str,value_type_str_list[i]) == 0)return (settinge_value_type)i;
    }
    return VALUE_TYPE_UNKNOWN;
}

// get_now_select_settings_item(): 現在選択中の設定項目を返す。
// 引数: screen_data=項目配列と選択位置を持つ設定画面データ。
// 返り値: 選択項目への借用ポインタ。引数や選択位置が不正な場合の動作は未定義。
settings_items_data *get_now_select_settings_item(settings_screen_data *screen_data){
    return &screen_data->item_data[screen_data->select_line];
}
