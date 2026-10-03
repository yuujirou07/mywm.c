#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cjson/cJSON.h"
#include "default_settings.h"
#include "input_complete.h"
#include "json_read.h"
#include "path_util.h"
#include "txt_editor.h"

static bool json_int_in_range(const cJSON *item, int min, int max){
    return cJSON_IsNumber(item) && item->valuedouble >= min &&
        item->valuedouble <= max && item->valuedouble == item->valueint;
}

// load_custom_editor_settings(): 設定JSONがあれば読み込み、既定値を上書きする。
// editor_settings/my_txt_editor_settings.jsonをカレントディレクトリ→実行ファイルの隣、
// の順で探す。
// 引数: settings_data=上書き対象の設定構造体。
// 返り値: なし。設定ファイルが無い、または不正な場合は既定値のまま戻る。
void load_custom_editor_settings(struct editor_settings *settings_data){
    const char *settings_name = "editor_settings/my_txt_editor_settings.json";
    char exe_dir_path[PATH_MAX];
    const char *path = NULL;

    if(access(settings_name, R_OK) == 0){
        path = settings_name;
    }
    else if(editor_path_from_exe_dir(exe_dir_path, sizeof(exe_dir_path), settings_name) != NULL &&
            access(exe_dir_path, R_OK) == 0){
        path = exe_dir_path;
    }
    else{
        return;
    }

    char *buf = read_file_all(path);
    if(buf == NULL){
        return;
    }

    cJSON *json_data = cJSON_Parse(buf);
    free(buf);
    if(json_data == NULL){
        return;
    }

    // 旧形式の設定ファイルはルートから読み込む。
    cJSON *buffer = cJSON_GetObjectItemCaseSensitive(json_data, "buffer");
    if(buffer == NULL)buffer = json_data;
    cJSON *display = cJSON_GetObjectItemCaseSensitive(json_data, "display");
    if(display == NULL)display = json_data;
    cJSON *editor = cJSON_GetObjectItemCaseSensitive(json_data, "editor");
    if(editor == NULL)editor = json_data;

    cJSON *max_lines = cJSON_GetObjectItemCaseSensitive(buffer, "max_lines");
    if(json_int_in_range(max_lines, 1, INT_MAX)){
        settings_data->max_lines = max_lines->valueint;
    }

    cJSON *max_line_size = cJSON_GetObjectItemCaseSensitive(buffer, "max_line_size");
    if(json_int_in_range(max_line_size, 2, EDITOR_LINE_COL_MAX)){
        settings_data->max_line_size = max_line_size->valueint;
    }

    cJSON *line_number_space = cJSON_GetObjectItemCaseSensitive(display, "line_number_space");
    if(json_int_in_range(line_number_space, 4, INT_MAX - 1)){
        settings_data->line_number_space = line_number_space->valueint;
    }

    cJSON *indent_range = cJSON_GetObjectItemCaseSensitive(editor, "indent_range");
    if(json_int_in_range(indent_range, 1, INT_MAX)){
        settings_data->indent_range = indent_range->valueint;
    }

    cJSON *jmp_set_cur_pos = cJSON_GetObjectItemCaseSensitive(editor, "jmp_set_cur_pos");
    if(json_int_in_range(jmp_set_cur_pos, 0, INT_MAX)){
        settings_data->jmp_set_cur_pos = jmp_set_cur_pos->valueint;
    }

    cJSON *default_load_line_size = cJSON_GetObjectItemCaseSensitive(buffer, "default_load_line_size");
    if(json_int_in_range(default_load_line_size, 1, INT_MAX)){
        settings_data->default_load_line_size = default_load_line_size->valueint;
    }

    cJSON *load_buffer_lines = cJSON_GetObjectItemCaseSensitive(buffer, "load_buffer_lines");
    if(json_int_in_range(load_buffer_lines, 1, INT_MAX)){
        settings_data->load_buffer_lines = load_buffer_lines->valueint;
    }

    cJSON *show_status_bar = cJSON_GetObjectItemCaseSensitive(display, "show_status_bar");
    if(cJSON_IsBool(show_status_bar)){
        settings_data->show_status_bar = cJSON_IsTrue(show_status_bar);
    }

    cJSON *status_bar_side = cJSON_GetObjectItemCaseSensitive(display, "status_bar_side");
    if(cJSON_IsString(status_bar_side) && status_bar_side->valuestring != NULL){
        if(strcmp(status_bar_side->valuestring, "top") == 0){
            settings_data->bar_side_state = top;
        }
        else if(strcmp(status_bar_side->valuestring, "bottom") == 0){
            settings_data->bar_side_state = bottom;
        }
    }

    cJSON *draw_split_line = cJSON_GetObjectItemCaseSensitive(display, "draw_split_line");
    if(cJSON_IsBool(draw_split_line)){
        settings_data->draw_split_line = cJSON_IsTrue(draw_split_line);
    }

    cJSON *show_start_menu = cJSON_GetObjectItemCaseSensitive(display, "show_start_menu");
    if(cJSON_IsBool(show_start_menu)){
        settings_data->show_start_menu = cJSON_IsTrue(show_start_menu);
    }

    cJSON *use_icon = cJSON_GetObjectItemCaseSensitive(display, "use_icon");
    if(cJSON_IsBool(use_icon)){
        settings_data->use_icon = cJSON_IsTrue(use_icon);
    }

    cJSON *built_in_syntax = cJSON_GetObjectItemCaseSensitive(editor, "built_in_syntax");
    if(cJSON_IsBool(built_in_syntax)){
        settings_data->built_in_syntax = cJSON_IsTrue(built_in_syntax);
    }

    cJSON *auto_complete = cJSON_GetObjectItemCaseSensitive(json_data, "auto_complete");
    if(auto_complete == NULL){
        auto_complete = cJSON_GetObjectItemCaseSensitive(editor, "auto_complete");
    }
    if(cJSON_IsObject(auto_complete)){
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(auto_complete,"enabled");
        if(cJSON_IsBool(enabled)){
            settings_data->auto_complete_settings_data.auto_complete_enabled = cJSON_IsTrue(enabled);
        }
        cJSON *window = cJSON_GetObjectItemCaseSensitive(auto_complete,"window");
        if(cJSON_IsObject(window)){
            cJSON *show = cJSON_GetObjectItemCaseSensitive(window,"show");
            if(cJSON_IsBool(show)){
                settings_data->auto_complete_settings_data.auto_complete_window_enable = cJSON_IsTrue(show);
            }
            cJSON *width = cJSON_GetObjectItemCaseSensitive(window,"width");
            if(json_int_in_range(width, 3, INT_MAX / 2)){
                settings_data->auto_complete_settings_data.auto_complete_window_size.x = width->valueint;
            }
            cJSON *height = cJSON_GetObjectItemCaseSensitive(window,"height");
            if(json_int_in_range(height, 3, INT_MAX / 2)){
                settings_data->auto_complete_settings_data.auto_complete_window_size.y = height->valueint;
            }
            // 不明な文字列や文字列以外の値では既定の配置方式を保持する。
            cJSON *pos_mode = cJSON_GetObjectItemCaseSensitive(window,"position_mode");
            if(cJSON_IsString(pos_mode) && pos_mode->valuestring != NULL){
                for(int i = 0; i < get_edit_comp_pos_def_world_num(); i++){
                    if(strcmp(pos_mode->valuestring, get_edit_comp_pos_def_world(i)) == 0){
                        settings_data->auto_complete_settings_data.auto_complete_position_mode = i;
                        break;
                    }
                }
            }
        }
    }
    else if(cJSON_IsBool(auto_complete)){
        settings_data->auto_complete_settings_data.auto_complete_enabled = cJSON_IsTrue(auto_complete);
    }

    cJSON *settings_language = cJSON_GetObjectItemCaseSensitive(json_data,"settings_language");
    if(cJSON_IsString(settings_language)){
        for(int i = 0;i < (int)(sizeof(SETTINGS_LANGUAGE_JSON_KEY_STR)/sizeof(SETTINGS_LANGUAGE_JSON_KEY_STR[0]));i++){
            if(strcmp(settings_language->string,SETTINGS_LANGUAGE_JSON_KEY_STR[i]) == 0){
                
            }
        }
    }

    cJSON *lsp = cJSON_GetObjectItemCaseSensitive(json_data, "lsp");
    if(cJSON_IsObject(lsp)){
        cJSON *launch_startup_editor =
            cJSON_GetObjectItemCaseSensitive(lsp, "launch_startup_editor");
        cJSON *epoll_timeout_ms =
            cJSON_GetObjectItemCaseSensitive(lsp, "epoll_timeout_ms");

        if(cJSON_IsBool(launch_startup_editor)){
            settings_data->lsp.lsp_launch_startup_editor =
                cJSON_IsTrue(launch_startup_editor);
        }
        if(json_int_in_range(epoll_timeout_ms, 0, INT_MAX)){
            settings_data->lsp.lsp_epoll_timeout_ms = epoll_timeout_ms->valueint;
        }
    }

    cJSON_Delete(json_data);
}
