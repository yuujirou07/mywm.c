#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <termios.h>
#include <time.h>
#include <wchar.h>
#include <sys/stat.h>
#include <unistd.h>
#include<dirent.h>
#include<sys/stat.h>
#include "error_log.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"
#include "json_read.h"
#include"default_settings.h"
#include"txt_editor_syntax.h"

// path_nameの全項目を呼び出し側所有の*tableへ読み、*table_rowsを必要時拡張する。stateはブラウザ状態。
// start_num以降の可視件数を*table_num、全件数を*dir_numへ返す。返り値: 成功0、引数・読込・確保の失敗-1。
int load_dir_table(struct editor_state *state,struct dir_entry **table,int *table_rows,char *path_name,int start_num,int *dir_num,int *table_num){
    if(table == NULL || table_rows == NULL || dir_num == NULL || table_num == NULL)return -1;
    *dir_num = 0;
    *table_num = 0;
    if(*table == NULL || *table_rows <= 0 || state->file_browse.area.h <= 0){
        return -1;
    }
    if(start_num < 0)start_num = 0;

    // 読み込む件数はテーブルの行数と表示できる行数の小さい方まで。
    DIR *dir = opendir(path_name);
    if(!dir){
        perror("/");
        return -1;
    }
    struct dirent *ent;
    int entry_num = 0;
    // 先に件数だけ数えて一度でテーブルを確保し、rewinddirで読み直す2パス方式にしている。
    while(readdir(dir) != NULL){
        entry_num++;
    }
    if(entry_num > *table_rows){
        struct dir_entry *new_table = realloc(*table,
            (size_t)entry_num * sizeof(*new_table));
        if(new_table == NULL){
            closedir(dir);
            return -1;
        }
        *table = new_table;
        *table_rows = entry_num;
    }
    rewinddir(dir);
    memset(*table,0,(size_t)*table_rows * sizeof(**table));
    entry_num = 0;
    while ((ent = readdir(dir))) {
        snprintf((*table)[entry_num].name,
                 sizeof((*table)[entry_num].name), "%s", ent->d_name);
        (*table)[entry_num].d_type = ent->d_type;
        entry_num++;
    }
    closedir(dir);
    *dir_num = entry_num;
    *table_num = (start_num < entry_num) ? entry_num - start_num : 0;
    if(*table_num > state->file_browse.area.h)*table_num = state->file_browse.area.h;

    if(*table_num > 0 && state->file_browse.select_line.now_line >= *table_num){
        file_select_line_update(&state->file_browse.select_line,*table_num - 1);
    }
    return 0;
}

// stateの選択行をtable/table_numから開く。tableがNULLならpath_nameを直接使い、それ以外は親パスとする。
// 返り値: なし。種類・名前をselect_stateへ返し、成功時のFILEは旧FILEを閉じてstateが所有する。失敗はerror。
void load_file(struct editor_state *state,struct dir_entry *table,int table_num,
                    char *path_name,struct file_browse_select_state *select_state){
    select_state->select_name[0] = '\0';
    select_state->select_state = error;
    if(path_name == NULL || path_name[0] == '\0'){
        return;
    }

    char path_name_buff[PATH_MAX];
    char *file_name;
    if(table == NULL){
        int path_len = snprintf(path_name_buff,sizeof(path_name_buff),"%s",path_name);
        if(path_len < 0 || (size_t)path_len >= sizeof(path_name_buff)){
            editor_error_screen(state,"path too long");
            select_state->select_state = error;
            return;
        }
        file_name = strrchr(path_name_buff,'/');
        file_name = (file_name != NULL) ? file_name + 1 : path_name_buff;
    }
    else{
        if(state->file_browse.select_line.now_line < 0 ||
           state->file_browse.select_line.now_line >= table_num){
            select_state->select_state = error;
            return;
        }

        int entry_index = state->file_browse.select_line.now_logical_line +
            state->file_browse.select_line.now_line;
        file_name = table[entry_index].name;

        const char *separator = strcmp(path_name, "/") == 0 ? "" : "/";
        int path_len = snprintf(path_name_buff, sizeof(path_name_buff), "%s%s%s",
                                path_name, separator, file_name);
        if(path_len < 0 || (size_t)path_len >= sizeof(path_name_buff)){
            editor_error_screen(state, "path too long");
            select_state->select_state = error;
            return;
        }
    }
    
    struct stat st;
    if(stat(path_name_buff,&st) != 0){
        editor_error_screen(state,"can not find file");
        return;
    }
    if(S_ISDIR(st.st_mode)){
        size_t file_name_size = strlen(file_name);
        if(file_name_size >= sizeof(select_state->select_name)){
            editor_error_screen(state,"file name too long");
            return;
        }
        select_state->select_state = folder;
        memcpy(select_state->select_name,file_name,file_name_size + 1);
        return;
    }
    if(!S_ISREG(st.st_mode)){
        editor_error_screen(state,"is not regular file");
        return;
    }

    FILE *now_file = fopen(path_name_buff,"r");
    if(now_file == NULL){
        editor_error_screen(state,"can not open file");
        return;
    }
    select_state->select_state = file;
    //初回はNULLなので除外する
    // 新しいファイルを開けた後で旧FILEを閉じる。開けなかったときに現在のファイルを失わないため。
    if(state->file_data.now_open_file != NULL){
        fclose(state->file_data.now_open_file);
    }

    state->file_data.now_open_file = now_file;
    snprintf(state->file_data.now_open_path_name,
         sizeof(state->file_data.now_open_path_name), "%s",path_name_buff);
    
    
    return;
}

