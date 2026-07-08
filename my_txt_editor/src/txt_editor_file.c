#include <dirent.h>
#include <linux/limits.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>
#include"cjson/cJSON.h"
#include "txt_editor.h"
#include "json_read.h"
#include"default_settings.h"

// load_dir_table(): path_name配下のディレクトリエントリを読み込み、
// ファイルブラウザ表示用の固定幅テーブルへ詰める。
// 引数: state=ファイルブラウザ領域と件数、table=書き込み先テーブル、table_size=tableのバイト数、path_name=読むディレクトリ。
// 返り値: なし。
void load_dir_table(struct editor_state *state,char *table,int table_size,char *path_name){
    state->dir_num = 0;
    if(state->file_browser_area.w <= 0 || state->file_browser_area.h <= 0 || table_size <= 0){
        return;
    }

    DIR *dir = opendir(path_name);
    if(!dir){
        perror("/");
        exit(EXIT_FAILURE);
    }
    struct dirent *ent;
    int draw_dir_name_line_counter = 0;
    memset(table,0,table_size * sizeof(char));
    while ((ent = readdir(dir)) && draw_dir_name_line_counter < state->file_browser_area.h) {
        int idx = state->file_browser_area.w * draw_dir_name_line_counter++;
        char name[256] = {0};

        strncpy(name, ent->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        int max_len = state->file_browser_area.w - 1;
        if(max_len <= 0){
            continue;
        }
        if ((int)strlen(name) > max_len && max_len > 3) {
            strncpy(table + idx, name, max_len - 3);
            memcpy(table + idx + max_len - 3, "...", 3);
        } else if ((int)strlen(name) > max_len) {
            strncpy(table + idx, name, max_len);
        } else {
            strncpy(table + idx, name, max_len);
        }

        state->dir_num = draw_dir_name_line_counter;
    }
    closedir(dir);
    if(state->dir_num <= 0){
        state->file_select_line = 0;
    }
    else if(state->file_select_line >= state->dir_num){
        state->file_select_line = state->dir_num - 1;
    }
}

// load_file(): ファイルブラウザで選択中の名前を取り出し、Cファイルなら読み込み用に開く。
// 開けない場合や対象外の拡張子ならエラー画面へ切り替える。
// 引数: state=選択行とファイル状態、table=固定幅のファイル名一覧、path_name=現在ディレクトリ、select_state=選択結果の書き込み先。
// 返り値: なし。成功時はstate->file_data.now_open_fileにFILE*を保存する。
void load_file(struct editor_state *state, char *table,char *path_name,struct file_browse_select_state *select_state){
    select_state->select_name[0] = '\0';
    if(state->file_select_line < 0 || state->file_select_line >= state->dir_num){
        select_state->select_state = error;
        return;
    }

    int idx = state->file_select_line *state->file_browser_area.w;
    char *file_name_start_ptr = table+idx;
    char *file_name_end_ptr = strchr(file_name_start_ptr,'\0');

    if(file_name_start_ptr == NULL){
        editor_error_screen(state, "can not load file");
        return;
    }
    int file_name_size = file_name_end_ptr - file_name_start_ptr;
    char file_name[file_name_size+1];
    memcpy(file_name,file_name_start_ptr,file_name_size*sizeof(char));
    file_name[file_name_size] = '\0';

    // file_nameだけでstat/fopenすると起動時のカレントディレクトリ基準になる。
    // file_browseで移動した先を使うため、path_nameと結合した絶対/現在パスで扱う。
    char path_name_buff[PATH_MAX];
    int path_len = snprintf(path_name_buff, sizeof(path_name_buff), "%s/%s", path_name, file_name);
    if(path_len < 0 || (size_t)path_len >= sizeof(path_name_buff)){
        editor_error_screen(state, "path too long");
        select_state->select_state = error;
        return;
    }

    char *ptr = strchr(file_name,'.');
    if(ptr == NULL){
        //ディレクトリ判定
        struct stat st;
        if (stat(path_name_buff, &st) != 0) {
            editor_error_screen(state,"is this file ? i think this is not file maybe");
            select_state->select_state = error;
            return;
        }
        if(S_ISDIR(st.st_mode)){
            select_state->select_state =  folder;
            snprintf(select_state->select_name, sizeof(select_state->select_name), "%s", file_name);
            return;
        }
        editor_error_screen(state,"is this file ? i think this is not file maybe");
        return;
    }
    select_state->select_state = file;
    FILE *file = fopen(path_name_buff,"r");
    if(file == NULL){
        editor_error_screen(state,"can not open file");
        return;
    }
    state->file_data.now_open_file = file;
    snprintf(state->file_data.now_open_path_name,
         sizeof(state->file_data.now_open_path_name), "%s",path_name_buff);
    return;
}

// set_line_memory(): ファイル各行の開始位置(ftell)を保存し、後で任意行へfseekできるようにする。
// 引数: state=開いているFILE*と行開始位置配列。
// 返り値: なし。
void set_line_memory(struct editor_state *state){
    int max_line_size = state->settings_data->max_line_size;
    char dummy_buff[max_line_size];
    for(int i = 0; i < state->settings_data->default_load_line_size; i++){
        state->file_data.file_line_start_num[state->file_data.file_line_start_num_counter++] = ftell(state->file_data.now_open_file);
        if(fgets(dummy_buff, max_line_size, state->file_data.now_open_file) == NULL){
            state->file_data.description_line_end = i;
            return;
        }
        while(strlen(dummy_buff) == (size_t)(max_line_size - 1) && dummy_buff[max_line_size - 2] != '\n'){
            if(fgets(dummy_buff, max_line_size, state->file_data.now_open_file) == NULL){
                return;
            }
        }
    }
}

// load_string_data(): 保存済みの行開始位置から指定行数分だけ読み込み、
// file_str_dataへ文字列として格納する。
// 引数: state=読み込み元FILE*と格納先、load_start_line=開始行、load_size=読み込む行数。
// 返り値: なし。
void load_string_data(struct editor_state *state,long load_start_line,int load_size){
    if(load_start_line >= state->file_data.file_line_start_num_counter){
        exit(1);
    }
    fseek(state->file_data.now_open_file,state->file_data.file_line_start_num[load_start_line],SEEK_SET);
    char **file_line_data = state->file_data.file_str_data;
    for(int i = 0;i < load_size;i++){
        if(file_line_data[i] == NULL) break;
        char *result = fgets(file_line_data[i],state->write_area.w,state->file_data.now_open_file);
        if(result == NULL){
            break;
        }
    }
}

// load_all_lines(): 開いたファイル全体を編集用のwide-char行バッファへ読み込む。
// 行数に合わせて既存の編集バッファを確保し直す。
// 引数: state=開いているFILE*・行開始位置・編集バッファ。
// 返り値: なし。
void load_all_lines(struct editor_state *state){
    long total = state->file_data.file_line_start_num_counter;
    if(total < 1) total = 1;
    int w = state->write_area.w;
    if(w < 1){
        editor_error_screen(state, "invalid screen width");
        exit(1);
    }

    free(state->str.line_str_data);
    free(state->str.line);
    state->str.line_str_data = calloc((size_t)total * w, sizeof(wint_t));
    state->str.line          = calloc((size_t)total, sizeof(int));
    if(state->str.line_str_data == NULL || state->str.line == NULL){
        editor_error_screen(state, "can not allocate file buffer");
        exit(1);
    }
    state->str.line_capacity = (int)total;
    state->str.col_capacity = w;

    int max_line_size = state->settings_data->max_line_size;
    char buf[max_line_size];
    wchar_t wide_buf[max_line_size];
    for(long i = 0; i < state->file_data.file_line_start_num_counter; i++){
        fseek(state->file_data.now_open_file, state->file_data.file_line_start_num[i], SEEK_SET);
        if(fgets(buf, max_line_size, state->file_data.now_open_file) == NULL){
            state->str.line[i] = 0;
            continue;
        }
        int len = strlen(buf);
        if(len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        if(len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

        memset(wide_buf, 0, sizeof(wide_buf));
        size_t converted = mbstowcs(wide_buf, buf, max_line_size - 1);
        if (converted == (size_t)-1) {
            state->str.line[i] = 0;
            state->str.line_str_data[i * w] = 0;
            continue;
        }

        int visible_width = 0;
        for(size_t j = 0; j < converted && visible_width < w; j++){
            if(wide_buf[j] == '\t'){
                for(int k = 0; k < state->settings_data->indent_range && visible_width < w; k++){
                    state->str.line_str_data[i * w + visible_width] = L' ';
                    visible_width++;
                }
                continue;
            }

            // state->str.lineは文字数ではなく画面上の桁数として使う。
            // 日本語など2桁幅の文字でもカーソル位置と配列位置が合うように、
            // line_str_dataもvisible_widthの位置へ配置する。
            int char_width = wcwidth(wide_buf[j]);
            if(char_width < 1){
                char_width = 1;
            }
            if(visible_width + char_width > w){
                break;
            }

            state->str.line_str_data[i * w + visible_width] = wide_buf[j];
            visible_width += char_width;
        }
        state->str.line[i] = visible_width;
    }
}

// load_view_from_cursor(): 現在の論理カーソル行から表示用の行データを読み込む。
// 引数: state=現在の論理カーソル行とファイル読み込み状態。
// 返り値: なし。
void load_view_from_cursor(struct editor_state *state){
    load_string_data(state, state->mouse.now_mouce_line, state->settings_data->load_buffer_lines);
}

// save_file(): 現在の編集バッファを開いているファイルパスへ書き戻す。
// line_str_dataは画面セル位置に合わせているため、0のセルは書かずに飛ばす。
// 引数: state=保存先パスと編集バッファを持つエディタ状態。
// 返り値: なし。
void save_file(struct editor_state *state){
    if(state->file_data.now_open_path_name[0] == '\0'){
        if(state->settings_data->ask_make_file){
            state->screen_state = ask_make_file_mode;
            return;
        }
        else{
            editor_error_screen(state, "no file opened");
        }
        return;
    }

    FILE *file = fopen(state->file_data.now_open_path_name, "w");
    if(file == NULL){
        editor_error_screen(state, "can not save file");
        return;
    }

    int w = editor_col_limit(state);
    int line_count = editor_line_limit(state);
    for(int line = 0; line < line_count; line++){
        int max_col = state->str.line[line];
        if(max_col > w) max_col = w;

        for(int col = 0; col < max_col; col++){
            wint_t cell = state->str.line_str_data[line * state->str.col_capacity + col];
            if(cell == 0) continue;
            if(fputwc((wchar_t)cell, file) == WEOF){
                fclose(file);
                editor_error_screen(state, "can not write file");
                return;
            }
        }
        fputwc('\n', file);
    }
    fclose(file);
}

// load_screen_size(): ファイル読み込み後に行開始位置と編集バッファを作り直し、
// 表示開始行とカーソル行を先頭へ戻す。
// 引数: state=ファイル読み込み後に初期化するエディタ状態。
// 返り値: なし。
void load_screen_size(struct editor_state *state){
    state->file_data.file_line_start_num_counter = 0;
    set_line_memory(state);
    load_all_lines(state);
    state->scr.scr_start_num = 0;
    state->mouse.now_mouce_line = 0;
}

// load_default_editor_settings(): エディタ設定へコンパイル時の既定値を入れる。
// 引数: settings_data=初期化する設定構造体。
// 返り値: なし。
void load_default_editor_settings(struct editor_settings *settings_data){
    settings_data->default_load_line_size       = DEFAULT_LOAD_LINE_SiZE;
    settings_data->load_buffer_lines            = LOAD_BUFFER_LINES;
    settings_data->max_line_size                = MAX_LINE_SIZE;
    settings_data->max_lines                    = MAX_LINES;
    settings_data->line_number_space            = LINE_NUMBER_SPACE;
    settings_data->indent_range                 = INDENT_RANGE;
    settings_data->jmp_set_cur_pos              = JMP_SET_CUR_POS;
    settings_data->bar_side_state               = DEFAULT_STATUS_BAR_SIDE;
    settings_data->show_status_bar              = SHOW_STATUS_BAR;
    settings_data->draw_split_line              = DEFAULT_DRAW_SPLIT_LINE;
    settings_data->ask_make_file                = DEFAULT_ASK_MAKE_FILE;
    settings_data->file_select_scene_lighting   = DEFAULT_FILE_SELECT_SCENE_LIGHTING;
    settings_data->show_start_menu              = DEFAULT_SHOW_START_MENU;
}

// load_custom_editor_settings(): 設定JSONがあれば読み込み、既定値を上書きする。
// 引数: settings_data=上書き対象の設定構造体。
// 返り値: なし。設定ファイルが無い、または不正な場合は既定値のまま戻る。
void load_custom_editor_settings(struct editor_settings *settings_data){
    const char *settings_path = "my_txt_editor_settings.json";
    const char *settings_abs_path = "/home/yuujirou07/vscode_proj/mywm_proj/my_txt_editor/my_txt_editor_settings.json";
    const char *path = NULL;

    if(access(settings_path, R_OK) == 0){
        path = settings_path;
    }
    else if(access(settings_abs_path, R_OK) == 0){
        path = settings_abs_path;
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

    if(settings_data->max_line_size < 2){
        settings_data->max_line_size = MAX_LINE_SIZE;
    }
    if(settings_data->default_load_line_size < 1){
        settings_data->default_load_line_size = DEFAULT_LOAD_LINE_SiZE;
    }
    if(settings_data->load_buffer_lines < 1){
        settings_data->load_buffer_lines = LOAD_BUFFER_LINES;
    }
    if(settings_data->indent_range < 1){
        settings_data->indent_range = INDENT_RANGE;
    }

    cJSON_Delete(json_data);
}

void make_new_file(){

}


void ask_new_file_name(struct pos str_start_pos,int w,int h){
    char *ask_str = "write a new file name";
    int str_len = strlen(ask_str);
    int ask_str_start_pos_x = str_start_pos.x + ((w - str_len)/2);
    mvaddstr(str_start_pos.y,ask_str_start_pos_x,ask_str);
}
