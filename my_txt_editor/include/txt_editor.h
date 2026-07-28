#ifndef TXT_EDITOR_H
#define TXT_EDITOR_H

#include <ncurses.h>
#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include "ascii_art_comb.h"
#include"default_settings.h"
#include"lsp_src/language_server_communication.h"

#define my_txt_editor_var 0.0
#define new_file 0
#define quit 1
#define select_folder 3
#define none 4
#define startuptime_log_file_argument_num 1
#define FDS_N 4
#define DRAW_BOX_REQUEST_MAX 64
#define box_retention_max 64
#define resize_request 5
#define screen_state_log_storage 256
// 各行へ前もって足しておく余白列数。ここに収まる入力は再配置なしで処理できる。
#define EDITOR_LINE_COL_SLACK 16
// 1行が確保できる列数の絶対上限。これを超える伸長要求は拒否する。
#define EDITOR_LINE_COL_MAX 65536

// update_screen()で再描画する領域を指定するビットフラグ。
enum render_flags {
    RENDER_NONE       = 0,       // 再描画要求なし。
    RENDER_LINE_STATUS     = 1 << 0,  // 現在行表示を更新する。
    RENDER_STATUS_BAR_LINE = 1 << 1,  // ステータスバーの区切り線を更新する。
    RENDER_LINE  = 1 << 2,      // 編集領域左の縦線を更新する。
    RENDER_STATUS_BAR = 1 << 3, // ステータスバーの内容を更新する。
    RENDER_SELECT_DIR_SCENE_COLOR = 1 << 4, // ファイル選択行の反転表示を更新する。
    RENDER_EDIT_SCREEN_BASE = 1<<5, // 編集画面の枠や基本線を更新する。
    RENDER_FILE_DATA = 1<<6,    // 編集バッファの表示内容を更新する。
    RENDER_FILE_BROWSE = 1 << 7, // ファイルブラウザ全体を更新する。
    RENDER_BOX        = 1 << 8, // draw_box_dataに積まれた枠を描画する。
    RENDER_CLEAR_BOX = 1 << 9,  // clear_box_dataに積まれた範囲を消す。
    RENDER_ALL        = 1 << 10, // 画面全体更新用の予約フラグ。
    RENDER_LINE_JUMP = 1 << 11, // 行ジャンプ入力欄を更新する。
    RENDER_MAKE_FILE = 1 << 12, // 新規ファイル作成ダイアログを更新する。
};


#define CTRL(x) ((x) & 0x1f)// 0x1fはCtrl

typedef int (*Start_Menu)(int screen_w, int screen_h, struct ascii_data *ascii_data,
                          const struct timespec *startup_start_time,
                          const char *startup_log_path);




// ステータスバーを画面上端か下端のどちらに出すか。
enum status_bar_side{
    top,
    bottom,
};

// 行ジャンプモード中に入力された行番号を保持する。
struct jump_mode{
    char jump_line_num[JUMP_LINE_NUM_DIGITS + 1]; // 入力中のジャンプ先行番号文字列。
    int  jump_line_num_counter; // jump_line_numに入っている文字数。
};

// 開いているファイルと、読み込み済みテキストの行情報。
struct file_data{
    FILE*   now_open_file; // 現在開いているFILE。未オープンならNULL。
    char**  file_str_data; // ファイルから読み込んだ各行の文字列配列。
    char    now_open_path_name[DEFAULT_PATH_NAME_MAX_SIZE]; // 現在開いているファイルパス。
    long*   file_line_start_num; // ファイル内で各行が始まるバイト位置。
    long    file_line_start_num_counter; // file_line_start_numに登録済みの行数。
    long    description_line_end; // 保存対象として扱う論理行数。
    long    file_str_line_end; // 可視文字がある最終行番号。
    int     file_line_n; // 画面に読み込むファイル行の作業用番号。
    long    file_total_str_size;//ファイル内の合計文字数
    bool    is_open_file; // ファイルを開いて編集しているならtrue。
};

// ファイルブラウザで選択中の項目種別。
enum select_state{
    file,
    folder,
    error,
};