// 編集バッファと行情報配列をまとめて解放する。
// 引数: state=解放対象のエディタ状態。 返り値: なし。NULLのstateは何もしない。解放後の各ポインタはNULLに戻す。
void editor_free_text_buffer(struct editor_state *state){
    if(state == NULL){
        return;
    }
    free(state->str.wint_line_str_data);
    free(state->str.line);
    free(state->str.line_offset);
    free(state->str.line_cap);
    state->str.wint_line_str_data = NULL;
    state->str.line               = NULL;
    state->str.line_offset        = NULL;
    state->str.line_cap           = NULL;
    state->str.total_capacity     = 0;
    state->str.line_capacity      = 0;
}

// stateの旧バッファを解放し、line_count行分の情報とtotal_capacity個のwint_tをゼロ初期化して確保する。
// 返り値: 成功true、失敗false。引数不正は旧領域を保持、確保失敗は全解放。成功後の行オフセット・容量は呼び出し側で設定する。
bool editor_alloc_text_buffer(struct editor_state *state, int line_count, long total_capacity){
    if(state == NULL || line_count < 1 || total_capacity < 1){
        return false;
    }
    // 要素数×sizeof(wint_t)がsize_tを超えないことを確認する
    if((size_t)total_capacity > SIZE_MAX / sizeof(wint_t)){
        return false;
    }

    editor_free_text_buffer(state);

    state->str.wint_line_str_data = calloc((size_t)total_capacity, sizeof(wint_t));
    state->str.line               = calloc((size_t)line_count, sizeof(int));
    state->str.line_offset        = calloc((size_t)line_count, sizeof(long));
    state->str.line_cap           = calloc((size_t)line_count, sizeof(int));
    if(state->str.wint_line_str_data == NULL || state->str.line == NULL ||
       state->str.line_offset == NULL || state->str.line_cap == NULL){
        editor_free_text_buffer(state);
        return false;
    }
    
    state->str.total_capacity = total_capacity;
    state->str.line_capacity  = line_count;
    return true;
}

// stateの論理行lineにneedセル以上を確保し、後続行のデータとオフセットも更新する。
// 返り値: 成功true、不正な状態・上限超過・確保失敗false。失敗時は保持し、成功時は既存セルポインタが無効になり得る。
bool editor_ensure_line_cap(struct editor_state *state, int line, int need){
    if(state == NULL || state->str.wint_line_str_data == NULL ||
       state->str.line_offset == NULL || state->str.line_cap == NULL){
        return false;
    }
    if(line < 0 || line >= state->str.line_capacity || need < 0){
        return false;
    }
    if(need <= state->str.line_cap[line]){
        return true;
    }
    if(need > EDITOR_LINE_COL_MAX){
        return false;
    }

    // 全行の本文は1本の連続バッファに詰めて持つ。1行だけ伸ばすと後続行を丸ごとずらし、
    // 各行のoffsetも更新する必要がある。倍々で伸ばすのはこの高コストな操作の回数を減らすため。
    int old_cap = state->str.line_cap[line];
    int new_cap = (old_cap > 0) ? old_cap : EDITOR_LINE_COL_SLACK;
    while(new_cap < need){
        if(new_cap > EDITOR_LINE_COL_MAX / 2){
            new_cap = EDITOR_LINE_COL_MAX;
            break;
        }
        new_cap *= 2;
    }

    long delta     = (long)new_cap - old_cap;
    long new_total = state->str.total_capacity + delta;
    if(new_total < 0 || (size_t)new_total > SIZE_MAX / sizeof(wint_t)){
        return false;
    }

    wint_t *buf = realloc(state->str.wint_line_str_data,
                          (size_t)new_total * sizeof(wint_t));
    if(buf == NULL){
        return false;
    }

    // 対象行の直後から末尾までをdelta分後方へ動かし、空いた隙間を0で埋める
    long tail_start = state->str.line_offset[line] + old_cap;
    long tail_len   = state->str.total_capacity - tail_start;
    if(tail_len > 0){
        memmove(&buf[tail_start + delta], &buf[tail_start],
                (size_t)tail_len * sizeof(wint_t));
    }
    memset(&buf[tail_start], 0, (size_t)delta * sizeof(wint_t));

    for(int i = line + 1; i < state->str.line_capacity; i++){
        state->str.line_offset[i] += delta;
    }

    state->str.wint_line_str_data = buf;
    state->str.line_cap[line]     = new_cap;
    state->str.total_capacity     = new_total;
    return true;
}

