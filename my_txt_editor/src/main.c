#include <ncurses.h>
#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "txt_editor.h"


int main(int argc, char *argv[])
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

    getmaxyx(win, state.scr.scr_size.y, state.scr.scr_size.x);

    int total_str_buff_size = state.scr.scr_size.x * state.scr.scr_size.y;
    state.str.line_str_data = calloc(total_str_buff_size * 2, sizeof(wint_t));
    //行に入っている文字数を入れる
    state.str.line = calloc(total_str_buff_size * 2, sizeof(int));
    file_browse_box.w = state.scr.scr_size.x / 3;
    file_browse_box.h = state.scr.scr_size.y / 2;
    file_browse_box.pos.x = (state.scr.scr_size.x / 2) - file_browse_box.w / 2;
    file_browse_box.pos.y = state.scr.scr_size.y / 4;

    state.is_cur_show = curs_set(1);
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
    state.write_area.x_end   = state.scr.scr_size.x;
    state.write_area.y_end   = state.scr.scr_size.y;
    state.write_area.w = state.write_area.x_end - state.write_area.x_start;
    state.write_area.h = state.write_area.y_end - state.write_area.y_start;

    draw_line_numbers(&state.scr, &state.write_area);
    move(state.write_area.y_start, state.write_area.x_start);
    refresh();

    while (1) {
        wint_t ch = 0;
        int input_result = get_wch(&ch);

        if (input_result == ERR){
            break;
        }

        if (input_result == KEY_CODE_YES) {
            if (ch == KEY_RESIZE){
                handle_resize(win, &state);
            }
            else if (ch == KEY_BACKSPACE)
                handle_backspace(win, &state);
        }

        if (ch == KEY_ENTER || ch == '\n'){
            handle_newline(win, &state);
        }

        if (ch == '\t'){
            handle_tab();
        }

        if (input_result == OK && iswprint((wint_t)ch)) {
            if (ch == 'q'){
                break;
            }
            handle_char_input(win, (wchar_t)ch, &state);
        }

        if (ch == KEY_MOUSE){
            handle_mouse(win, &mouse_event, &state);
        }
    }

    free(state.str.line_str_data);
    free(state.str.line);
    endwin();
    return 0;
}