// 現在表示している画面・入力モード。
enum now_screen_state{
    edit_screen, // 通常の編集画面。
    file_browse_screen, // ファイルブラウザ画面。
    start_menu_file_browse_screen, // start menuから開いたファイルブラウザ。
    start_menu_screen, // start menu pluginの画面。
    error_screen, // エラー表示画面。
    line_jump_mode, // 行ジャンプ番号入力中。
    ask_make_file_mode, // 新規ファイル作成確認中。
};

// 新規ファイル作成ダイアログの入力状態。
struct make_file_mode_status{
    bool is_input_scene; // trueならファイル名入力欄を編集中。
    char new_file_name[DEFAULT_PATH_NAME_MAX_SIZE]; // 入力された新規ファイル名。
    int new_file_name_counter; // new_file_nameに入っている文字数。
};

// ファイルブラウザで決定された項目名と種別。
struct file_browse_select_state{
    enum select_state select_state; // 選択項目がファイル・フォルダ・エラーのどれか。
    char select_name[NAME_MAX + 1]; // 選択された項目名。
};

// ncurses画面上の座標。
struct pos {
    int x; // 横方向の座標。
    int y; // 縦方向の座標。
};

// ファイルブラウザで反転表示する行の現在値と直前値。
struct file_select_line {
    int now_line; // 現在選択中の行。
    int previous_line; // 前回選択していた行。
};

// 画面上の矩形領域。
struct box {
    struct pos pos; // 左上座標。
    int w; // 幅。
    int h; // 高さ。
};

// 文字入力・描画が許可される編集領域。
struct write_possible_area {
    int x_start; // 入力可能範囲の左端。
    int y_start; // 入力可能範囲の上端。
    int x_end; // 入力可能範囲の右端。
    int y_end; // 入力可能範囲の下端。
    int w; // 入力可能範囲の幅。
    int h; // 入力可能範囲の高さ。
};

// 画面サイズ・カーソル・スクロール開始行。
struct scr_data {
    struct pos cursor_pos; // 保存しておくカーソル座標。
    struct pos scr_size; // 現在の画面サイズ。
    int scr_start_num; // 画面先頭に表示している論理行番号。
};

// 編集バッファ本体と、行ごとの文字数・容量情報。
// wint_line_str_dataは「行数×画面幅」の矩形ではなく、行ごとに必要な分だけを
// 連続領域へ詰めた可変長レイアウトで持つ。行の先頭位置はline_offsetが持ち、
// その行に確保済みの列数はline_capが持つ。画面幅とは完全に独立している。
struct str_data {
    wint_t *wint_line_str_data; // 編集中テキストを保持するワイド文字バッファ。
    char   *chr_file_all_str_data; // ファイル全体をUTF-8文字列化するときの作業バッファ。
    int    *line; // 各論理行の表示桁数。
    long   *line_offset; // 各論理行がwint_line_str_data内で始まるインデックス。
    int    *line_cap; // 各論理行に確保済みの列数。
    long    total_capacity; // wint_line_str_data全体の要素数。
    int     line_capacity; // 扱える最大行数。
};

// マウス位置と、現在編集対象にしている論理行。
struct mouse_data {
    int now_mouce_line; // 現在編集・選択対象にしている論理行番号。
    struct pos scr_abs_now_pos; // 画面上の現在マウス絶対座標。
};

// 後で消去する矩形領域を一時的に保持する。
struct clear_box_data{
    struct box clear_box[box_retention_max]; // 消去予定の矩形配列。
    int clear_box_counter; // clear_boxに積まれている数。
};

struct screen_state_log{
    enum now_screen_state screen_state_log[screen_state_log_storage]; // 画面遷移履歴。
    int screen_state_log_counter; // 記録済みの遷移数。
};




