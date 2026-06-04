#include<ncurses.h>
#include<locale.h>
#include"txt_editor.h"
#include<stdlib.h>

struct str_data
{
    int **str_count;  // str_count[y][x] = character at (y, x)
    int  *line_len;   // line_len[y] = number of chars on line y
    int   cur_line;
};

int main()
{
    struct scr_data scr_data;
    struct str_data str_data;
    struct write_possible_area write_area;

    setlocale(LC_ALL, "");

    int clor_pair[10] = {0};
    int color_pair_count = 0;

    scr_data.win = initscr();
    noecho();
    scr_data.cursor_pos.x = 0;
    scr_data.cursor_pos.y = 0;

    getmaxyx(scr_data.win,scr_data.scr_size.y,scr_data.scr_size.x);

    str_data.str_count = calloc(scr_data.scr_size.y, sizeof(int*));
    str_data.line_len  = calloc(scr_data.scr_size.y, sizeof(int));
    for (int i = 0; i < scr_data.scr_size.y; i++)
        str_data.str_count[i] = calloc(scr_data.scr_size.x, sizeof(int));
    str_data.cur_line = 0;

    curs_set(1);
    start_color();
    keypad(scr_data.win, TRUE);

    init_pair(clor_pair[color_pair_count++], COLOR_WHITE, COLOR_BLACK);
    attrset(COLOR_PAIR(0));

    write_area.x_end = scr_data.scr_size.x;
    draw_line_numbers(&scr_data,&write_area);
    move(write_area.y_start,write_area.x_start);

    refresh();

    int ch;
    while ((ch = getch()) != 'q') {
        if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)
        {
            int x, y;
            getyx(scr_data.win, y, x);

            if(x > write_area.x_start)
            {
                mvdelch(y, x-1);
                str_data.line_len[str_data.cur_line]--;
            }
            else if(write_area.y_start < y)
            {
                str_data.cur_line--;
                move(y-1, write_area.x_start + str_data.line_len[str_data.cur_line]);
            }
            refresh();
        }
        else if(ch == KEY_ENTER || ch == '\n' || ch == '\r')
        {
            int x, y;
            getyx(scr_data.win, y, x);
            str_data.cur_line++;
            move(y+1, write_area.x_start);
        }
        else if(ch == KEY_RESIZE)
        {
            int cx, cy;
            getyx(scr_data.win, cy, cx);
            getmaxyx(scr_data.win, scr_data.scr_size.y, scr_data.scr_size.x);
            clear();
            write_area.x_end = scr_data.scr_size.x;
            draw_line_numbers(&scr_data, &write_area);
            move(cy, cx);
            refresh();
        }
        else if((ch > 0x21 && ch <= 0x7E) || ch == ' '/*スペース*/)
        {
            int col = str_data.line_len[str_data.cur_line];
            str_data.str_count[str_data.cur_line][col] = ch;
            str_data.line_len[str_data.cur_line]++;
            addch(ch);
            if(str_data.line_len[str_data.cur_line] >= write_area.x_end - write_area.x_start)
            {
                str_data.cur_line++;
                int dummy_x, y;
                getyx(scr_data.win, y, dummy_x);
                move(y+1, write_area.x_start);
            }
        }
    }
    endwin();
    return 0;
}

