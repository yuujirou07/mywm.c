#include <dirent.h>
#include <limits.h>
#include <linux/limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
        
    }
    else if(state->file_select_line_data.now_line >= state->dir_num){
        file_select_line_update(&state->file_select_line_data,state->dir_num - 1);
    }
}

// load_file(): ファイルブラウザで選択中の名前を取り出し、Cファイルなら読み込み用に開く。
// 開けない場合や対象外の拡張子ならエラー画面へ切り替える。
// 引数: state=選択行とファイル状態、table=固定幅のファイル名一覧、path_name=現在ディレクトリ、select_state=選択結果の書き込み先。
// 返り値: なし。成功時はstate->file_data.now_open_fileにFILE*を保存する。
void load_file(struct editor_state *state, char *table,char *path_name,struct file_browse_select_state *select_state){
    select_state->select_name[0] = '\0';
    if(state->file_select_line_data.now_line < 0 || state->file_select_line_data.now_line  >= state->dir_num){
        select_state->select_state = error;
        return;
    }

    int idx = state->file_select_line_data.now_line  *state->file_browser_area.w;
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
    // 危険: 既にnow_open_fileがある場合も閉じずに上書きするため、
    // ファイルを開き直すたびにFILEとファイルディスクリプタが残る。
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

// editor_free_text_buffer(): 編集バッファと行情報配列をまとめて解放する。
// 引数: state=解放対象のエディタ状態。
// 返り値: なし。
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

// editor_alloc_text_buffer(): 合計容量total_capacity分の連続バッファと、
// line_count行分の行情報配列(長さ・開始位置・行容量)を確保する。
// line_offsetとline_capは0のままなので、呼び出し側がレイアウトを決めて埋める。
// 引数: state=確保先、line_count=扱う行数、total_capacity=バッファ全体の要素数。
// 返り値: 確保できたらtrue。失敗時は何も確保していない状態へ戻す。
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

// editor_ensure_line_cap(): 指定行がneed列を保持できるよう、必要なら容量を伸ばす。
// 連続レイアウトなので、伸ばした行より後ろのデータをまとめて後方へずらし、
// 後続行のline_offsetへ差分を加算する。倍々で伸ばして再配置の頻度を抑える。
// 引数: state=編集バッファ、line=対象論理行、need=必要な列数。
// 返り値: need列を確保できたらtrue。上限超過やrealloc失敗ならfalse(バッファは無変更)。
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

// editor_ensure_row_capacity(): 行情報配列(line/line_offset/line_cap)がneed_rows行分の
// スロットを持つよう、必要なら伸長する。editor_ensure_line_cap()の行方向版にあたる。
// 引数: state=編集バッファ、need_rows=最低限確保したい行スロット数。
// 返り値: need_rows行を確保できたらtrue。realloc失敗時はfalse。
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
    long *line_offset  = realloc(state->str.line_offset, (size_t)new_cap * sizeof(long));
    int  *line_cap     = realloc(state->str.line_cap, (size_t)new_cap * sizeof(int));
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

// count_line_cells(): 1行分の文字列が画面上で占める桁数の上限を数える。
// UTF-8ではバイト数が表示桁数以上になるため、バイト数を桁数の上限として使える。
// タブは展開後の幅で数える。改行文字はバッファへ入れないので除く。
// 引数: buff='\0'終端の行データ、indent_range=タブ1個の展開幅。
// 返り値: 桁数の上限。
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

// set_line_memory(): ファイル各行の開始位置(ftell)を保存し、後で任意行へfseekできるようにする。
// 併せて全行の合計桁数を数え、file_total_str_sizeへ書き込む。
// 引数: state=開いているFILE*と行開始位置配列。
// 返り値: なし。
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
        state->file_data.file_line_start_num[state->file_data.file_line_start_num_counter++]
            = ftell(state->file_data.now_open_file);
        if(fgets(dummy_buff, max_line_size, state->file_data.now_open_file) == NULL){
            //i行目自体が存在しないので、保存対象はi行まで
            state->file_data.description_line_end = i;
            break;
        }
        dummy_buff_size += count_line_cells(dummy_buff, indent_range);

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

// get_last_visible_file_line(): 画面上で文字がある最後の行番号を返す。
// 引数: state=読み込み済み行の表示幅を持つエディタ状態。
// 返り値: 1始まりの最終行番号。文字がない、または状態が不正なら0。
long get_last_visible_file_line(struct editor_state *state){
    if(state == NULL || state->str.line == NULL){
        return 0;
    }

    long line_count = state->file_data.file_line_start_num_counter;
    if(line_count > state->str.line_capacity){
        line_count = state->str.line_capacity;
    }

    for(long i = line_count - 1; i >= 0; i--){
        if(state->str.line[i] > 0){
            return i + 1;
        }
    }
    return 0;
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
// バッファはset_line_memory()が数えた合計桁数(+行ごとの余白)分だけ確保し、
// 各行を先頭から詰めながらline_offset/line_capを確定させる。
// 画面幅には一切依存しないため、リサイズしても再読み込みは不要。
// 引数: state=開いているFILE*・行開始位置・編集バッファ。
// 返り値: なし。
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


//リサイズなどで表示領域が変わった際の表示文字列を計算する
int update_visiable_line_str(struct editor_state *state,long line_num,char *str){
    if(state == NULL)return -1;
    //バッファは画面幅と独立しているので、可視幅で切り出すだけでよい
    long line_size = editor_line_len(state, (int)line_num);
    long view_cols = editor_view_cols(state);
    if(line_size > view_cols){
        line_size = view_cols;
    }

    return 0;
}

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
            if(cell == 0){
                continue;
            }

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

// load_view_from_cursor(): 現在の論理カーソル行から表示用の行データを読み込む。
// 引数: state=現在の論理カーソル行とファイル読み込み状態。
// 返り値: なし。
void load_view_from_cursor(struct editor_state *state){
    load_string_data(state, state->mouse.now_mouce_line, state->settings_data->load_buffer_lines);
}

// save_file(): 現在の編集バッファを開いているファイルパスへ書き戻す。
// wint_line_str_dataは画面セル位置に合わせているため、0のセルは書かずに飛ばす。
// 引数: state=保存先パスと編集バッファを持つエディタ状態。
// 返り値: なし。
void save_file(struct editor_state *state){
    if(state == NULL || state->file_data.now_open_path_name[0] == '\0'){
        if(state->settings_data->ask_make_file){
            editor_set_screen_state(state, ask_make_file_mode);
            return;
        }
        else{
            editor_error_screen(state, "no file opened");
        }
        return;
    }

    // 危険: "w"で元ファイルを先に切り詰めてから書くため、
    // 途中の変換・書き込み失敗やプロセス停止で元データを復元できない。
    FILE *file = fopen(state->file_data.now_open_path_name, "w");
    if(file == NULL){
        editor_error_screen(state, "can not save file");
        return;
    }

    int line_count = state->file_data.description_line_end;
    for(int line = 0; line < line_count; line++){
        //画面幅では丸めない。画面外にあった桁もバッファ上に残っているので保存する。
        int max_col = editor_line_len(state, line);
        wint_t *cells = editor_line_cells(state, line);
        if(cells == NULL){
            fputwc('\n', file);
            continue;
        }


        
        for(int col = 0; col < max_col; col++){
            wint_t cell = cells[col];
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
    settings_data->lsp.lsp_lanch_startup_editor = DEFAULT_LSP_PROCESS_LANCH_STARTUP_EDITOR;
    settings_data->lsp.lsp_epoll_timeout_ms     = DEFAULT_EPOLL_TIME_OUT_MS;
    settings_data->lsp.lsp_use                  = DEFAULT_LSP_USE;
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

    cJSON *lsp = cJSON_GetObjectItemCaseSensitive(json_data, "lsp");
    if(cJSON_IsObject(lsp)){
        cJSON *launch_startup_editor =
            cJSON_GetObjectItemCaseSensitive(lsp, "launch_startup_editor");
        cJSON *epoll_timeout_ms =
            cJSON_GetObjectItemCaseSensitive(lsp, "epoll_timeout_ms");

        if(cJSON_IsBool(launch_startup_editor)){
            settings_data->lsp.lsp_lanch_startup_editor =
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

void file_select_line_update(struct file_select_line *file_select_line,int line){
    file_select_line->previous_line = file_select_line->now_line;
    file_select_line->now_line = line;
}