// エディタ全体で共有する実行時状態。
struct editor_state {
    struct editor_settings    *settings_data; // 設定ファイルとデフォルト値から作った設定。
    struct scr_data            scr; // 画面サイズ・カーソル・スクロール状態。
    struct str_data            str; // 編集バッファと行長情報。
    struct mouse_data          mouse; // マウス位置と現在の論理行。
    struct write_possible_area write_area; // 編集可能な画面領域。
    struct make_file_mode_status make_file_mode_status; // 新規ファイル作成ダイアログ状態。
    struct box                 file_browser_area; // ファイル一覧を描画する内側領域。
    struct box                 draw_box_data[DRAW_BOX_REQUEST_MAX]; // 次回描画する枠のキュー。
    struct box                *file_browser_box; // ファイルブラウザ外枠への参照。
    struct box                *status_bar; // ステータスバー領域への参照。
    struct box                 ask_make_file_box; // 新規ファイル作成ダイアログ外枠。
    struct box                 write_file_name_area; // 新規ファイル名入力欄。
    struct file_data           file_data; // 現在開いているファイルと行情報。
    struct jump_mode           jump_mode_data; // 行ジャンプ入力状態。
    struct file_select_line    file_select_line_data; // ファイルブラウザの選択行状態。
    struct clear_box_data      clear_box_data; // 次回消去する矩形領域。
    struct screen_state_log    screen_log; // 現在状態を末尾に持つ画面遷移履歴。
    int                        dir_num; // ファイルブラウザに表示中の項目数。
    int                        render_flags; // update_screen()へ渡す再描画要求。
    int                        draw_box_count; // draw_box_dataに積まれている数。
    bool                       is_cur_show; // カーソル表示中ならtrue。

};

static inline enum now_screen_state editor_get_screen_state(struct editor_state *state){
    if(state->screen_log.screen_state_log_counter <= 0){
        return edit_screen;
    }

    return state->screen_log.screen_state_log[
        state->screen_log.screen_state_log_counter - 1];
}

static inline enum now_screen_state editor_get_previous_screen_state(struct editor_state *state){
    if(state->screen_log.screen_state_log_counter < 2){
        return edit_screen;
    }

    return state->screen_log.screen_state_log[
        state->screen_log.screen_state_log_counter - 2];
}

static inline void editor_set_screen_state(struct editor_state *state,
                                           enum now_screen_state next_state){
    struct screen_state_log *log = &state->screen_log;

    if(log->screen_state_log_counter > 0 &&
       log->screen_state_log[log->screen_state_log_counter - 1] == next_state){
        return;
    }
    if(log->screen_state_log_counter >= screen_state_log_storage){
        for(int i = 1; i < screen_state_log_storage; i++){
            log->screen_state_log[i - 1] = log->screen_state_log[i];
        }
        log->screen_state_log_counter = screen_state_log_storage - 1;
    }

    log->screen_state_log[log->screen_state_log_counter++] = next_state;
}




struct editor_input_context {
    WINDOW *win;                 // 入力処理と描画で使うncursesウィンドウ。
    MEVENT *mouse_event;         // KEY_MOUSE時にgetmouse()へ渡すイベント格納先。
    struct editor_state *state;  // 画面状態・カーソル・ファイル情報をまとめた本体状態。
    struct box file_browse_box;  // ファイルブラウザ外枠の位置とサイズ。
    char *dir_name_table;        // ファイルブラウザに表示する固定幅のディレクトリ一覧。
    int dir_name_table_size;     // dir_name_tableの確保済みバイト数。
    char *path_name;             // ファイルブラウザが現在開いているディレクトリパス。
    struct pos line_start_pos;   // 編集領域左の区切り線の開始座標。
    struct pos line_end_pos;     // 編集領域左の区切り線の終了座標。
    int screen_center_y;         // 確認ダイアログを縦方向中央寄せするときの基準。
    struct pos screen_center_pos;// 確認ダイアログを中央寄せするときの基準座標。
    bool *open_start_menu;       // file browserからstart menuへ戻る要求を書き込む先。
    bool has_start_menu;         // start menu pluginがロード済みならtrue。
    Start_Menu start_menu;       // start menu pluginの関数ポインタ。
    struct ascii_data *ascii_data; // start menuへ渡すASCIIアートデータ。
    const struct timespec *startup_start_time; // 起動時間計測の開始時刻。
    const char *startup_log_path; // 起動時間ログの出力先。不要ならNULL。
    struct lsp_process *lsp_data; // LSPプロセスと通信状態。
};


// editor_line_limit(): 編集対象として扱える最大行数を返す。
// 引数: state=行バッファ容量と読み込み済みファイル行数を持つエディタ状態。
// 返り値: 0以上の有効行数。
static inline int editor_line_limit(struct editor_state *state){
    int limit = state->str.line_capacity;
    if(state->file_data.now_open_file != NULL &&
       state->file_data.file_line_start_num_counter < limit){
        limit = (int)state->file_data.file_line_start_num_counter;
    }

    return (limit > 0) ? limit : 0;
}

