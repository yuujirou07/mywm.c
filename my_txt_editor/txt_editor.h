#ifndef TXT_EDITOR_H
#define TXT_EDITOR_H
#include <ncurses.h>

#define MAX_LINES    1000
#define MAX_LINE_LEN 1000

struct pos{
    int x;
    int y;
};

struct write_possible_area{
    int x_start;
    int y_start;
    int x_end;
    int y_end;
};

struct scr_data{
    WINDOW *win;
    struct pos cursor_pos;
    struct pos scr_size;
};

struct text_buffer {
    char *lines;
    int now_line;
};

void draw_line_numbers(struct scr_data *scr_data, struct write_possible_area *area);
#endif
