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
#include "editor_types.h"
#include "settings_screen.h"
#include"filetree.h"


#define SETTINGS_FILE_EXT ".json"

#define startuptime_log_file_argument_num 1
#define FDS_N 4
#define DRAW_BOX_REQUEST_MAX 64
#define box_retention_max 64
#define screen_state_log_storage 256
// ファイルブラウザ一覧に保持する名前の最大長。
#define DIR_ENTRY_NAME_MAX 256
// 各行へ前もって足しておく余白列数。ここに収まる入力は再配置なしで処理できる。
#define EDITOR_LINE_COL_SLACK 16
// 1行が確保できる列数の絶対上限。これを超える伸長要求は拒否する。
#define EDITOR_LINE_COL_MAX 65536

// update_screen()で再描画する領域を指定するビットフラグ。
enum render_flags {
    RENDER_NONE       = 0,          // 再描画要求なし。
    RENDER_LINE_STATUS     = 1 << 0,// 現在行表示を更新する。
    RENDER_STATUS_BAR_LINE = 1 << 1,// ステータスバーの区切り線を更新する。
    RENDER_LINE  = 1 << 2,      // 編集領域左の縦線を更新する。
    RENDER_STATUS_BAR = 1 << 3, // ステータスバーの内容を更新する。
    RENDER_SELECT_DIR_SCENE_COLOR = 1 << 4, // ファイル選択行の反転表示を更新する。
    RENDER_EDIT_SCREEN_BASE = 1<<5, // 編集画面の枠や基本線を更新する。
    RENDER_FILE_DATA = 1<<6,    // 編集バッファの表示内容を更新する。
    RENDER_FILE_BROWSE = 1 << 7, // ファイルブラウザ全体を更新する。
    RENDER_BOX        = 1 << 8, // draw_box_queueに積まれた枠を描画する。
    RENDER_CLEAR_BOX = 1 << 9,  // clear_box_dataに積まれた範囲を消す。
    RENDER_ALL        = 1 << 10, // 画面全体更新用の予約フラグ。
    RENDER_LINE_JUMP = 1 << 11, // 行ジャンプ入力欄を更新する。
    RENDER_MAKE_FILE = 1 << 12, // 新規ファイル作成ダイアログを更新する。
    RENDER_SETTINGS = 1 << 13, //設定ファイルを描画する
};

// ファイルツリーは編集画面の左端に重ねて出し、編集領域をその幅だけ右へ寄せる。
// 描画側でwrite_areaを書き換えると再描画やリサイズで位置がずれるため、
// レイアウトの計算はeditor_apply_write_area()へ集約する。

// <sys/ttydefaults.h>(sys/epoll.h経由で入る)も同名・同値のCTRLを定義しているため、
// 先に外してから定義し直す。値は同じなので、どちらが残っても動作は変わらない。
#ifdef CTRL
#undef CTRL
#endif
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
    // 実際に開いて編集・保存・LSP通知するファイルパス。ブラウザから開く場合は絶対パス、
    // 新規作成時は入力された相対パスまたは絶対パスをそのまま保持する。
    // now_open_path_name()内のファイルブラウザ用パスとは別に保持する。
    char    now_open_path_name[DEFAULT_PATH_NAME_MAX_SIZE];
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
    unkown,
    error,
};


// 現在表示している画面・入力モード。
enum now_screen_state{
    edit_screen, // 通常の編集画面。
    file_browse_screen, // ファイルブラウザ画面。
    filetree_screen, // ファイルツリー画面。
    start_menu_screen, // start menu pluginの画面。
    error_screen, // エラー表示画面。
    line_jump_mode, // 行ジャンプ番号入力中。
    ask_make_file_mode, // 新規ファイル作成確認中。
    setting_screen,//せってう画面
    screen_state_log_error, // 指定された履歴位置が範囲外。
};

// 新規ファイル作成ダイアログの入力状態。
struct make_file_mode_status{
    bool is_input_scene; // trueならファイル名入力欄を編集中。
    char new_file_name[DEFAULT_PATH_NAME_MAX_SIZE]; // 入力された保存先。相対パスまたは絶対パス。
    int new_file_name_counter; // new_file_nameに入っている文字数。
};

// ファイルブラウザで決定された項目名と種別。
struct file_browse_select_state{
    enum select_state select_state; // 選択項目がファイル・フォルダ・エラーのどれか。
    char select_name[NAME_MAX + 1]; // 選択された項目名。
};