// editor_view_cols(): 画面へ描ける桁数を返す。表示上の都合だけで使う値であり、
// バッファ容量とは無関係。リサイズで変わるのはこちらだけ。
// 引数: state=書き込み領域を持つエディタ状態。
// 返り値: 0以上の桁数。
static inline int editor_view_cols(struct editor_state *state){
    return (state->write_area.w > 0) ? state->write_area.w : 0;
}

// editor_line_cap(): 指定行に確保済みの列数を返す。
// 引数: state=行容量配列を持つエディタ状態、line=調べる論理行番号。
// 返り値: 確保済み列数。行が不正なら0。
static inline int editor_line_cap(struct editor_state *state, int line){
    if(state->str.line_cap == NULL || line < 0 || line >= state->str.line_capacity){
        return 0;
    }
    return (state->str.line_cap[line] > 0) ? state->str.line_cap[line] : 0;
}

// editor_line_cells(): 指定行のセル配列先頭を返す。
// line * col_capacityのような矩形前提の添字計算をこの関数へ集約している。
// 引数: state=編集バッファと行オフセットを持つエディタ状態、line=対象論理行。
// 返り値: 行先頭へのポインタ。行が不正、または未確保ならNULL。
static inline wint_t *editor_line_cells(struct editor_state *state, int line){
    if(state->str.wint_line_str_data == NULL || state->str.line_offset == NULL ||
       line < 0 || line >= state->str.line_capacity){
        return NULL;
    }
    long offset = state->str.line_offset[line];
    if(offset < 0 || offset >= state->str.total_capacity){
        return NULL;
    }
    return &state->str.wint_line_str_data[offset];
}

// editor_col_limit(): 現在行へ実際に書き込める列数を返す。
// バッファ容量と可視幅の小さい方。横スクロールが無いため可視幅も上限になる。
// 引数: state=行容量と書き込み領域を持つエディタ状態、line=対象論理行。
// 返り値: 0以上の有効列数。
static inline int editor_col_limit(struct editor_state *state, int line){
    int limit = editor_view_cols(state);
    int cap   = editor_line_cap(state, line);
    if(cap < limit){
        limit = cap;
    }

    return (limit > 0) ? limit : 0;
}

// editor_clamp_int(): valueをmin以上max以下に丸める。
// 引数: value=丸める値、min=下限、max=上限。
// 返り値: 範囲内に収めた値。
static inline int editor_clamp_int(int value, int min, int max){
    if(value < min){
        return min;
    }
    if(value > max){
        return max;
    }
    return value;
}

// editor_line_len(): 指定行が保持している桁数を返す。
// 画面幅では丸めない。画面外の桁もバッファ上には残っているため、
// 保存やUTF-8変換はこの長さを使う。
// 引数: state=行長と行容量を持つエディタ状態、line=調べる論理行番号。
// 返り値: 行容量で丸めた行長。不正な行なら0。
static inline int editor_line_len(struct editor_state *state, int line){
    if(line < 0 || line >= editor_line_limit(state)){
        return 0;
    }
    return editor_clamp_int(state->str.line[line], 0, editor_line_cap(state, line));
}

// editor_cursor_x_on_line(): 指定行で有効なカーソルx座標へ丸める。
// 行が可視幅より長い場合は可視幅で止める(横スクロール未実装のため)。
// 引数: state=書き込み領域と行長を持つエディタ状態、line=対象行、x=丸めるx座標。
// 返り値: 行頭から行末までの範囲に収めたx座標。
static inline int editor_cursor_x_on_line(struct editor_state *state, int line, int x){
    int len = editor_line_len(state, line);
    int view = editor_view_cols(state);
    if(len > view){
        len = view;
    }
    return editor_clamp_int(x, state->write_area.x_start,
                            state->write_area.x_start + len);
}

// editor_move_cursor_line(): 論理カーソル行(now_mouce_line)をdelta分だけ動かす。
// now_mouce_line への書き込みはこの関数経由に統一し、複数箇所からの多重加算を防ぐ。
// 引数: state=更新対象のエディタ状態、delta=移動量(負値で上へ)。
// 返り値: 範囲内で移動できたらtrue、範囲外で何もしなかったらfalse。
static inline bool editor_move_cursor_line(struct editor_state *state, int delta){
    int next = state->mouse.now_mouce_line + delta;
    if(next < 0 || next >= editor_line_limit(state)){
        return false;
    }
    state->mouse.now_mouce_line = next;
   
    return true;
}

