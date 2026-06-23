#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include<stdlib.h>
#include <wctype.h>
#include "txt_editor.h"

void draw_line_numbers(struct scr_data *scr_data, struct write_possible_area *area) {
    struct pos cur_pos;
    getyx(stdscr, cur_pos.y, cur_pos.x);

    for (int i = 0; i < scr_data->scr_size.y; i++) {
        char num_str[6];
    
        int size = snprintf(num_str, 6, "%d", (scr_data->scr_start_num + i) + 1);
        mvhline(i,0,' ',4);
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

    state->write_area.x_end = state->scr.scr_size.x - 1;
    state->write_area.y_end = state->scr.scr_size.y;
    state->write_area.w    = state->write_area.x_end - state->write_area.x_start;
    state->write_area.h    = state->write_area.y_end - state->write_area.y_start;

    clear();

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

        int line_base = state->mouse.now_mouce_line * state->write_area.w;
        int del_pos   = (x - 1) - state->write_area.x_start;
        int new_len   = state->str.line[state->mouse.now_mouce_line];
        
        int move_count = new_len - del_pos;
        if (move_count > 0)
            memmove(&state->str.line_str_data[line_base + del_pos],
                    &state->str.line_str_data[line_base + del_pos + 1],
                    move_count * sizeof(wint_t));

        state->str.line_str_data[line_base + new_len] = 0;

    } else if (state->write_area.y_start < y) {
        int prev_x = state->str.line[--state->mouse.now_mouce_line] + state->write_area.x_start;
        if (prev_x >= state->write_area.x_end)
            prev_x = state->write_area.x_end ;
        move(y - 1, prev_x);
    }
    refresh();
}

void handle_newline(WINDOW *win, struct editor_state *state) {
    int y = getcury(win);
    if (y + 1 < state->write_area.y_end){
        move(y + 1, state->write_area.x_start);
    }
    else{
        move(y, state->write_area.x_start);
    }
    state->mouse.now_mouce_line++;
    refresh();
}

void handle_tab(struct editor_state *state) {
    for (int i = 0; i < INDENT_RANGE; i++){
        addch(' ');
    }
    state->str.line[state->mouse.now_mouce_line]+=INDENT_RANGE;
    refresh();
}

void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);

    int writing_area = x - state->write_area.x_start;
    int idx = state->mouse.now_mouce_line * state->write_area.w + writing_area;

    if(state->str.line_str_data[idx] != 0){
       // 挿入
        int line_len = state->str.line[state->mouse.now_mouce_line];
        int insert_count = line_len - writing_area;
        if (insert_count > 0)
            memmove(&state->str.line_str_data[idx + 1],
                    &state->str.line_str_data[idx],
                    insert_count * sizeof(wint_t));
        insch(ch);
        move(y, x + 1);
    }
    else{
        addch(ch);
    }

    state->str.line_str_data[idx] = ch;                                                          
    //state->str.line[state->mouse.now_mouce_line]の位置にバックスペースで戻ると文字と重なるので+1する
    state->str.line[state->mouse.now_mouce_line]++;

    if (x >= state->write_area.x_end-1){
        move(y + 1, state->write_area.x_start);
        state->mouse.now_mouce_line++;
    }
    refresh();
}

void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state) {
    if (getmouse(event) != OK){
        return;
    }

    int x, y;
    getyx(win, y, x);

    if (event->bstate & BUTTON5_PRESSED && state->is_show_box == false) {
        scroll(win);
        
        if (state->scr.scr_start_num < state->mouse.now_mouce_line){
            move(y - 1, x);
        }
        else{
            state->is_cur_show = false;
        }
        
        state->scr.scr_start_num++;

    } else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0  && state->is_show_box == false) {
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
    draw_line_numbers(&state->scr, &state->write_area);
    curs_set(state->is_cur_show ? 1 : 0);
    refresh();
}

void scr_show_line_str(WINDOW *win,struct editor_state *state){
    if(state->str.line[state->scr.scr_start_num] > 0){

        wint_t tmp[state->write_area.w + 1];
        memset(tmp, 0, (state->write_area.w + 1) * sizeof(wint_t));

        int line_idx = state->scr.scr_start_num * state->write_area.w;
        memcpy(tmp, &state->str.line_str_data[line_idx], state->write_area.w * sizeof(wint_t));
        tmp[state->write_area.w] = 0;

        int x;
        int y;

        getyx(win, y, x);
        mvhline(state->write_area.y_start, state->write_area.x_start, ' ', state->write_area.w);
        move(state->write_area.y_start, state->write_area.x_start);
        addwstr((wchar_t *)tmp);
        move(y, x);
    }
}