struct dir_table{
    char *path_name; // 操作対象のパス。呼び出し元により相対パスまたは絶対パス。
    char d_name[DIR_ENTRY_NAME_MAX];
    unsigned char d_type;
};

struct dir_entry {
    char name[DIR_ENTRY_NAME_MAX];
    unsigned char d_type;
};

// ファイルブラウザで反転表示する行の現在値と直前値。
struct file_select_line {
    int now_line; // 表示領域の先頭から数えた選択行。
    int previous_line; // 前回選択していた行。
    int now_logical_line; // dir_name_tableの表示開始添字。
};

// ファイルブラウザ画面の状態。外枠・内側領域・一覧テーブル・現在パス・選択行を
// 1箇所にまとめる。描画側と入力側が同じ実体を直接見るので、実体を指す別名ポインタは持たない。
struct file_browse_state{
    struct box box; // ブラウザ全体の外枠。
    struct box search_box; // パス入力欄の枠。
    struct box area; // 外枠の内側で一覧を描く領域。
    struct dir_entry *dir_name_table; // 一覧に出す名前と種別。
    int dir_name_table_rows; // dir_name_tableの確保済み行数。
    int dir_num; // ディレクトリ内の全エントリ数。
    int dir_name_table_num; // 現在の表示範囲にある有効な行数。
    // 現在表示しているディレクトリ。getcwd()で絶対パスに初期化するが、
    // パス入力モードでは相対パスも入り得る。実体はここが所有する。
    char path_name[PATH_MAX];
    bool path_input_mode; // trueならパス入力欄を編集中。
    struct file_select_line select_line; // 反転表示する行。
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

// 画面サイズ・スクロール開始行。
// カーソル位置はここには持たない(struct cursorが唯一の保持場所)。
struct scr_data {
    struct pos scr_size; // 現在の画面サイズ。
    int scr_start_num; // 画面先頭に表示している論理行番号。
};

// 編集カーソルの論理ファイル座標と、描画時に計算した画面座標。
struct cursor {
    struct pos file_pos; // x=行頭からの桁、y=ファイル先頭からの論理行。
    struct pos screen_pos; // x/y=画面上のカーソル座標。
    struct pos show_cur_pos_queue;
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

// 後で消去する矩形領域を一時的に保持する。
struct clear_box_data{
    struct box clear_box[box_retention_max]; // 消去予定の矩形配列。
    int clear_box_counter; // clear_boxに積まれている数。
};

// 次回更新で描く枠を積むキュー。配列と件数を分けて持つと
// 「件数 <= 確保数」を呼び出し側が守る必要があるため、1つにまとめる。
struct box_queue{
    struct box box[DRAW_BOX_REQUEST_MAX]; // 積まれた枠。先頭からcount個が有効。
    int count; // boxに入っている有効な数。
};

struct screen_state_log{
    enum now_screen_state screen_state_log[screen_state_log_storage]; // 画面遷移履歴。
    int screen_state_log_counter; // 記録済みの遷移数。
};



// エディタ全体で共有する実行時状態。
struct editor_state {
    struct editor_settings    *settings_data; // 設定ファイルとデフォルト値から作った設定。
    struct scr_data            scr; // 画面サイズ・スクロール状態。
    struct str_data            str; // 編集バッファと行長情報。
    struct cursor              cursor; // 編集カーソルの論理位置。カーソルの唯一の情報源。
    struct write_possible_area write_area; // 編集可能な画面領域。
    struct make_file_mode_status make_file_mode_status; // 新規ファイル作成ダイアログ状態。
    struct box_queue           draw_box_queue; // 次回描画する枠のキュー。
    struct file_browse_state   file_browse; // ファイルブラウザ画面の状態。
    struct box                *status_bar; // ステータスバー領域への参照。
    struct box                 ask_make_file_box; // 新規ファイル作成ダイアログ外枠。
    struct box                 write_file_name_area; // 新規ファイル名入力欄。
    struct file_data           file_data; // 現在開いているファイルと行情報。
    struct jump_mode           jump_mode_data; // 行ジャンプ入力状態。
    struct clear_box_data      clear_box_data; // 次回消去する矩形領域。
    struct screen_state_log    screen_log; // 現在状態を末尾に持つ画面遷移履歴。
    settings_screen_data       settings_screen_data;
    file_tree_data             file_tree_data; 
    int                        render_flags; // update_screen()へ渡す再描画要求。
    bool                       is_cur_show; // カーソル表示中ならtrue。
    bool                       mylsp_use;
};

// editor_get_screen_state_log(): 現在位置を基準に画面遷移履歴を取得する。
// 引数: state=画面遷移履歴を持つ状態、history_offset=0なら現在、1なら直前、2なら2つ前。
// 返り値: 指定位置の画面状態。履歴範囲外ならscreen_state_log_error。
static inline enum now_screen_state editor_get_screen_state_log(struct editor_state *state,
                                                                int history_offset){
    if(history_offset < 0 ||
       history_offset >= state->screen_log.screen_state_log_counter){
        return screen_state_log_error;
    }

    int log_index = state->screen_log.screen_state_log_counter - history_offset - 1;
    return state->screen_log.screen_state_log[log_index];
}

// editor_get_screen_state(): 現在の画面状態を返す。
// 引数: state=画面遷移履歴を持つ状態。
// 返り値: 現在の画面状態。履歴が空ならscreen_state_log_error。
static inline enum now_screen_state editor_get_screen_state(struct editor_state *state){
    return editor_get_screen_state_log(state,0);
}

// editor_set_screen_state(): 画面状態を履歴末尾へ追加する。
// 同じ状態が連続する場合は追加せず、満杯なら最古の状態を捨てる。
// 引数: state=更新する画面遷移履歴、next_state=遷移先。
// 返り値: なし。
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




struct edit_screen_context {
    struct pos line_start_pos;
    struct pos line_end_pos;
};

struct ask_make_file_mode_context {
    int screen_center_y;
    struct pos screen_center_pos;
};

struct start_menu_screen_context {
    bool *open;
    bool has_plugin;
    Start_Menu plugin;
    struct ascii_data *ascii_data;
    const struct timespec *startup_start_time;
    const char *startup_log_path;
};

struct editor_input_context {
    WINDOW *win;
    MEVENT *mouse_event;
    struct editor_state *state;
    struct lsp_process *lsp_data;
    struct edit_screen_context edit_screen;
    struct ask_make_file_mode_context ask_make_file_mode;
    struct start_menu_screen_context start_menu_screen;
};

// editor_filetree_offset(): ファイルツリーを表示中に編集領域を右へ寄せる列数を返す。
// 引数: state=ファイルツリーの表示状態と枠を持つエディタ状態。
// 返り値: 寄せる列数。表示していない、または枠が未設定なら0。
static inline int editor_filetree_offset(struct editor_state *state){
    if(!state->file_tree_data.is_show){
        return 0;
    }
    int offset = state->file_tree_data.ft_box.pos.x +
                 state->file_tree_data.ft_box.w;

    return (offset > 0) ? offset : 0;
}

// editor_apply_write_area(): ファイルツリーの表示状態を反映し、編集領域の左端と幅を決め直す。
// x_endは呼び出し側が決めた値(画面幅や右余白)をそのまま使う。
// 引数: state=編集領域・画面設定・ファイルツリーを持つエディタ状態。
// 返り値: なし。
static inline void editor_apply_write_area(struct editor_state *state){
    state->write_area.x_start = state->settings_data->line_number_space + 1 +
                                editor_filetree_offset(state);
    int w = state->write_area.x_end - state->write_area.x_start;
    state->write_area.w = (w > 0) ? w : 0;
}

// editor_sync_split_line(): 区切り線の座標を現在のwrite_areaへ合わせる。
// 引数: ctx=編集領域と区切り線座標を持つ入力context。
// 返り値: なし。
static inline void editor_sync_split_line(struct editor_input_context *ctx){
    int x = ctx->state->write_area.x_start - 1;

    ctx->edit_screen.line_start_pos = (struct pos){x,ctx->state->write_area.y_start};
    ctx->edit_screen.line_end_pos   = (struct pos){x,ctx->state->write_area.y_end};
}


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

// get_screen_cursor_pos(): stateに保存されている画面上のカーソル座標を返す。
// 引数: state=取得元のエディタ状態。
// 返り値: cursor.screen_posの値。stateがNULLの場合の動作は未定義。
static inline struct pos get_screen_cursor_pos(struct editor_state *state){
    return state->cursor.screen_pos;
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

// editor_clamp_col(): 指定行で有効なカーソル桁へ丸める。
// 行が可視幅より長い場合は可視幅で止める(横スクロール未実装のため)。
// 引数: state=書き込み領域と行長を持つエディタ状態、line=対象行、col=丸める桁数。
// 返り値: 0から行末までの範囲に収めた桁数。
static inline int editor_clamp_col(struct editor_state *state, int line, int col){
    int len = editor_line_len(state, line);
    int view = editor_view_cols(state);
    if(len > view){
        len = view;
    }
    return editor_clamp_int(col, 0, len);
}

// editor_cursor_screen_pos(): 論理ファイル座標から画面座標を計算して保持する。
// 引数: state=カーソル・表示開始行・書き込み領域を持つエディタ状態。
// 返り値: カーソルを置くべき画面座標。
static inline struct pos editor_cursor_screen_pos(struct editor_state *state){
    state->cursor.screen_pos.x = state->write_area.x_start + state->cursor.file_pos.x;
    state->cursor.screen_pos.y = state->write_area.y_start +
        (state->cursor.file_pos.y - state->scr.scr_start_num);
    return state->cursor.screen_pos;
}

// editor_cursor_write_area_pos(): 論理カーソル位置を表示領域内の相対座標へ変換する。
// 引数: state=カーソル位置・表示開始行・書き込み領域を持つエディタ状態。
// 返り値: 書き込み領域左上を原点とするカーソル座標。
static inline struct pos editor_cursor_write_area_pos(struct editor_state *state){
    struct pos pos;
    pos.x = state->cursor.file_pos.x;
    pos.y = state->cursor.file_pos.y - state->scr.scr_start_num;
    return pos;
}

// editor_cursor_logical_line_pos(): 現在の論理カーソル行を返す。
// 引数: state=カーソル位置を持つエディタ状態。
// 返り値: ファイル先頭を0とする論理行番号。
static inline int editor_cursor_logical_line_pos(struct editor_state *state){
    return state->cursor.file_pos.y;
}

// editor_cursor_is_visible(): カーソル行が現在の表示範囲に入っているかを返す。
// 引数: state=カーソル行・表示開始行・書き込み領域を持つエディタ状態。
// 返り値: 編集領域内に見えているならtrue。
static inline bool editor_cursor_is_visible(struct editor_state *state){
    struct pos pos = editor_cursor_screen_pos(state);
    return (pos.y >= state->write_area.y_start && pos.y < state->write_area.y_end);
}

int cur_pos_push(struct pos pos,struct editor_state *state);
int set_cur_pos(struct editor_state *state);

// editor_sync_cursor(): 論理カーソル位置から表示座標を計算して反映待ちへ積む。
// 引数: state=論理カーソル位置と反映待ち座標を持つエディタ状態。
// 返り値: なし。
static inline void editor_sync_cursor(struct editor_state *state){
    state->cursor.file_pos.x = editor_clamp_col(state, state->cursor.file_pos.y,
        state->cursor.file_pos.x);
    struct pos pos = editor_cursor_screen_pos(state);
    cur_pos_push(pos,state);
}

// editor_set_cursor(): カーソルを指定の論理位置へ置く。行は有効範囲、桁は行長で丸める。
// 引数: state=更新対象のエディタ状態、line=移動先の論理行、col=移動先の桁。
// 返り値: なし。
static inline void editor_set_cursor(struct editor_state *state, int line, int col){
    int line_limit = editor_line_limit(state);
    if(line_limit <= 0){
        state->cursor.file_pos = (struct pos){0,0};
        return;
    }
    state->cursor.file_pos.y = editor_clamp_int(line, 0, line_limit - 1);
    state->cursor.file_pos.x = editor_clamp_col(state, state->cursor.file_pos.y, col);
}

// editor_move_cursor_line(): 論理カーソル行をdelta分だけ動かす。桁は新しい行長へ丸める。
// cursor.file_pos.yへの書き込みはこの関数かeditor_set_cursor()経由に統一し、
// 複数箇所からの多重加算を防ぐ。
// 引数: state=更新対象のエディタ状態、delta=移動量(負値で上へ)。
// 返り値: 範囲内で移動できたらtrue、範囲外で何もしなかったらfalse。
static inline bool editor_move_cursor_line(struct editor_state *state, int delta){
    int next = state->cursor.file_pos.y + delta;
    if(next < 0 || next >= editor_line_limit(state)){
        return false;
    }
    state->cursor.file_pos.y = next;
    state->cursor.file_pos.x = editor_clamp_col(state, next, state->cursor.file_pos.x);

    return true;
}

enum line_mode {
    all_draw_mode,//書き直し時
    fix_scr_line_damage,//スクロールで線が破損したときなど
};

// txt_editor_draw.c
void draw_editor_buffer_line(struct editor_state *state, int line, int screen_y);
void draw_line_numbers(struct editor_state *state);
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode);
void draw_box(struct box box,WINDOW *win);
void request_draw_box(struct editor_state *state,struct box box);
void draw_now_path_name(struct box file_browse_box,char *path_name);
void draw_edit_screen_base(struct editor_state *state,WINDOW *win,struct pos start_pos,struct pos end_pos);
void draw_box_inside_dir(struct editor_state *state,struct dir_entry *table);
void draw_select_dir_scene_color(struct editor_state *state,int dir_num,int num);
void show_file_browse(struct editor_state *state);
void set_file_select_line(struct editor_state *state,int dir_num,int line);
void editor_screen_move_line(struct editor_input_context *ctx,int num);
void editor_error_screen(struct editor_state *state,char *error_comment);
void draw_file_data(struct editor_state *state);
void draw_status_bar_line(struct editor_state *state,struct box status_bar,WINDOW *win);
void draw_status_bar_path(struct editor_state *state, WINDOW *win);
void clear_box(struct clear_box_data *clear_box);
void draw_line_status(struct editor_state *state,WINDOW *win);
void update_screen(struct editor_input_context *ctx);
void request_clear_box(struct editor_state *state, struct box box);
void draw_line_jump(struct editor_state *state);
int set_clear_box(struct clear_box_data *clear_box_data,struct box box);
void set_settings_screen_box(struct editor_state *state);

// txt_editor_file.c
int load_dir_table(struct editor_state *state,struct dir_entry **table,int *table_rows,char *path_name,int start_num,int *dir_num,int *table_num);
void load_file(struct editor_state *state,struct dir_entry *table,int table_num,char *path_name,struct file_browse_select_state *select_state);
bool editor_alloc_text_buffer(struct editor_state *state, int line_count, long total_capacity);
void editor_free_text_buffer(struct editor_state *state);
bool editor_ensure_line_cap(struct editor_state *state, int line, int need);
bool editor_ensure_row_capacity(struct editor_state *state, int need_rows);
void set_line_memory(struct editor_state *state);
void load_string_data(struct editor_state *state,long load_start_line,int load_size);
void load_all_lines(struct editor_state *state);
char *editor_buffer_to_utf8(struct editor_state *state);
void save_file(struct editor_state *state);
void load_screen_size(struct editor_state *state);
void load_default_editor_settings(struct editor_settings *settings_data);
void load_custom_editor_settings(struct editor_settings *settings_data);
void file_select_line_update(struct file_select_line *file_select_line,int line);
void input_mode_tmp_path(char *path,enum flags flags);
const wchar_t *now_open_path_name(struct dir_table *path,enum flags flags);
int check_dir_mem(struct dir_table *dir_table,int size);
enum select_state get_path_state(const char *path);
int now_input_path_open(struct editor_state *state,struct editor_input_context *ctx);
int file_browser_show_mem_start_num(int start_num,enum flags flags);

// txt_editor_func.c
void resize_file_browser(struct editor_state *state);
void handle_resize(WINDOW *win, struct editor_input_context *ctx);
void handle_backspace(struct editor_input_context *ctx);
void handle_newline(struct editor_input_context *ctx);
void handle_tab(WINDOW *win, struct editor_state *state);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(struct editor_input_context *ctx,int dir_num);
void handle_input_allow(struct editor_input_context *ctx,wchar_t ch);
void set_line_limit(int limit);
int get_line_limit();
int remove_line_join_str_data(struct editor_state *state,long remove_line_num);
int make_new_line_space(struct editor_state *state,long make_space_line_num);
void editor_screen_mouse_event(struct editor_input_context *ctx);
void file_browse_screen_mouse_event(WINDOW *win, MEVENT *event, struct editor_state *state,int dir_num);
void set_file_browse_path_input_mode(struct file_browse_state *file_browse,bool flag);
bool get_file_browse_path_input_mode(struct file_browse_state *file_browse);
void my_cur_set(struct editor_state *state,bool set);

// txt_editor_state.c
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);
void move_view_to_line(struct editor_state *state, long target_line, int col);
int update_screen_ratio(struct editor_input_context *ctx);
void restore_edit_screen(struct editor_state *state);

// main.c
int cur_pos_mg(struct pos mouse_pos,enum flags flags);
void my_mvaddstr(struct pos pos,char *str);

int filetree_mouse_event(struct editor_input_context *ctx);

#endif
