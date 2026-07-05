#ifndef TXT_EDITOR_H
#define TXT_EDITOR_H

#include <ncurses.h>
#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>
#include <dirent.h>
#include <limits.h>
#include"default_settings.h"

#define my_txt_editor_var 0.0
#define new_file 0
#define quit 1
#define settings 2
#define select_folder 3


#define CTRL(x) ((x) & 0x1f)// 0x1fはCtrl

enum status_bar_side{
    top,
    bottom,
};

struct jump_mode{
    char jump_line_num[JUMP_LINE_NUM_DIGITS + 1];
    int  jump_line_num_counter;
};

struct editor_settings{
    int max_lines;
    int max_line_size;
    int line_number_space;
    int indent_range;
    int jmp_set_cur_pos;
    int default_load_line_size;
    int load_buffer_lines;
    enum status_bar_side bar_side_state;
    bool show_status_bar;
    bool draw_split_line;
    bool ask_make_file;//ファイル変更時に何もファイルを開いていなかった場合ファイルを作るか聞く
    bool show_start_menu;
};
struct file_data{
    FILE*   now_open_file;
    char**  file_str_data;
    char    now_open_path_name[DEFAULT_PATH_NAME_MAX_SIZE];
    long*   file_line_start_num;
    long    file_line_start_num_counter;
    long    description_line_end;
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
    wint_t *line_str_data;
    int    *line;
    int     line_capacity;
    int     col_capacity;
};

struct mouse_data {
    int now_mouce_line;
    struct pos scr_abs_now_pos;
};

struct editor_state {
    struct editor_settings    *settings_data;
    struct scr_data            scr;
    struct str_data            str;
    struct mouse_data          mouse;
    struct write_possible_area write_area;
    struct make_file_mode_status make_file_mode_status;
    struct box                 file_browser_area;
    struct box                *file_browser_box;
    struct box                *status_bar;
    struct box                 ask_make_file_box;
    struct box                 write_file_name_area;
    struct file_data           file_data;
    struct jump_mode           jump_mode_data;
    enum now_screen_state      screen_state;
    int                        file_select_line; 
    int                        dir_num;
    bool                       is_cur_show;

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
void draw_all_line(WINDOW *win,struct editor_state *state);
void draw_editor_buffer_line(struct editor_state *state, int line, int screen_y);
void draw_now_path_name(struct box file_browse_box,char *path_name);
void draw_edit_screen_base(struct editor_state *state,WINDOW *win,struct pos start_pos,struct pos end_pos);
void draw_box_inside_dir(struct editor_state *state,char *table);
void draw_select_dir_scene_color(struct editor_state *state,int num);
void show_file_browse(struct editor_state *state,struct box file_browse_box,char *dir_name_table,char *path_name,WINDOW *win);
void file_sellect_line_update(struct editor_state *state,int line);
void load_string_data(struct editor_state *state,long load_start_line,int load_size);
void draw_file_data(struct editor_state *state);
void draw_status_bar_line(struct editor_state *state,struct box status_bar,WINDOW *win);
void draw_status_bar_path(struct editor_state *state, WINDOW *win);
void load_view_from_cursor(struct editor_state *state);

void load_dir_table(struct editor_state *state,char *table,int table_size,char *path_name);
void load_file(struct editor_state *state,char *table,char *path_name,struct file_browse_select_state *select_state);
void load_screen_size(struct editor_state *state);
void set_line_memory(struct editor_state *state);
void load_all_lines(struct editor_state *state);
void save_file(struct editor_state *state);
void load_default_editor_settings(struct editor_settings *settings_data);
void load_custom_editor_settings(struct editor_settings *settings_data);

void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_resize(WINDOW *win, struct editor_state *state,struct pos *start_pos,struct pos *end_pos);
void handle_backspace(WINDOW *win, struct editor_state *state);
void handle_newline(WINDOW *win, struct editor_state *state);
void handle_tab(struct editor_state *state);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state);

void scr_show_line_str(WINDOW *win,struct editor_state *state);
void scr_show_line_str_down(WINDOW *win,struct editor_state *state);

void editor_error_screen(struct editor_state *state,char *error_comment);
void editor_screen_move_line(struct editor_state *state,WINDOW *win,int num);


void set_line_limit(int limit);
void ask_new_file_name(struct pos str_start_pos,int w,int h);
void clear_box(struct box box);
void get_new_file_name();
char *uint_to_str(unsigned int value, char *buf);
int get_line_limit();

void my_mvaddstr(struct pos pos,char * str);
void my_mvaddch(struct pos pos,char str);
void draw_line_status(struct editor_state *state,WINDOW *win);

#endif
