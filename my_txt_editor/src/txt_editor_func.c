#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include<stdlib.h>
#include <wctype.h>
#include "txt_editor.h"

/* 画面左端に行番号を描画する。
 * scr_data->scr_start_num を基準に scr_size.y 行分の番号を表示し、
 * カーソル位置が書込み領域より左にある場合は x_start へ補正する。 */
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

/* ウィンドウリサイズ時に呼び出す。
 * 端末サイズを再取得して write_area の各端点・幅・高さを更新し、
 * 画面をクリアして行番号を再描画したあとカーソルを元の位置に戻す。 */
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

/* バックスペースキーの入力を処理する。
 * カーソルが行頭より右にある場合: 1文字削除し line_str_data を詰める。
 * カーソルが行頭にある場合: 前行末尾へカーソルを移動する（文字の結合は行わない）。 */
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

/* 改行キーの入力を処理する。
 * 次の行が書込み領域内であれば次行の行頭へ移動し、
 * 画面下端の場合は現在行の行頭に留まる。now_mouce_line をインクリメントする。 */
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

/* タブキーの入力を処理する。
 * INDENT_RANGE 個のスペースを挿入し、現在行の文字数カウントを同数だけ増やす。 */
void handle_tab(struct editor_state *state) {
    for (int i = 0; i < INDENT_RANGE; i++){
        addch(' ');
    }
    state->str.line[state->mouse.now_mouce_line]+=INDENT_RANGE;
    refresh();
}

/* 通常文字 ch の入力を処理する。
 * カーソル位置に既存文字がある場合は挿入モードで memmove してから insch を使用し、
 * 空きセルへの入力は addch で追記する。行末まで達すると次行の行頭へ折り返す。 */
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

/* マウスイベントを処理する。
 * BUTTON5（下スクロール）: 1行下にスクロールしカーソルを追従させる。
 * BUTTON4（上スクロール）: scr_start_num > 0 のとき1行上にスクロールし、
 *   カーソルが画面外に出た場合は非表示にして is_cur_show を管理する。
 * is_show_box が true のときはスクロール操作を無効化する。 */
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

/* 上スクロール後に画面先頭行へ文字列を再描画する。
 * scr_start_num 行目に文字が存在する場合のみ、write_area 先頭行をクリアしてから
 * line_str_data の該当スライスを addwstr で書き直す。カーソル位置は復元する。 */
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

/* 矢印キー（KEY_UP/DOWN/LEFT/RIGHT）の入力を処理する。
 * UP/DOWN: 行内の x 位置を行長でクランプしつつ now_mouce_line を更新し、
 *   画面端に達した場合は editor_screen_move_line でスクロールする。
 * LEFT: 行頭でさらに左を押すと前行末へ移動する。
 * RIGHT: 行末でさらに右を押すと次行頭へ移動する。 */
void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);

    switch(ch){
        case KEY_UP:{
            if (y > state->write_area.y_start) {
                state->mouse.now_mouce_line--;
                int line_len = state->str.line[state->mouse.now_mouce_line];
                int new_x = (x > state->write_area.x_start + line_len)
                            ? state->write_area.x_start + line_len
                            : x;
                move(y - 1, new_x);
            }
            else if(state->scr.scr_start_num > 0){
              editor_screen_move_line(state, win,-1);
              scr_show_line_str(win, state);
            }
            break;
        }
        case KEY_DOWN:{
            if (y + 1 < state->write_area.y_end) {
                state->mouse.now_mouce_line++;
                int line_len = state->str.line[state->mouse.now_mouce_line];
                int new_x = (x > state->write_area.x_start + line_len)
                            ? state->write_area.x_start + line_len
                            : x;
                move(y + 1, new_x);
            }
            else{
                editor_screen_move_line(state,win,1);
            }
            break;
        }
        case KEY_LEFT:{
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
    }
    refresh();
}
/* 水平線または垂直線を描画する。
 * start_pos と end_pos の x か y どちらかが同じ値のときのみ動作する。
 * all_draw_mode    : 全セルを ACS_VLINE / ACS_HLINE で上書きする。
 * fix_scr_line_damege : 両端から走査し、既に正しい罫線文字があるセルを skip して
 *                       破損箇所だけ修復する（2箇所修復で打ち切り）。 */
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



