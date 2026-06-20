#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>
#include "txt_editor.h"

void draw_line_numbers(struct scr_data *scr_data, struct write_possible_area *area) {
    struct pos cur_pos;
    getyx(stdscr, cur_pos.y, cur_pos.x);

    for (int i = 0; i < scr_data->scr_size.y; i++) {
        char num_str[6];
        int size = snprintf(num_str, 6, "%d", (scr_data->scr_start_num + i) + 1);
        mvprintw(i, 4 - size, "%s", num_str);
    }
    if (cur_pos.x < area->x_start)
        cur_pos.x = area->x_start;
    move(cur_pos.y, cur_pos.x);
}

void handle_resize(WINDOW *win, struct editor_state *state) {
    int cx, cy;
    getyx(win, cy, cx);
    getmaxyx(win, state->scr.scr_size.y, state->scr.scr_size.x);
    clear();
    state->write_area.x_end = state->scr.scr_size.x;
    draw_line_numbers(&state->scr, &state->write_area);
    move(cy, cx);
    refresh();
}

void handle_backspace(WINDOW *win, struct editor_state *state) {
    int x, y;
    getyx(win, y, x);

    if (x > state->write_area.x_start) {
        mvdelch(y, x - 1);
        state->str.line[state->mouse.now_mouce_line]--;
    } else if (state->write_area.y_start < y) {
        move(y - 1, state->str.line[--state->mouse.now_mouce_line] + state->write_area.x_start);
    }
    refresh();
}

void handle_newline(WINDOW *win, struct editor_state *state) {
    int y = getcury(win);
    if (y + 1 < state->write_area.y_end)
        move(y + 1, state->write_area.x_start);
    else
        move(y, state->write_area.x_start);
    state->mouse.now_mouce_line++;
    refresh();
}

void handle_tab(void) {
    for (int i = 0; i < INDENT_RANGE; i++)
        addch(' ');
    refresh();
}

void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state) {
    int x, y;
    getyx(win, y, x);
    addch(ch);

    int write_area_x = x - state->write_area.x_start;
    int idx = state->mouse.now_mouce_line * state->write_area.w + write_area_x;

    state->str.line_str_data[idx] = ch;
    //state->str.line[state->mouse.now_mouce_line]の位置にバックスペースで戻ると文字と重なるので+1する
    state->str.line[state->mouse.now_mouce_line] = write_area_x + 1;

    if (x >= state->write_area.x_end){
        move(y + 1, state->write_area.x_start);
    }
    refresh();
}

void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state) {
    if (getmouse(event) != OK)
        return;

    int x, y;
    getyx(win, y, x);

    if (event->bstate & BUTTON5_PRESSED) {
        scroll(win);
        if (state->scr.scr_start_num < state->mouse.now_mouce_line){
            move(y - 1, x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num++;

    } else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0) {
        scrl(-1);
        if(state->scr.scr_start_num + state->write_area.y_end >= state->mouse.now_mouce_line){
            move(y + 1, x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num--;
        scr_show_line_str(win,state);

        //マウスカーソルの画面内判定
        if(state->scr.scr_start_num <= state->mouse.now_mouce_line){
            //state->is_cur_showがtrueに代わる1度だけmoveを実行する
            if(state->is_cur_show == false){
                move(state->write_area.y_start, x);
                state->is_cur_show = true;
            }
        }
    }
    curs_set(state->is_cur_show);

    draw_line_numbers(&state->scr, &state->write_area);
}

void scr_show_line_str(WINDOW *win,struct editor_state *state){
    if(state->str.line[state->scr.scr_start_num] > 0){

        wint_t tmp[state->write_area.x_end];
        memset(tmp,0,state->write_area.y_start * sizeof(wint_t));

        int line_idx = state->scr.scr_start_num * state->write_area.x_end;
        memcpy(tmp,&state->str.line_str_data[line_idx],state->write_area.x_end * sizeof(wint_t));

        int x;
        int y;
        getyx(win,y,x);
        move(state->write_area.y_start,state->write_area.x_start);
        addwstr((wchar_t *)tmp);
        move(y,x);
    }
}
