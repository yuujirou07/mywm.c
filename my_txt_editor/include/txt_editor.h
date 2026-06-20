#ifndef TXT_EDITOR_H
#define TXT_EDITOR_H

#include <ncurses.h>
#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>

#define MAX_LINES    1000
#define MAX_LINE_LEN 1000
#define INDENT_RANGE 8
#define CTRL(x) ((x) & 0x1f)// 0x1fはCtrl

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
};

struct mouse_data {
    int now_mouce_line;
};

struct editor_state {
    struct scr_data            scr;
    struct str_data            str;
    struct mouse_data          mouse;
    struct write_possible_area write_area;
    bool                       is_cur_show;
    bool                       is_show_box;
};

enum line_mode {
    all_draw_mode,//書き直し時
    fix_scr_line_damege,//スクロールで線が破損したときなど
};

void draw_line_numbers(struct scr_data *scr_data, struct write_possible_area *area);
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode);
void draw_box(struct editor_state *state,struct box box,WINDOW *win);

void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_resize(WINDOW *win, struct editor_state *state);
void handle_backspace(WINDOW *win, struct editor_state *state);
void handle_newline(WINDOW *win, struct editor_state *state);
void handle_tab(struct editor_state *state);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state);

void scr_show_line_str(WINDOW *win,struct editor_state *state);

#endif
