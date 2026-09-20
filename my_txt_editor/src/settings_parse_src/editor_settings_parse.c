#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cjson/cJSON.h"
#include "default_settings.h"
#include "json_read.h"
#include "path_util.h"
#include "txt_editor.h"

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

    cJSON *max_lines = cJSON_GetObjectItemCaseSensitive(json_data, "max_lines");
    if(cJSON_IsNumber(max_lines)){
        settings_data->max_lines = max_lines->valueint;
    }

    cJSON *max_line_size = cJSON_GetObjectItemCaseSensitive(json_data, "max_line_size");
    if(cJSON_IsNumber(max_line_size)){
        settings_data->max_line_size = max_line_size->valueint;
    }

    cJSON *line_number_space = cJSON_GetObjectItemCaseSensitive(json_data, "line_number_space");
    if(cJSON_IsNumber(line_number_space)){
        settings_data->line_number_space = line_number_space->valueint;
    }
    if(settings_data->line_number_space < 4){
        settings_data->line_number_space = 4;
    }

    cJSON *indent_range = cJSON_GetObjectItemCaseSensitive(json_data, "indent_range");
    if(cJSON_IsNumber(indent_range)){
        settings_data->indent_range = indent_range->valueint;
    }

    cJSON *jmp_set_cur_pos = cJSON_GetObjectItemCaseSensitive(json_data, "jmp_set_cur_pos");
    if(cJSON_IsNumber(jmp_set_cur_pos)){
        settings_data->jmp_set_cur_pos = jmp_set_cur_pos->valueint;
    }

    cJSON *default_load_line_size = cJSON_GetObjectItemCaseSensitive(json_data, "default_load_line_size");
    if(cJSON_IsNumber(default_load_line_size)){
        settings_data->default_load_line_size = default_load_line_size->valueint;
    }

    cJSON *load_buffer_lines = cJSON_GetObjectItemCaseSensitive(json_data, "load_buffer_lines");
    if(cJSON_IsNumber(load_buffer_lines)){
        settings_data->load_buffer_lines = load_buffer_lines->valueint;
    }

    cJSON *show_status_bar = cJSON_GetObjectItemCaseSensitive(json_data, "show_status_bar");
    if(cJSON_IsBool(show_status_bar)){
        settings_data->show_status_bar = cJSON_IsTrue(show_status_bar);
    }

    cJSON *status_bar_side = cJSON_GetObjectItemCaseSensitive(json_data, "status_bar_side");
    if(cJSON_IsString(status_bar_side) && status_bar_side->valuestring != NULL){
        if(strcmp(status_bar_side->valuestring, "top") == 0){
            settings_data->bar_side_state = top;
        }
        else if(strcmp(status_bar_side->valuestring, "bottom") == 0){
            settings_data->bar_side_state = bottom;
        }
    }

    cJSON *draw_split_line = cJSON_GetObjectItemCaseSensitive(json_data, "draw_split_line");
    if(cJSON_IsBool(draw_split_line)){
        settings_data->draw_split_line = cJSON_IsTrue(draw_split_line);
    }

    cJSON *show_start_menu = cJSON_GetObjectItemCaseSensitive(json_data, "show_start_menu");
    if(cJSON_IsBool(show_start_menu)){
        settings_data->show_start_menu = cJSON_IsTrue(show_start_menu);
    }

    cJSON *use_icon = cJSON_GetObjectItemCaseSensitive(json_data, "use_icon");
    if(cJSON_IsBool(use_icon)){
        settings_data->use_icon = cJSON_IsTrue(use_icon);
    }

    cJSON *built_in_syntax = cJSON_GetObjectItemCaseSensitive(json_data,"built_in_syntax");
    if(cJSON_IsBool(built_in_syntax)){
        settings_data->built_in_syntax = cJSON_IsTrue(built_in_syntax);
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
        if(cJSON_IsNumber(epoll_timeout_ms) && epoll_timeout_ms->valueint >= 0){
            settings_data->lsp.lsp_epoll_timeout_ms = epoll_timeout_ms->valueint;
        }
    }

    if(settings_data->max_line_size < 2){
        settings_data->max_line_size = MAX_LINE_SIZE;
    }
    if(settings_data->default_load_line_size < 1){
        settings_data->default_load_line_size = DEFAULT_LOAD_LINE_SIZE;
    }
    if(settings_data->load_buffer_lines < 1){
        settings_data->load_buffer_lines = LOAD_BUFFER_LINES;
    }
    if(settings_data->indent_range < 1){
        settings_data->indent_range = INDENT_RANGE;
    }

    cJSON_Delete(json_data);
}