void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);

    switch(ch){
        case KEY_UP:
            if (y > state->write_area.y_start) {
                state->mouse.now_mouce_line--;
                int line_len = state->str.line[state->mouse.now_mouce_line];
                int new_x = (x > state->write_area.x_start + line_len)
                            ? state->write_area.x_start + line_len
                            : x;
                move(y - 1, new_x);
            }
            break;
        case KEY_DOWN:
            if (y + 1 < state->write_area.y_end) {
                state->mouse.now_mouce_line++;
                int line_len = state->str.line[state->mouse.now_mouce_line];
                int new_x = (x > state->write_area.x_start + line_len)
                            ? state->write_area.x_start + line_len
                            : x;
                move(y + 1, new_x);
            }
            break;
        case KEY_LEFT:
            if (x > state->write_area.x_start)
                move(y, x - 1);
            else if (y > state->write_area.y_start) {
                state->mouse.now_mouce_line--;
                move(y - 1, state->write_area.x_start + state->str.line[state->mouse.now_mouce_line]);
            }
            break;
        case KEY_RIGHT:
            if (x < state->write_area.x_start + state->str.line[state->mouse.now_mouce_line])                                                                              
                move(y, x + 1);
            else if (y + 1 < state->write_area.y_end) {
                state->mouse.now_mouce_line++;
                move(y + 1, state->write_area.x_start);
            }  
            break;
    }
    refresh();
}
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode){
    int x;
    int y;
    getyx(win,y,x);
    switch(mode){
        case all_draw_mode:
        {
            if(start_pos.x == end_pos.x){
                int range = abs(start_pos.y - end_pos.y);
                for(int i=0;i<range;i++){
                    move(start_pos.y + i,start_pos.x);
                    addch(ACS_VLINE);
                }
            }
            else if(start_pos.y == end_pos.y){
                int range = abs(start_pos.x - end_pos.x);

                for(int i=0;i<range;i++){
                    move(start_pos.y,start_pos.x + i);
                    addch(ACS_HLINE);
                }
            }
            break;
        }

        //両サイドから確認していく方式
        case fix_scr_line_damege:
        {
            if(start_pos.x == end_pos.x){

                int range = abs(start_pos.y - end_pos.y);
                int is_fixed_all = 0;

                for(int i=0;i<range;i++){
                    
                    chtype ch1 = mvinch(start_pos.y + i, start_pos.x);
                    chtype ch2 = mvinch(start_pos.y + range - i, start_pos.x);

                    if (ch1 != ACS_VLINE) {
                        move(start_pos.y + i,start_pos.x);
                        addch(ACS_VLINE);
                    }
                    else{
                        is_fixed_all++;
                    }

                    if (ch2 != ACS_VLINE) {

                        move(start_pos.y + range - i,start_pos.x);
                        addch(ACS_VLINE);
                    }
                    else{
                        is_fixed_all++;
                    }

                    if(is_fixed_all >=2){
                        break;
                    }
                }
            }
            else if(start_pos.y == end_pos.y){
                int range = abs(start_pos.x - end_pos.x);
                int is_fixed_all = 0;

                for(int i=0;i<range;i++){

                    chtype ch1 = mvinch(start_pos.y, start_pos.x + i);
                    chtype ch2 = mvinch(start_pos.y, start_pos.x + range - i);

                    if(ch1 != ACS_HLINE){
                        move(start_pos.y, start_pos.x + i);
                        addch(ACS_HLINE);
                    }
                    else{
                        is_fixed_all++;
                    }

                    if(ch2 != ACS_HLINE){
                        move(start_pos.y, start_pos.x + range - i);
                        addch(ACS_HLINE);
                    }
                    else{
                        is_fixed_all++;
                    }
                     if(is_fixed_all >=2){
                        break;
                    }
                }
            }
            break;
        }
    }
    move(y,x);
}



void draw_box(struct editor_state *state, struct box box, WINDOW *win){
    if(state->is_show_box == false){

        //ボックス表示中は打てない設定
        state->is_cur_show = false;
        state->is_show_box = true;

        curs_set(state->is_cur_show ? 1 : 0);
    }

    int cur_x;
    int cur_y;

    getyx(win,cur_y,cur_x);

    int x = box.pos.x;
    int y = box.pos.y;
    int w = box.w; 
    int h = box.h;
 
    struct pos top_left     = {x,     y};
    struct pos top_right    = {x + w, y}; 
    struct pos bottom_left  = {x,     y + h};
    struct pos bottom_right = {x + w, y + h};

    draw_line(top_left,    bottom_left,  win, all_draw_mode); // 左
    draw_line(top_right,   bottom_right, win, all_draw_mode); // 右
    draw_line(top_left,    top_right,    win, all_draw_mode); // 上
    draw_line(bottom_left, bottom_right, win, all_draw_mode); // 下

    mvaddch(y,     x,     ACS_ULCORNER);
    mvaddch(y,     x + w, ACS_URCORNER);
    mvaddch(y + h, x,     ACS_LLCORNER);
    mvaddch(y + h, x + w, ACS_LRCORNER);

    move(cur_y,cur_x);
    refresh();
}

void draw_all_line(WINDOW *win,struct editor_state *state){
    int x = getcurx(win);

    //描画開始位置
    int start_draw_line_num = (state->mouse.now_mouce_line > JMP_SET_CUR_POS) 
        ? state->mouse.now_mouce_line - JMP_SET_CUR_POS : 0;

    state->scr.scr_start_num = start_draw_line_num;

    for(int i = start_draw_line_num;i < (start_draw_line_num + state->write_area.h);i++){

        if(state->str.line[i] > 0){

            int allocate_cell_size = state->write_area.w + 1;
            int line_idx = i * state->write_area.w;
            int scr_pos_y = i - start_draw_line_num;
            
            wint_t tmp[allocate_cell_size];
            memset(tmp, 0, allocate_cell_size * sizeof(wint_t));

            memcpy(tmp, &state->str.line_str_data[line_idx], state->write_area.w * sizeof(wint_t));
            tmp[state->write_area.w] = 0;

            mvhline(scr_pos_y,state->write_area.x_start, ' ', state->write_area.w);
            move(scr_pos_y, state->write_area.x_start);
            addwstr((wchar_t *)tmp);
        }
    }
    int scr_pos_y = state->mouse.now_mouce_line - state->scr.scr_start_num;
    move(scr_pos_y, x);  
}