/* 矩形ボックスを描画する。
 * 初回呼び出し時（is_show_box == false）にカーソルを非表示にして is_show_box を true にする。
 * box の pos・w・h から四隅と四辺を計算し draw_line と ACS_*CORNER で描画する。
 * カーソル位置は描画前後で保存・復元する。 */
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


/* 現在のカーソル行を中心に全行を画面へ再描画する。
 * now_mouce_line から JMP_SET_CUR_POS 行上を描画開始行として scr_start_num を更新し、
 * write_area.h 行分の line_str_data を addwstr で出力する。
 * 文字数が 0 の行は描画をスキップする。最後にカーソルを正しい画面行へ移動する。 */
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

/* path_name のディレクトリエントリを読み込み table に格納する。
 * table は file_browser_area.h 行 × file_browser_area.w バイトのフラット配列を想定する。
 * ファイル名が最大幅を超える場合は末尾を "..." で省略する。
 * 読み込んだエントリ数を state->dir_num に保存する。 */
void load_dir_table(struct editor_state *state,char *table,char *path_name){

    DIR *dir = opendir(path_name);
    if(!dir){
        perror("/");
        exit(EXIT_FAILURE);
    }
    struct dirent *ent;
    int draw_dir_name_line_counter = 0;

    while ((ent = readdir(dir)) && draw_dir_name_line_counter < state->file_browser_area.h) {
        int idx = state->file_browser_area.w * draw_dir_name_line_counter++;
        char name[256] = {0};

        strncpy(name, ent->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        int max_len = state->file_browser_area.w - 1;
        if ((int)strlen(name) > max_len) {
            strncpy(table + idx, name, max_len - 3);
            memcpy(table + idx + max_len - 3, "...", 3);
        } else {
            strncpy(table + idx, name, max_len);
        }

        state->dir_num = draw_dir_name_line_counter;
    }
    closedir(dir);
    return;
}


/* ファイルブラウザのボックス内にディレクトリエントリ一覧を描画する。
 * load_dir_table で作成した table を走査し、空でない行を
 * file_browser_area の座標へ mvaddstr で出力する。 */
void draw_box_inside_dir(struct editor_state *state,char *table){
    for(int i = 0;i < state->file_browser_area.h;i++){
        char *entry = table + i * state->file_browser_area.w;
        if(*entry == '\0') continue;
        mvaddstr(state->file_browser_area.pos.y + i, state->file_browser_area.pos.x, entry);
    }
}

/* ファイルブラウザで選択中の行をカラーペア num でハイライトする。
 * file_select_line を画面 y 座標に変換して mvchgat で属性を変更する。 */
void draw_select_dir_scene_color(struct editor_state *state,int num){
    int ligthing_line = state->file_browser_area.pos.y + state->file_select_line;
    mvchgat(ligthing_line,state->file_browser_area.pos.x,state->file_browser_area.w,A_NORMAL,num,NULL);
}

/* 画面を num 行スクロールしてカーソル行と表示開始行を同量ずらす。
 * num > 0 で下方向、num < 0 で上方向。スクロール後に行番号と
 * 左端の区切り線を再描画する。 */
void editor_screen_move_line(struct editor_state *state,WINDOW *win,int num){
    scrl(num); 

    state->mouse.now_mouce_line += num;
    state->scr.scr_start_num += num;

    struct pos line_start_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_start};
    struct pos line_end_pos = (struct pos){state->write_area.x_start-1,state->scr.scr_size.y};

    draw_line_numbers(&state->scr,&state->write_area);
    draw_line(line_start_pos,line_end_pos,win,all_draw_mode);
}