enum line_mode {
    all_draw_mode,//書き直し時
    fix_scr_line_damege,//スクロールで線が破損したときなど
};


void draw_line_numbers(struct editor_state *state);
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode);
void draw_box(struct box box,WINDOW *win);
void request_draw_box(struct editor_state *state,struct box box);
void draw_all_line(WINDOW *win,struct editor_state *state);
void draw_editor_buffer_line(struct editor_state *state, int line, int screen_y);
void draw_now_path_name(struct box file_browse_box,char *path_name);
void draw_edit_screen_base(struct editor_state *state,WINDOW *win,struct pos start_pos,struct pos end_pos);
void draw_box_inside_dir(struct editor_state *state,char *table);
void draw_select_dir_scene_color(struct editor_state *state,int num);
void draw_line_status(struct editor_state *state,WINDOW *win);
void draw_file_data(struct editor_state *state);
void draw_status_bar_line(struct editor_state *state,struct box status_bar,WINDOW *win);
void draw_status_bar_path(struct editor_state *state, WINDOW *win);
void draw_line_jump(struct editor_state *state);

void load_view_from_cursor(struct editor_state *state);
void load_dir_table(struct editor_state *state,char *table,int table_size,char *path_name);
void load_file(struct editor_state *state,char *table,char *path_name,struct file_browse_select_state *select_state);
void load_screen_size(struct editor_state *state);
void load_all_lines(struct editor_state *state);
void load_string_data(struct editor_state *state,long load_start_line,int load_size);
void load_default_editor_settings(struct editor_settings *settings_data);
void load_custom_editor_settings(struct editor_settings *settings_data);

void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_resize(WINDOW *win, struct editor_input_context *ctx);
void handle_backspace(WINDOW *win, struct editor_state *state);
void handle_newline(WINDOW *win, struct editor_state *state);
void handle_tab(WINDOW *win, struct editor_state *state);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state);
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);

void scr_show_line_str(WINDOW *win,struct editor_state *state);
void scr_show_line_str_down(WINDOW *win,struct editor_state *state);

void editor_error_screen(struct editor_state *state,char *error_comment);
void editor_screen_move_line(struct editor_state *state,WINDOW *win,int num);
char *editor_buffer_to_utf8(struct editor_state *state);

// 編集バッファ(wint_line_str_data / line / line_offset / line_cap)の確保・解放・伸長。
bool editor_alloc_text_buffer(struct editor_state *state, int line_count, long total_capacity);
void editor_free_text_buffer(struct editor_state *state);
bool editor_ensure_line_cap(struct editor_state *state, int line, int need);
bool editor_ensure_row_capacity(struct editor_state *state, int need_rows);


void set_line_limit(int limit);
void set_line_memory(struct editor_state *state);
long get_last_visible_file_line(struct editor_state *state);
void save_file(struct editor_state *state);

void show_file_browse(struct editor_state *state,struct box file_browse_box,char *dir_name_table,char *path_name,WINDOW *win);
void set_file_sellect_line(struct editor_state *state,int line);
void file_select_line_update(struct file_select_line *file_select_line,int line);

void ask_new_file_name(struct pos str_start_pos,int w,int h);
void clear_box(struct clear_box_data *clear_box);

void get_new_file_name();
int get_line_limit();

char *uint_to_str(unsigned int value, char *buf);

void my_mvaddstr(struct pos pos,char * str);
void my_mvaddch(struct pos pos,char str);
void update_screen(struct editor_input_context *ctx);
void request_clear_box(struct editor_state *state, struct box box);

void move_view_to_line(WINDOW *win, struct editor_state *state, long target_line,
                              int x, struct pos line_start_pos, struct pos line_end_pos);
                              
int remove_line_join_str_data(struct editor_state *state,long remove_line_num);
int make_new_line_space(struct editor_state *state,long make_space_line_num);
void resize_file_browser(struct editor_input_context *ctx);
int  update_sccreen_ratio(struct editor_input_context *ctx);



#endif