// stateの行情報配列をneed_rows行以上へ拡張する。本文セルの容量は増やさない。
// 返り値: 成功true、不正な状態・確保失敗false。一部の再確保だけ成功した場合もポインタは更新されるが行容量は保持する。
bool editor_ensure_row_capacity(struct editor_state *state, int need_rows){
    if(state == NULL || state->str.line == NULL || state->str.line_offset == NULL ||
       state->str.line_cap == NULL || need_rows < 0){
        return false;
    }
    if(need_rows <= state->str.line_capacity){
        return true;
    }

    int old_cap = state->str.line_capacity;
    int new_cap = (old_cap > 0) ? old_cap : 1;
    while(new_cap < need_rows){
        if(new_cap > INT_MAX / 2){
            new_cap = need_rows;
            break;
        }
        new_cap *= 2;
    }

    int  *line        = realloc(state->str.line, (size_t)new_cap * sizeof(int));
    long *line_offset = realloc(state->str.line_offset, (size_t)new_cap * sizeof(long));
    int  *line_cap    = realloc(state->str.line_cap, (size_t)new_cap * sizeof(int));
    // reallocは失敗しても元のポインタを解放しないため、成功した分だけでも
    // state側へ反映させておく(そうしないと古いポインタのままfreeし損ねる/二重解放になる)。
    if(line != NULL)        state->str.line        = line;
    if(line_offset != NULL) state->str.line_offset = line_offset;
    if(line_cap != NULL)    state->str.line_cap    = line_cap;
    if(line == NULL || line_offset == NULL || line_cap == NULL){
        return false;
    }

    // 新しく増えた行スロットは、総容量の末尾を指す空行として初期化しておく
    for(int i = old_cap; i < new_cap; i++){
        state->str.line[i]        = 0;
        state->str.line_cap[i]    = 0;
        state->str.line_offset[i] = state->str.total_capacity;
    }

    state->str.line_capacity = new_cap;
    return true;
}

// NUL終端のbuffをバイト単位で数え、タブをindent_rangeセルに展開した必要セル数の上限を返す。
// CR/LFは除外する。UTF-8入力を想定し、buffは非NULL、indent_rangeは正の値とする。
// 全角文字はUTF-8で複数バイトだが、バイト数のまま数えれば実セル数(1〜2)以上になり、確保不足を起こさない。
static size_t count_line_cells(const char *buff, int indent_range){
    size_t cells = 0;
    for(const char *p = buff; *p != '\0'; p++){
        if(*p == '\n' || *p == '\r'){
            continue;
        }
        cells += (*p == '\t') ? (size_t)indent_range : 1;
    }
    return cells;
}

// stateの現在のFILE位置から行頭オフセットと必要セル数の上限を収集する。読込上限はdefault_load_line_size行。
// 返り値: なし。有効なFILEと行頭配列が必要で、再走査時のFILE位置と行カウンタは呼び出し側で戻す。
void set_line_memory(struct editor_state *state){
    int max_line_size = state->settings_data->max_line_size;
    // 危険: max_line_sizeは設定JSONで上限を検査していない。
    // このVLAとload_all_lines()内のVLAが大きくなり、スタックを使い切る可能性がある。
    char dummy_buff[max_line_size];
    int indent_range = state->settings_data->indent_range;
    //ファイル内文字数カウンタ
    size_t file_data_total_str_size = 0;
    bool reached_eof = false;
    for(int i = 0; i < state->settings_data->default_load_line_size; i++){
        size_t dummy_buff_size = 0;
        // 行頭のファイル位置を先に控える。後でfseekして必要な行だけ読み直すための目次になる。
        state->file_data.file_line_start_num[state->file_data.file_line_start_num_counter++]
            = ftell(state->file_data.now_open_file);
        if(fgets(dummy_buff, max_line_size, state->file_data.now_open_file) == NULL){
            //i行目自体が存在しないので、保存対象はi行まで
            state->file_data.description_line_end = i;
            break;
        }
        dummy_buff_size += count_line_cells(dummy_buff, indent_range);

        // 1行がmax_line_sizeより長いとfgetsが途中で切れるため、改行に着くまで読み継いで1行分のセル数を数える。
        while(strlen(dummy_buff) == (size_t)(max_line_size - 1) && dummy_buff[max_line_size - 2] != '\n'){
            if(fgets(dummy_buff, max_line_size, state->file_data.now_open_file) == NULL){
                //i行目の途中でEOFに達したので、i行目までが保存対象
                state->file_data.description_line_end = i + 1;
                reached_eof = true;
                break;
            }
            else{
                dummy_buff_size += count_line_cells(dummy_buff, indent_range);
            }
        }
        file_data_total_str_size += dummy_buff_size;
        //内側のbreakは外側のforを抜けないため、ここで明示的に打ち切る
        if(reached_eof){
            break;
        }
    }
    state->file_data.file_total_str_size = (long)file_data_total_str_size;
    return;
}

