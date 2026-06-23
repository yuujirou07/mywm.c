#include <ncurses.h>
#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "txt_editor.h"



int main(void)
{
    struct editor_state state = {0};
    struct box file_browse_box;
    MEVENT mouse_event;
    WINDOW *win;

    setlocale(LC_ALL, "");

    win = initscr();
    if (win == NULL)
        return 1;

    cbreak();
    noecho();
    state.scr.cursor_pos.x = 0;
    state.scr.cursor_pos.y = 0;
    state.scr.scr_start_num = 0;

    state.mouse.now_mouce_line = 0;
    state.mouse.scr_abs_now_pos = (struct pos){0,0};

    getmaxyx(win, state.scr.scr_size.y, state.scr.scr_size.x);

    int total_str_buff_size = state.scr.scr_size.x * state.scr.scr_size.y;
    state.str.line_str_data = calloc(total_str_buff_size * 2, sizeof(wint_t));
    if(state.str.line_str_data == NULL){
        printf("state.str.line_str_data calloc error");
        return 1;
    }
    //行に入っている文字数を入れる
    state.str.line = calloc(total_str_buff_size * 2, sizeof(int));
    if(state.str.line == NULL){
        free(state.str.line_str_data);
        return 1;
    }
    file_browse_box.w = state.scr.scr_size.x / 3;
    file_browse_box.h = state.scr.scr_size.y / 2;
    file_browse_box.pos.x = (state.scr.scr_size.x / 2) - file_browse_box.w / 2;
    file_browse_box.pos.y = state.scr.scr_size.y / 4;

    curs_set(1);
    state.is_cur_show = true;
    state.is_show_box = 0;
    raw();
    start_color();
    scrollok(win, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    keypad(win, TRUE);   

    if (has_colors()) {
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        attrset(COLOR_PAIR(1));
    }

    state.write_area.x_start = 5;
    state.write_area.y_start = 0;
    state.write_area.x_end   = state.scr.scr_size.x-1;
    state.write_area.y_end   = state.scr.scr_size.y;
    state.write_area.w = state.write_area.x_end - state.write_area.x_start;
    state.write_area.h = state.write_area.y_end - state.write_area.y_start;

    struct pos line_start_pos = (struct pos){state.write_area.x_start-1,state.write_area.y_start};
    struct pos line_end_pos = (struct pos){state.write_area.x_start-1,state.scr.scr_size.y};

    draw_line(line_start_pos,line_end_pos,win,all_draw_mode);
    draw_line_numbers(&state.scr, &state.write_area);
    move(state.write_area.y_start, state.write_area.x_start);
    refresh();

    while (1) {
        wint_t ch = 0;
        int input_result = get_wch(&ch);

        if (input_result == ERR){   
            continue;
        }

        if (input_result == KEY_CODE_YES) {
            if (ch == KEY_RESIZE){
                handle_resize(win, &state);
            }
            else if (ch == KEY_BACKSPACE && state.is_cur_show)
                handle_backspace(win, &state);
        }

        if (state.is_show_box == false) {
            if (state.is_cur_show) {
                if (ch == KEY_ENTER || ch == '\n'){
                    handle_newline(win, &state);
                }

                if (ch == '\t'){
                    handle_tab(&state);
                }

                if (input_result == OK && iswprint((wint_t)ch)) {
                    if (ch == 'q'){
                        break;
                    }
                    handle_char_input(win, (wchar_t)ch, &state);
                }

                if(ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
                    handle_input_allow(win,ch,&state);
                }
            } else {
                if (input_result == OK && iswprint((wint_t)ch)) {

                    draw_all_line(win, &state);
                    draw_line_numbers(&state.scr,&state.write_area);

                    state.is_cur_show = true;
                    curs_set(state.is_cur_show ? 1 : 0);

                    handle_char_input(win, (wchar_t)ch, &state);
                    refresh();
                }
            }
        }
        if(ch == CTRL('f')){
            if(state.is_show_box){
                for(int i = 0; i <= file_browse_box.h; i++){
                    mvhline(file_browse_box.pos.y + i, file_browse_box.pos.x, ' ' , file_browse_box.w + 1);
                }
                state.is_show_box = false;
                curs_set(true);
                move(state.mouse.scr_abs_now_pos.y,state.mouse.scr_abs_now_pos.x);
            }
            else{
                getyx(win,state.mouse.scr_abs_now_pos.y,state.mouse.scr_abs_now_pos.x);
                draw_box(&state, file_browse_box, win);
               
                state.is_show_box = true;
            }
            refresh();
        }

        if (ch == KEY_MOUSE){
            handle_mouse(win, &mouse_event, &state);
            draw_line(line_start_pos,line_end_pos,win,all_draw_mode);
        }
    }

    free(state.str.line_str_data);
    free(state.str.line);
    endwin();
    return 0;
}
