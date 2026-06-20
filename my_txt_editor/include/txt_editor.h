#ifndef TXT_EDITOR_H
#define TXT_EDITOR_H

#include <ncurses.h>
#include <stdbool.h>
#include <wchar.h>

#define MAX_LINES    1000
#define MAX_LINE_LEN 1000
#define INDENT_RANGE 8

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
};

void draw_line_numbers(struct scr_data *scr_data, struct write_possible_area *area);
void handle_resize(WINDOW *win, struct editor_state *state);
void handle_backspace(WINDOW *win, struct editor_state *state);
void handle_newline(WINDOW *win, struct editor_state *state);
void handle_tab(void);
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state);
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state);
void scr_show_line_str(WINDOW *win,struct editor_state *state);
#endif