// stateの記録済みload_start_lineへシークし、最大load_size回のfgetsで確保済みfile_str_dataへ読み込む。
// 返り値: なし。開始行が記録数以上ならexit(1)。1回の読込幅はwrite_area.wで、EOF・NULLの格納先で打ち切る。
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

// stateのファイル全文と、記録済み行数分の編集セル配列を読み直す。タブは空白、全角の後続セルは0にする。
// 返り値: なし。確保した本文はstateが所有し、ファイル読込失敗はエラー画面、編集バッファ確保失敗はexit(1)。
void load_all_lines(struct editor_state *state){
    long line_count = (state->file_data.file_line_start_num_counter < 1 )
        ?1:state->file_data.file_line_start_num_counter;

    char *file_all_str_data = read_file_all(state->file_data.now_open_path_name);
    if(file_all_str_data == NULL){
        editor_error_screen(state, "can not read file");
        return;
    }
    free(state->str.chr_file_all_str_data);
    state->str.chr_file_all_str_data = file_all_str_data;

    // 合計桁数はUTF-8のバイト数で数えた上限値なので、実際の表示桁数より必ず大きい。
    // これに行ごとの編集用余白を足したものをバッファ全体の容量にする。
    long total_cells = state->file_data.file_total_str_size;
    if(total_cells < 0){
        total_cells = 0;
    }
    if(line_count > (LONG_MAX - total_cells) / EDITOR_LINE_COL_SLACK){
        editor_error_screen(state, "file is too large");
        return;
    }
    long total_capacity = total_cells + line_count * EDITOR_LINE_COL_SLACK;

    if(!editor_alloc_text_buffer(state, (int)line_count, total_capacity)){
        editor_error_screen(state, "can not allocate file buffer");
        exit(1);
    }

    int max_line_size = state->settings_data->max_line_size;
    char buf[max_line_size];
    wchar_t wide_buf[max_line_size];

    long cur = 0;
    for(long i = 0; i < state->file_data.file_line_start_num_counter; i++){
        //この行が使える範囲は cur から total_capacity まで
        long room = state->str.total_capacity - cur;
        if(room <= 0){
            break;
        }
        state->str.line_offset[i] = cur;
        state->str.line[i]        = 0;
        state->str.line_cap[i]    = (room < EDITOR_LINE_COL_SLACK)
            ? (int)room : EDITOR_LINE_COL_SLACK;

        fseek(state->file_data.now_open_file, state->file_data.file_line_start_num[i], SEEK_SET);
        if(fgets(buf, max_line_size, state->file_data.now_open_file) == NULL){
            cur += state->str.line_cap[i];
            continue;
        }
        int len = strlen(buf);
        if(len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        if(len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

        memset(wide_buf, 0, sizeof(wide_buf));
        size_t converted = mbstowcs(wide_buf, buf, max_line_size - 1);
        if (converted == (size_t)-1) {
            cur += state->str.line_cap[i];
            continue;
        }

        wint_t *cells = &state->str.wint_line_str_data[cur];
        //この行に書ける上限。余白分を引いた残りが実データの上限になる。
        long line_room = room - EDITOR_LINE_COL_SLACK;
        if(line_room < 0){
            line_room = room;
        }

        int visible_width = 0;
        for(size_t j = 0; j < converted && visible_width < line_room; j++){
            if(wide_buf[j] == '\t'){
                for(int k = 0; k < state->settings_data->indent_range && visible_width < line_room; k++){
                    cells[visible_width] = L' ';
                    visible_width++;
                }
                continue;
            }

            // state->str.lineは文字数ではなく画面上の桁数として使う。
            // 日本語など2桁幅の文字でもカーソル位置と配列位置が合うように、
            // wint_line_str_dataもvisible_widthの位置へ配置する。
            int char_width = wcwidth(wide_buf[j]);
            if(char_width < 1){
                char_width = 1;
            }
            if(visible_width + char_width > line_room){
                break;
            }

            cells[visible_width] = wide_buf[j];
            visible_width += char_width;
        }

        state->str.line[i]     = visible_width;
        //実データ + 余白をこの行の容量とし、次の行の開始位置を決める
        state->str.line_cap[i] = visible_width + EDITOR_LINE_COL_SLACK;
        if(state->str.line_cap[i] > room){
            state->str.line_cap[i] = (int)room;
        }
        cur += state->str.line_cap[i];
    }

    //読み込み対象外だった行にも開始位置と容量を割り当てておく
    for(long i = state->file_data.file_line_start_num_counter; i < line_count; i++){
        long room = state->str.total_capacity - cur;
        if(room <= 0){
            state->str.line_offset[i] = (cur > 0) ? cur - 1 : 0;
            state->str.line_cap[i]    = 0;
            continue;
        }
        state->str.line_offset[i] = cur;
        state->str.line_cap[i]    = (room < EDITOR_LINE_COL_SLACK)
            ? (int)room : EDITOR_LINE_COL_SLACK;
        cur += state->str.line_cap[i];
    }
}

// stateの編集セルを現在のロケールでマルチバイト化し、各行末に改行を付ける。UTF-8利用時は対応ロケールが必要。
// 返り値: 呼び出し側がfreeするNUL終端文字列、状態不正・容量超過・確保・変換失敗はNULL。値0のセルは出力しない。
char *editor_buffer_to_utf8(struct editor_state *state)
{
    int line_count;
    size_t capacity = 1;
    size_t pos = 0;
    char *text;

    if(state == NULL || state->str.wint_line_str_data == NULL ||
       state->str.line == NULL || state->str.line_offset == NULL ||
       state->str.line_cap == NULL){
        return NULL;
    }

    line_count = state->file_data.description_line_end;
    if(line_count < 0 || line_count > state->str.line_capacity){
        return NULL;
    }

    for(int line = 0; line < line_count; line++){
        int line_len = editor_line_len(state, line);
        if(line_len < 0 ||
           (size_t)line_len > (SIZE_MAX - capacity - 1) / MB_CUR_MAX){
            return NULL;
        }
        capacity += (size_t)line_len * MB_CUR_MAX + 1;
    }

    text = malloc(capacity);
    if(text == NULL){
        return NULL;
    }

    for(int line = 0; line < line_count; line++){
        int line_len = editor_line_len(state, line);
        wint_t *cells = editor_line_cells(state, line);
        mbstate_t conversion_state = {0};

        if(cells == NULL){
            text[pos++] = '\n';
            continue;
        }

        for(int col = 0; col < line_len; col++){
            wint_t cell = cells[col];
            if(cell == 0)continue;
            
            size_t bytes = wcrtomb(text + pos, (wchar_t)cell, &conversion_state);
            if(bytes == (size_t)-1){
                free(text);
                return NULL;
            }
            pos += bytes;
        }
        text[pos++] = '\n';
    }

    text[pos] = '\0';
    return text;
}

// stateの本文を保存先と同じディレクトリの一時ファイルへ書き、成功時だけ置き換えてFILEを更新する。
// 返り値: なし。保存先未指定は設定に応じて作成確認かエラー画面へ、保存失敗はエラー画面へ遷移する。NULLは何もしない。
void save_file(struct editor_state *state){
    if(state == NULL)return;

    const char *now_open_path_name = state->file_data.now_open_path_name;
    if(now_open_path_name[0] == '\0'){
        if(state->settings_data->ask_make_file){
            editor_set_screen_state(state, ask_make_file_mode);
            return;
        }
        else{
            editor_error_screen(state, "no file opened");
        }
        return;
    }

    char *text = editor_buffer_to_utf8(state);
    if(text == NULL){
        editor_error_screen(state, "can not convert file");
        return;
    }

    // 保存は「同じディレクトリに一時ファイルを書いてrename」で置き換える。
    // 書込み途中で失敗・停電しても元ファイルが壊れず、同一ディレクトリなのでrenameがアトミックになる。
    struct stat st;
    char resolved_path[PATH_MAX];
    const char *save_path = now_open_path_name;
    bool existed = (stat(now_open_path_name, &st) == 0);
    if(existed){
        if(!S_ISREG(st.st_mode) || realpath(now_open_path_name, resolved_path) == NULL){
            free(text);
            editor_error_screen(state, "can not save file");
            return;
        }
        // シンボリックリンクはrealpathで実体へ解決する。そのままrenameするとリンク自体が通常ファイルに置き換わる。
        save_path = resolved_path;
    }
    else{
        // 新規作成時は、ENOENT以外の失敗や壊れたシンボリックリンクの上書きを避けて中止する。
        struct stat link_st;
        if(errno != ENOENT || lstat(now_open_path_name, &link_st) == 0 || errno != ENOENT){
            free(text);
            editor_error_screen(state, "can not save file");
            return;
        }
    }

    char tmp_path[PATH_MAX + sizeof("/.my_txt_editor_XXXXXX")];
    const char *last_slash = strrchr(save_path, '/');
    int path_len = last_slash == NULL
        ? snprintf(tmp_path, sizeof(tmp_path), "./.my_txt_editor_XXXXXX")
        : snprintf(tmp_path, sizeof(tmp_path), "%.*s/.my_txt_editor_XXXXXX",
                   (int)(last_slash - save_path), save_path);

    if(path_len < 0 || (size_t)path_len >= sizeof(tmp_path)){
        free(text);
        editor_error_screen(state, "path too long");
        return;
    }

    int fd = mkstemp(tmp_path);
    if(fd < 0){
        free(text);
        editor_error_screen(state, "can not save file");
        return;
    }
    FILE *file = fdopen(fd, "w");
    if(file == NULL){
        close(fd);
        unlink(tmp_path);
        free(text);
        editor_error_screen(state, "can not save file");
        return;
    }

    size_t text_len = strlen(text);
    bool write_ok = fwrite(text, 1, text_len, file) == text_len;
    free(text);
    // mkstempは0600で作るため、既存ファイルの権限(実行ビット等)を引き継ぐ。
    if(write_ok && existed && fchmod(fd, st.st_mode & 0777) != 0)write_ok = false;
    if(write_ok && fflush(file) != 0)write_ok = false;
    if(write_ok && fsync(fd) != 0)write_ok = false;
    if(fclose(file) != 0)write_ok = false;
    if(!write_ok){
        unlink(tmp_path);
        editor_error_screen(state, "can not write file");
        return;
    }

    FILE *replacement = fopen(tmp_path, "r");
    if(replacement == NULL || rename(tmp_path, save_path) != 0){
        if(replacement != NULL)fclose(replacement);
        unlink(tmp_path);
        editor_error_screen(state, "can not save file");
        return;
    }

    // renameで中身(inode)が入れ替わったため、開き直したFILEに差し替えて行頭位置も取り直す。
    if(state->file_data.now_open_file != NULL)fclose(state->file_data.now_open_file);
    state->file_data.now_open_file = replacement;
    state->file_data.file_line_start_num_counter = 0;
    set_line_memory(state);
}

// ファイル読み込み後に行開始位置と編集バッファを作り直し、表示開始行とカーソル行を先頭へ戻す。
// 引数: state=ファイル読み込み後に初期化するエディタ状態。 返り値: なし。
void load_screen_size(struct editor_state *state){
    state->file_data.file_line_start_num_counter = 0;
    set_line_memory(state);
    load_all_lines(state);
    state->scr.scr_start_num = 0;
    state->cursor.file_pos = (struct pos){0,0};
}

// エディタ設定へコンパイル時の既定値を入れる。
// 引数: settings_data=初期化する設定構造体。 返り値: なし。
void load_default_editor_settings(struct editor_settings *settings_data){
    settings_data->default_load_line_size       = DEFAULT_LOAD_LINE_SIZE;
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
    settings_data->lsp.lsp_launch_startup_editor= DEFAULT_LSP_PROCESS_LAUNCH_STARTUP_EDITOR;
    settings_data->lsp.lsp_epoll_timeout_ms     = DEFAULT_EPOLL_TIME_OUT_MS;
    settings_data->lsp.lsp_use                  = DEFAULT_LSP_USE;
    settings_data->use_icon                     = DEFAULT_USE_ICON;
    settings_data->built_in_syntax              = DEFAULT_BUILT_IN_SYNTAX;
    settings_data->auto_complete_settings_data.auto_complete_enabled       = DEFAULT_AUTO_COMPLETE;
    settings_data->auto_complete_settings_data.auto_complete_window_enable = DEFAULT_AUTO_COMPLETE_WINDOW;
    settings_data->auto_complete_settings_data.auto_complete_window_size =
        (struct pos){DEFAULT_AUTO_COMPLETE_WINDOW_WIDTH, DEFAULT_AUTO_COMPLETE_WINDOW_HEIGHT};
    settings_data->auto_complete_settings_data.auto_complete_position_mode = DEFAULT_AUTO_COMPLETE_POSITION_MODE;
    settings_data->settings_lang                = DEFAULT_SETTINGS_LANGUAGE;
    settings_data->key_log_settings.use_key_log = DEFAULT_USE_KEY_LOG;
    settings_data->key_log_settings.key_log_buffer_size = DEFAULT_KEY_LOG_BUFFER_SIZE;
}

// 現在の選択行をprevious_lineに保存し、新しい選択行を設定する。
// 引数: file_select_line=更新対象の選択行状態、line=新しい行番号。NULLは指定できない。 返り値: なし。
void file_select_line_update(struct file_select_line *file_select_line,int line){
    file_select_line->previous_line = file_select_line->now_line;
    file_select_line->now_line = line;
}


// flagsがsetならpathを借用して内部に保持する。NULLは何もしない。
// 返り値: なし。getは値渡しのローカル変数だけを書き換えるため、呼び出し側へパスを返さない。
void input_mode_tmp_path(char *path,enum flags flags){
    if(path == NULL)return;
    static char *tmp_path = NULL;

    if(flags == set){
        tmp_path = path;
    }
    else if(flags == get){
        path = tmp_path;
    }
}


// flags=setでpathのパスを内部複製し、getでpathがあれば借用パス・名前・種類を書き戻す。
// 返り値: 次回呼出しまで有効なワイド文字列、失敗NULL。返値は解放不可、getのpath_nameは次回setで無効になる。
const wchar_t *now_open_path_name(struct dir_table *path,enum flags flags){
    if((path == NULL || path->path_name == NULL) && flags == set)return NULL;

    // ファイルブラウザで入力・移動中のパス。
    // state->file_data.now_open_path_nameの編集中ファイルパスとは別に保持する。
    static struct dir_table now_open_path;
    static wchar_t wide_path[PATH_MAX]; // now_open_path.path_nameを変換した返値用バッファ。
    if(flags == set){
        size_t path_size = strlen(path->path_name);
        const char *name_end = path->path_name + path_size;
        while(name_end > path->path_name + 1 && name_end[-1] == '/'){
            name_end--;
        }
        const char *name_start = name_end;
        while(name_start > path->path_name && name_start[-1] != '/'){
            name_start--;
        }
        size_t name_size = (size_t)(name_end - name_start);
        if(name_size == 0 && path->path_name[0] == '/'){
            name_start = path->path_name;
            name_size = 1;
        }

        char *saved_path = malloc(path_size + 1);
        if(saved_path == NULL || name_size >= sizeof(now_open_path.d_name)){
            free(saved_path);
            return NULL;
        }
        memcpy(saved_path,path->path_name,path_size + 1);

        free(now_open_path.path_name);
        now_open_path.path_name = saved_path;
        memcpy(now_open_path.d_name,name_start,name_size);
        now_open_path.d_name[name_size] = '\0';
        now_open_path.d_type = DT_UNKNOWN;

        struct stat st;
        if(stat(now_open_path.path_name,&st) == 0){
            if(S_ISDIR(st.st_mode)){
                now_open_path.d_type = DT_DIR;
            }
            else if(S_ISREG(st.st_mode)){
                now_open_path.d_type = DT_REG;
            }
        }
    }
    else if(flags == get && path != NULL){
        path->path_name = now_open_path.path_name;
        memcpy(path->d_name,now_open_path.d_name,sizeof(path->d_name));
        path->d_type = now_open_path.d_type;
    }

    const char *path_name = (now_open_path.path_name != NULL)
        ? now_open_path.path_name : "";
    size_t converted = mbstowcs(wide_path,path_name,PATH_MAX - 1);
    if(converted == (size_t)-1){
        wide_path[0] = L'\0';
        return NULL;
    }
    wide_path[converted] = L'\0';
    return wide_path;
}

// 現在パスの末尾名を含むディレクトリエントリを最大size件収集する。
// 引数: dir_table=結果の格納先配列、size=配列の要素数。0以下は無効。 返り値: 格納した件数。引数不正、文字変換、ディレクトリオープン失敗時は-1。
int check_dir_mem(struct dir_table *dir_table,int size){
    if(dir_table == NULL || size <= 0)return -1;
    memset(dir_table,0,(size_t)size * sizeof(*dir_table));

    const wchar_t *path = now_open_path_name(NULL,get);
    if(path == NULL){
        error_log("can not get now open path name");
        return -1;
    }
    char char_path[PATH_MAX];
    size_t converted = wcstombs(char_path,path,sizeof(char_path) - 1);
    if(converted == (size_t)-1)return -1;
    char_path[converted] = '\0';

    char dir_name[PATH_MAX] = ".";
    const char *now_dir_mem_name = char_path;
    char *last_slash = strrchr(char_path,'/');
    if(last_slash != NULL){
        now_dir_mem_name = last_slash + 1;
        if(last_slash == char_path){
            dir_name[0] = '/';
            dir_name[1] = '\0';
        }
        else{
            size_t dir_name_size = (size_t)(last_slash - char_path);
            memcpy(dir_name,char_path,dir_name_size);
            dir_name[dir_name_size] = '\0';
        }
    }

    DIR *dir = opendir(dir_name);
    if(dir == NULL)return -1;
    struct dirent *dirent_dir = NULL;
    
    int dir_mem_counter = 0;
    while((dirent_dir = readdir(dir)) != NULL){
        if(dir_mem_counter >= size)break;
        size_t dirent_name_size = strlen(dirent_dir->d_name);
        if(strstr(dirent_dir->d_name,now_dir_mem_name) != NULL){
            if(dirent_name_size >= sizeof(dir_table[dir_mem_counter].d_name))continue;
            memcpy(dir_table[dir_mem_counter].d_name,
                   dirent_dir->d_name,dirent_name_size + 1);
            dir_table[dir_mem_counter].d_type = dirent_dir->d_type;
            dir_mem_counter++;
        }
    }
    closedir(dir);
    return dir_mem_counter;
}

// パスの実体をstat()で調べ、ファイルまたはディレクトリへ分類する。
// 引数: path=判定するNUL終端パス。 返り値: 通常ファイルならfile、ディレクトリならfolder、その他はunkown、NULLならerror。
enum select_state get_path_state(const char *path){
    if(path == NULL)return error;
    struct stat stat_state = {0};
    int stat_rt = stat(path,&stat_state);
    if(stat_rt == -1){
        char *error_msg = strerror(errno);
        if(strlen(error_msg) + 1 > ERROR_MSG_SIZE_MAX){
            error_msg[ERROR_MSG_SIZE_MAX - 1] = '\0';
        }
        error_log_write(error_msg);
        error_log_write((char*)path);
    }
    if(stat_state.st_mode & S_IFREG){
        return file;
    }
    else if(stat_state.st_mode & S_IFDIR){
        return folder;
    }
    else return unkown;
}

// パス入力欄の完成済みパスを開き、種類に応じて画面状態を更新する。
// 引数: state=読込先の編集状態、ctx=ファイルブラウザと描画状態を持つ入力context。 返り値: 成功時0、引数不正・判定失敗時-1。完成パスが無い場合はtrue。
int now_input_path_open(struct editor_state *state,struct editor_input_context *ctx){
    if(state == NULL || ctx == NULL)return -1;
    struct dir_table dir_info = {0};
    now_open_path_name(&dir_info,get);
    if(dir_info.path_name == NULL)return true;

    struct file_browse_select_state select_state;
    load_file(state,NULL,0,dir_info.path_name,&select_state);
    if(select_state.select_state == unkown || select_state.select_state == error)return -1;
    if(select_state.select_state == folder){
        char *last_slash_ptr = strrchr(dir_info.path_name,'/');
        if(last_slash_ptr == NULL){

        }
        else{
            *(last_slash_ptr + 1) = '\0';
            load_dir_table(
                state,
                &state->file_browse.dir_name_table,
                &state->file_browse.dir_name_table_rows,
                dir_info.path_name,
                0,
                &state->file_browse.dir_num,
                &state->file_browse.dir_name_table_num
            );
            memcpy(
                state->file_browse.path_name,
                dir_info.path_name,
                sizeof(char) * ((last_slash_ptr+2) - &dir_info.path_name[0]));
        }
    }

    if(select_state.select_state == file){
        load_screen_size(state);
        editor_set_cursor(state,0,0);
        restore_edit_screen(state);
        set_file_browse_path_input_mode(&state->file_browse,false);
        if(state->settings_data->built_in_syntax){
            set_syntax_data(&ctx->syntax_data,ctx);
        }
    }
    return 0;
}


// ファイルブラウザの表示開始位置を保存または取得する。
// 引数: start_num=set時に保存する位置、flags=getまたはset。 返り値: get時は保存値、それ以外は-1。set時も保存後に-1を返す。
int file_browser_show_mem_start_num(int start_num,enum flags flags){
    static int static_start_num = 0;
    if(flags == get)return static_start_num;
    else if(flags == set)static_start_num = start_num;
    return -1;
}
