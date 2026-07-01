#include <ncurses.h>
#include <string.h>
#include <wctype.h>
#include "txt_editor.h"

// handle_resize(): 端末サイズ変更後に画面サイズと書き込み領域を更新し、
// 行番号を描き直してカーソル位置を復元する。
// 引数: win=操作対象のncursesウィンドウ、state=更新するエディタ状態、start_pos/end_pos=区切り線端点の更新先。
// 返り値: なし。
static int limit = 0;

void handle_resize(WINDOW *win, struct editor_state *state,struct pos *start_pos,struct pos *end_pos){

    int cx, cy;
    getyx(win, cy, cx);
    getmaxyx(win, state->scr.scr_size.y, state->scr.scr_size.x);

    state->write_area.y_start = 0;
    state->write_area.x_end = state->scr.scr_size.x - 1;
    state->write_area.y_end = state->scr.scr_size.y;
    if(state->settings_data->show_status_bar){
        state->status_bar->w = state->scr.scr_size.x;
        state->status_bar->h = 1;
        if(state->settings_data->bar_side_state == top){
            state->status_bar->pos = (struct pos){0, 1};
            state->write_area.y_start = state->status_bar->pos.y + state->status_bar->h;
        }
        else{
            state->status_bar->pos = (struct pos){0, state->scr.scr_size.y - 1};
            state->write_area.y_end = state->status_bar->pos.y;
        }
    }
    state->write_area.w    = state->write_area.x_end - state->write_area.x_start;
    state->write_area.h    = state->write_area.y_end - state->write_area.y_start;

    *start_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_start};
    *end_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_end};

    draw_edit_screen_base(state, win,*start_pos,*end_pos);

    clear();
    move(cy, cx);
    refresh();
}

// handle_backspace(): カーソル左の1文字を削除し、行バッファを左へ詰める。
// 行頭では前の行末へカーソルを移動する。
// 引数: win=現在カーソル位置を持つウィンドウ、state=文字バッファと行情報。
// 返り値: なし。
void handle_backspace(WINDOW *win, struct editor_state *state) {
    int x, y;
    getyx(win, y, x);
    int line = state->mouse.now_mouce_line;
    int col_limit = editor_col_limit(state);
    if(line < 0 || line >= editor_line_limit(state) || col_limit <= 0){
        return;
    }

    if (x > state->write_area.x_start) {
        int del_pos   = (x - 1) - state->write_area.x_start;
        if(del_pos < 0 || del_pos >= col_limit || del_pos >= state->str.line[line]){
            return;
        }
        mvdelch(y, x - 1);
        state->str.line[line]--;

        int line_base = line * state->str.col_capacity;
        int new_len   = state->str.line[line];

        int move_count = new_len - del_pos;
        if (move_count > 0)
            memmove(&state->str.line_str_data[line_base + del_pos],
                    &state->str.line_str_data[line_base + del_pos + 1],
                    move_count * sizeof(wint_t));

        state->str.line_str_data[line_base + new_len] = 0;

    } else if (state->write_area.y_start < y && state->mouse.now_mouce_line > 0) {
        state->mouse.now_mouce_line--;
        move(y - 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line, state->write_area.x_end));
    }
    refresh();
}

// handle_newline(): 次の画面行の先頭へカーソルを移動し、
// 編集中の論理行番号(now_mouce_line)を1つ進める。
// 引数: win=改行操作を反映するウィンドウ、state=カーソル行と書き込み領域。
// 返り値: なし。
void handle_newline(WINDOW *win, struct editor_state *state) {
    if(state->mouse.now_mouce_line + 1 >= editor_line_limit(state)){
        return;
    }
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

// handle_tab(): INDENT_RANGE個の空白を挿入し、現在行の文字数へ反映する。
// 引数: state=現在行の文字数を更新するエディタ状態。
// 返り値: なし。
void handle_tab(struct editor_state *state) {
    int line = state->mouse.now_mouce_line;
    int col_limit = editor_col_limit(state);
    if(line < 0 || line >= editor_line_limit(state) || col_limit <= 0){
        return;
    }
    if(state->str.line[line] >= col_limit){
        return;
    }
    int add_count = state->settings_data->indent_range;
    if(state->str.line[line] + add_count > col_limit){
        add_count = col_limit - state->str.line[line];
    }
    for (int i = 0; i < add_count; i++){
        addch(' ');
    }
    state->str.line[line] += add_count;
    refresh();
}

// handle_char_input(): 通常文字をカーソル位置へ挿入する。
// 既存文字がある場所では後続文字を右へずらしてから書き込む。
// 引数: win=描画先ウィンドウ、ch=挿入するwide文字、state=編集バッファ。
// 返り値: なし。
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);

    int line = state->mouse.now_mouce_line;
    int col_limit = editor_col_limit(state);
    if(line < 0 || line >= editor_line_limit(state) || col_limit <= 0){
        return;
    }
    x = editor_clamp_int(x, state->write_area.x_start, state->write_area.x_start + col_limit - 1);
    move(y, x);

    int writing_area = x - state->write_area.x_start;
    if(writing_area < 0 || writing_area >= col_limit){
        return;
    }
    int idx = line * state->str.col_capacity + writing_area;
    int line_len = editor_line_len(state, line);
    int char_width = wcwidth(ch);
    if(char_width < 1){
        char_width = 1;
    }
    if(writing_area + char_width > col_limit){
        return;
    }

    if(writing_area < line_len){
        if(line_len + char_width > col_limit){
            return;
        }
        int insert_count = line_len - writing_area;
        if (insert_count > 0)
            memmove(&state->str.line_str_data[idx + char_width],
                    &state->str.line_str_data[idx],
                    insert_count * sizeof(wint_t));
    }

    state->str.line_str_data[idx] = ch;
    for(int i = 1; i < char_width; i++){
        state->str.line_str_data[idx + i] = 0;
    }
    if(state->str.line[line] <= writing_area){
        state->str.line[line] = writing_area + char_width;
    }
    else if(state->str.line[line] < col_limit){
        state->str.line[line] += char_width;
    }

    draw_editor_buffer_line(state, line, y);
    move(y, x + char_width);
    if (x >= state->write_area.x_end-1 && state->mouse.now_mouce_line + 1 < editor_line_limit(state)){
        move(y + 1, state->write_area.x_start);
        state->mouse.now_mouce_line++;
    }
    refresh();
}

