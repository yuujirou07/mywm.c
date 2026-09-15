#include<stdlib.h>
#include<limits.h>
#include<string.h>
#include<unistd.h>
#include<cjson/cJSON.h>
#include "txt_editor.h"
#include "txt_editor_screen.h"
#include"error_log.h"
#include"json_read.h"
#include"path_util.h"

// handle_settings_screen_input(): 設定画面の終了、選択行移動、入力エラーを処理する。
// Tabでは履歴上の遷移元へ戻り、履歴が無ければエラー画面へ遷移する。
// 引数: ctx=画面履歴と描画状態を持つcontext、ch=入力文字、input_result=get_wch()の結果。
// 返り値: 入力ループを続けるならtrue、qで終了するならfalse。
bool handle_settings_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result){
    struct editor_state *state = ctx->state;
    if(ch == 'q')return false;
    if(input_result == ERR)editor_error_screen(ctx->state,"key input error");
    if(ch == '\t'){
        enum now_screen_state old_state = editor_get_screen_state_log(state,1);
        if(old_state == screen_state_log_error)editor_error_screen(state,"editor screen error");
        else editor_set_screen_state(state,old_state);
    }
    if(ch == KEY_UP || ch == 'k'){
        move_settings_select_line(&state->settings_screen_data,-1);
        state->render_flags |= RENDER_SETTINGS;
    }
    if(ch == KEY_DOWN || ch == 'j'){
        move_settings_select_line(&state->settings_screen_data,1);
        state->render_flags |= RENDER_SETTINGS;
    }
    return true;
}

// move_settings_select_line(): 設定画面の選択行をdelta分だけ動かす。
// 項目の範囲外へは出さず、端ではそのまま止める。
// 引数: settings_screen_data=選択行と項目数を持つ設定画面データ、delta=移動量。
// 返り値: なし。
void move_settings_select_line(struct settings_screen_data *settings_screen_data,int delta){
    if(settings_screen_data == NULL)return;

    int line_limit = settings_screen_data->settings_item_data_num;
    if(line_limit <= 0)return;

    int next_line = settings_screen_data->select_line + delta;
    if(next_line < 0)next_line = 0;
    if(next_line >= line_limit)next_line = line_limit - 1;
    settings_screen_data->select_line = next_line;
}


int add_settings_screen_item(struct settings_screen_data *settings_screen_data,struct settings_items_data item_data){
    if(settings_screen_data->item_data == NULL){
        settings_screen_data->settings_item_data_allocate_num = 16; 
        settings_screen_data->item_data = malloc(sizeof(struct settings_items_data) * 
            settings_screen_data->settings_item_data_allocate_num);
        if(settings_screen_data->item_data == NULL){
            error_log("item data arry allocate error");
            return -1;
        }
    }
    if(settings_screen_data->settings_item_data_num >= settings_screen_data->settings_item_data_allocate_num){
        struct settings_items_data *tmp_item_data = realloc(settings_screen_data->item_data
            ,sizeof(struct settings_items_data) * (settings_screen_data->settings_item_data_num * 2));
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

int load_settings_screen_items(struct settings_screen_data *settings_screen_data){
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
        if(!cJSON_IsString(name) || !cJSON_IsString(key) ||
           !cJSON_IsString(explanation) || key->valuestring[0] == '\0' ||
           key->valuestring[1] != '\0'){
            cJSON_Delete(root);
            return -1;
        }

        struct settings_items_data item = {
            .name = strdup(name->valuestring),
            .key_code = (unsigned char)key->valuestring[0],
            .explanation = strdup(explanation->valuestring),
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
