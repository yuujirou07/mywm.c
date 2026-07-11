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

enum render_flags {
    RENDER_NONE       = 0,
    RENDER_LINE_STATUS     = 1 << 0,
    RENDER_STATUS_BAR_LINE = 1 << 1,
    RENDER_LINE  = 1 << 2,
    RENDER_STATUS_BAR = 1 << 3,
    RENDER_SELECT_DIR_SCENE_COLOR = 1 << 4,
    RENDER_EDIT_SCREEN_BASE = 1<<5,
    RENDER_FILE_DATA = 1<<6,
    RENDER_FILE_BROWSE = 1 << 7,
    RENDER_BOX        = 1 << 8,
    RENDER_CLEAR_BOX = 1 << 9,
    RENDER_ALL        = 1 << 10,
    RENDER_LINE_JUMP = 1 << 11,
    RENDER_MAKE_FILE = 1 << 12,
};


#define CTRL(x) ((x) & 0x1f)// 0x1fはCtrl

typedef int (*Start_Menu)(int screen_w, int screen_h, struct ascii_data *ascii_data,
                          const struct timespec *startup_start_time,
                          const char *startup_log_path);




enum status_bar_side{
    top,
    bottom,
};

struct jump_mode{
    char jump_line_num[JUMP_LINE_NUM_DIGITS + 1];
    int  jump_line_num_counter;
};

struct file_data{
    FILE*   now_open_file;
    char**  file_str_data;
    char    now_open_path_name[DEFAULT_PATH_NAME_MAX_SIZE];
    long*   file_line_start_num;
    long    file_line_start_num_counter;
    long    description_line_end;
    long    file_str_line_end;//可視文字がある行数
    int     file_line_n;
    bool    is_open_file;
};
enum select_state{
    file,
    folder,
    error,
};


enum now_screen_state{
    edit_screen,
    file_browse_screen,
    start_menu_file_browse_screen,
    start_menu_screen,
    error_screen,
    line_jump_mode,
    ask_make_file_mode,
};

struct make_file_mode_status{
    bool is_input_scene;
    char new_file_name[DEFAULT_PATH_NAME_MAX_SIZE];
    int new_file_name_counter;
};
struct file_browse_select_state{
    enum select_state select_state;
    char select_name[NAME_MAX + 1];
};

struct pos {
    int x;
    int y;
};

struct file_select_line {
    int now_line;
    int previous_line;
};
struct box {
    struct pos pos;
    int w;
    int h;
};

struct write_possible_area {
    int x_start;
    int y_start;
    int x_end;
    int y_end;
    int w;
    int h;
};

struct scr_data {
    struct pos cursor_pos;
    struct pos scr_size;
    int scr_start_num;
};

struct str_data {
    wint_t *wint_line_str_data;
    char   *chr_file_all_str_data;
    int    *line;
    int     line_capacity;
    int     col_capacity;
};

struct mouse_data {
    int now_mouce_line;
    struct pos scr_abs_now_pos;
};

struct clear_box_data{
    struct box clear_box[box_retention_max];
    int clear_box_counter;
};


struct editor_state {
    struct editor_settings    *settings_data;
    struct scr_data            scr;
    struct str_data            str;
    struct mouse_data          mouse;
    struct write_possible_area write_area;
    struct make_file_mode_status make_file_mode_status;
    struct box                 file_browser_area;
    struct box                 draw_box_data[DRAW_BOX_REQUEST_MAX];
    int                        draw_box_count;
    struct box                *file_browser_box;
    struct box                *status_bar;
    struct box                 ask_make_file_box;
    struct box                 write_file_name_area;
    struct file_data           file_data;
    struct jump_mode           jump_mode_data;
    struct file_select_line    file_select_line_data;
    struct clear_box_data      clear_box_data;
    enum now_screen_state      screen_state;
    int                        dir_num;
    int                        render_flags;
    bool                       is_cur_show;

};


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
    Start_Menu start_menu;
    struct ascii_data *ascii_data;
    const struct timespec *startup_start_time;
    const char *startup_log_path;
    struct lsp_process *lsp_data;
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

// editor_col_limit(): 1行で編集できる最大列数を返す。
// 引数: state=画面上の書き込み領域と行バッファ列容量を持つエディタ状態。
// 返り値: 0以上の有効列数。
static inline int editor_col_limit(struct editor_state *state){
    int limit = state->write_area.w;
    if(state->str.col_capacity < limit){
        limit = state->str.col_capacity;
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

// editor_line_len(): 指定行の表示可能な文字数を返す。
// 引数: state=行長と列上限を持つエディタ状態、line=調べる論理行番号。
// 返り値: 列上限で丸めた行長。不正な行なら0。
static inline int editor_line_len(struct editor_state *state, int line){
    if(line < 0 || line >= editor_line_limit(state)){
        return 0;
    }
    return editor_clamp_int(state->str.line[line], 0, editor_col_limit(state));
}

// editor_cursor_x_on_line(): 指定行で有効なカーソルx座標へ丸める。
// 引数: state=書き込み領域と行長を持つエディタ状態、line=対象行、x=丸めるx座標。
// 返り値: 行頭から行末までの範囲に収めたx座標。
static inline int editor_cursor_x_on_line(struct editor_state *state, int line, int x){
    return editor_clamp_int(x, state->write_area.x_start,
                            state->write_area.x_start + editor_line_len(state, line));
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
void handle_resize(WINDOW *win, struct editor_state *state,struct pos *start_pos,struct pos *end_pos);
void handle_backspace(WINDOW *win, struct editor_state *state);
void handle_newline(WINDOW *win, struct editor_state *state);
void handle_tab(struct editor_state *state);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state);
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);

void scr_show_line_str(WINDOW *win,struct editor_state *state);
void scr_show_line_str_down(WINDOW *win,struct editor_state *state);

void editor_error_screen(struct editor_state *state,char *error_comment);
void editor_screen_move_line(struct editor_state *state,WINDOW *win,int num);
char *editor_buffer_to_utf8(struct editor_state *state);


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

#endif