// handle_mouse(): マウスホイールで表示開始行を上下に動かし、
// カーソルが表示範囲外へ出る場合は一時的に非表示にする。
// 引数: win=スクロールするウィンドウ、event=getmouse()の格納先、state=表示位置とカーソル状態。
// 返り値: なし。
void handle_mouse(WINDOW *win, MEVENT *event, struct editor_state *state) {
    if (getmouse(event) != OK){
        return;
    }

    int x, y;
    getyx(win, y, x);
    y = editor_clamp_int(y, state->write_area.y_start, state->write_area.y_end - 1);

    int line_limit = editor_line_limit(state);
    bool can_scroll_down = state->scr.scr_start_num + state->write_area.h < line_limit;

    if (event->bstate & BUTTON5_PRESSED && state->screen_state == edit_screen && can_scroll_down) {
        wsetscrreg(win, state->write_area.y_start, state->write_area.y_end - 1);
        wscrl(win, 1);
        wsetscrreg(win, 0, state->scr.scr_size.y - 1);

        if (state->scr.scr_start_num < state->mouse.now_mouce_line){
            move(editor_clamp_int(y - 1, state->write_area.y_start, state->write_area.y_end - 1), x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num++;
        scr_show_line_str_down(win, state);

    } else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0 && state->screen_state == edit_screen) {
        wsetscrreg(win, state->write_area.y_start, state->write_area.y_end - 1);
        wscrl(win, -1);
        wsetscrreg(win, 0, state->scr.scr_size.y - 1);
        if(state->scr.scr_start_num + state->write_area.y_end >= state->mouse.now_mouce_line){
            move(editor_clamp_int(y + 1, state->write_area.y_start, state->write_area.y_end - 1), x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num--;
        scr_show_line_str(win,state);

        if(state->scr.scr_start_num <= state->mouse.now_mouce_line){
            if(state->is_cur_show == false){
                move(state->write_area.y_start, x);
                state->is_cur_show = true;
            }
        }
    }
    draw_line_numbers(state);
    curs_set(state->is_cur_show ? 1 : 0);
    refresh();
}

// handle_input_allow(): 矢印キー入力を処理し、行長を超えない位置へカーソルを移動する。
// 画面端ではスクロールしながら表示内容を補う。
// 引数: win=カーソル移動対象のウィンドウ、ch=KEY_UP/DOWN/LEFT/RIGHT、state=行長と表示位置。
// 返り値: なし。
void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);
    int line_limit = editor_line_limit(state);
    if(line_limit <= 0){
        return;
    }

    switch(ch){
        case KEY_UP:{
            if (y > state->write_area.y_start && state->mouse.now_mouce_line > 0) {
                state->mouse.now_mouce_line--;
                move(y - 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line, x));
            }
            else if(state->scr.scr_start_num > 0){
              editor_screen_move_line(state, win,-1);
              scr_show_line_str(win, state);
            }
            break;
        }
        case KEY_DOWN:{
            if(state->mouse.now_mouce_line + 1 >= line_limit){
                break;
            }
            if (y + 1 < state->write_area.y_end) {
                state->mouse.now_mouce_line++;
                move(y + 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line, x));
            }
            else{
                editor_screen_move_line(state,win,1);
                scr_show_line_str_down(win, state);
            }
            break;
        }
        case KEY_LEFT:{
            if (x > state->write_area.x_start)
                move(y, x - 1);
            else if (y > state->write_area.y_start && state->mouse.now_mouce_line > 0) {
                state->mouse.now_mouce_line--;
                move(y - 1, state->write_area.x_start + editor_line_len(state, state->mouse.now_mouce_line));
            }

            break;
        }
        case KEY_RIGHT:{
            int line = state->mouse.now_mouce_line;
            if(line < 0 || line >= line_limit){
                break;
            }
            int line_len = editor_line_len(state, line);
            if (x < state->write_area.x_start + line_len)
                move(y, x + 1);
            else if (y + 1 < state->write_area.y_end && state->mouse.now_mouce_line + 1 < line_limit) {
                state->mouse.now_mouce_line++;
                move(y + 1, state->write_area.x_start);
            }
            break;
        }
    }
    refresh();
}

void set_line_limit(int line_limit){
    limit = line_limit;
}
int get_line_limit(){
    return limit;
}
