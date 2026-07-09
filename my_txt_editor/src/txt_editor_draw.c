#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "txt_editor.h"

// line_draw_info(): 線の向きから描画範囲・移動量・罫線文字を決める。
// 引数: start_pos/end_pos=線の端点、range/step_x/step_y/line_ch=計算結果の書き込み先。
// 返り値: 縦線または横線ならtrue、斜め線ならfalse。
static bool line_draw_info(struct pos start_pos, struct pos end_pos,
                           int *range, int *step_x, int *step_y, chtype *line_ch){
    if(start_pos.x == end_pos.x){
        *range = abs(start_pos.y - end_pos.y);
        *step_x = 0;
        *step_y = 1;
        *line_ch = ACS_VLINE;
        return true;
    }
    if(start_pos.y == end_pos.y){
        *range = abs(start_pos.x - end_pos.x);
        *step_x = 1;
        *step_y = 0;
        *line_ch = ACS_HLINE;
        return true;
    }
    return false;
}

// draw_full_line(): 指定方向へrangeセル分の罫線を描画する。
// 引数: start_pos=開始座標、range=描画セル数、step_x/step_y=1セルごとの移動量、line_ch=描画文字。
// 返り値: なし。
static void draw_full_line(struct pos start_pos, int range, int step_x, int step_y, chtype line_ch){
    for(int i = 0; i < range; i++){
        move(start_pos.y + i * step_y, start_pos.x + i * step_x);
        addch(line_ch);
    }
}

// fix_line_cell(): 指定セルの罫線が壊れていれば描き直す。
// 引数: y/x=確認する座標、line_ch=期待する罫線文字。
// 返り値: 既に正しい罫線だったら1、描き直したら0。
static int fix_line_cell(int y, int x, chtype line_ch){
    if(mvinch(y, x) == line_ch){
        return 1;
    }
    move(y, x);
    addch(line_ch);
    return 0;
}

// fix_line_damage(): 両端から罫線を確認し、壊れたセルだけ補修する。
// 引数: start_pos=開始座標、range=確認セル数、step_x/step_y=1セルごとの移動量、line_ch=期待する罫線文字。
// 返り値: なし。
static void fix_line_damage(struct pos start_pos, int range, int step_x, int step_y, chtype line_ch){
    int is_fixed_all = 0;

    for(int i = 0; i < range; i++){
        is_fixed_all += fix_line_cell(start_pos.y + i * step_y,
                                      start_pos.x + i * step_x,
                                      line_ch);
        is_fixed_all += fix_line_cell(start_pos.y + (range - i) * step_y,
                                      start_pos.x + (range - i) * step_x,
                                      line_ch);

        if(is_fixed_all >= 2){
            break;
        }
    }
}

// draw_editor_buffer_line(): 指定した論理行を画面上の1行へ描画する。
// 引数: state=文字バッファと書き込み領域、line=描画する論理行、screen_y=描画先の画面y座標。
// 返り値: なし。
void draw_editor_buffer_line(struct editor_state *state, int line, int screen_y){
    int col_limit = editor_col_limit(state);
    if(screen_y < state->write_area.y_start || screen_y >= state->write_area.y_end || col_limit <= 0){
        return;
    }

    mvhline(screen_y, state->write_area.x_start, ' ', col_limit);
    if(line < 0 || line >= editor_line_limit(state)) return;
    if(state->str.line[line] <= 0) return;

    // line_str_dataは画面セル位置に合わせて格納している。
    // addwstrで詰めて描くと2桁幅文字の後ろでズレるため、
    // セルごとのx座標へ1文字ずつ描く。
    int line_base = line * state->str.col_capacity;
    int max_col = state->str.line[line];
    if(max_col > col_limit) max_col = col_limit;

    for(int col = 0; col < max_col; col++){
        wint_t cell = state->str.line_str_data[line_base + col];
        if(cell == 0) continue;

        wchar_t ch = (wchar_t)cell;
        mvaddnwstr(screen_y, state->write_area.x_start + col, &ch, 1);
    }
}

// draw_line_numbers(): 表示開始行(scr_start_num)を基準に、左端へ行番号を描画する。
// 描画後は元のカーソル位置へ戻す。
// 引数: state=画面サイズ・表示開始行・書き込み領域を持つエディタ状態。
// 返り値: なし。
void draw_line_numbers(struct editor_state *state) {
    struct pos cur_pos;
    getyx(stdscr, cur_pos.y, cur_pos.x);
    struct scr_data *scr_data = &state->scr;
    struct write_possible_area *area = &state->write_area;
    int line_number_space = state->settings_data->line_number_space;

    for (int i = 0; i < area->h; i++) {
        char num_str[6];

        int size = snprintf(num_str, 6, "%d", (scr_data->scr_start_num + i) + 1);
        int draw_x = line_number_space - size;
        if(draw_x < 0){
            draw_x = 0;
        }
        mvhline(area->y_start + i, 0, ' ', line_number_space);
        mvprintw(area->y_start + i, draw_x, "%s", num_str);
    }
    if (cur_pos.x < area->x_start)
        cur_pos.x = area->x_start;
    move(cur_pos.y, cur_pos.x);
}

// scr_show_line_str(): 上スクロール後に、新しく見えた先頭行の文字列を描き直す。
// 引数: win=描画先ウィンドウ、state=表示開始行と文字バッファ。
// 返り値: なし。
void scr_show_line_str(WINDOW *win,struct editor_state *state){
    int x;
    int y;

    getyx(win, y, x);
    draw_editor_buffer_line(state, state->scr.scr_start_num, state->write_area.y_start);
    move(y, x);
}

// scr_show_line_str_down(): 下スクロール後に、新しく見えた最終行の文字列を描き直す。
// 引数: win=描画先ウィンドウ、state=表示範囲と文字バッファ。
// 返り値: なし。
void scr_show_line_str_down(WINDOW *win, struct editor_state *state){
    int last_line = state->scr.scr_start_num + state->write_area.h - 1;
    int x;
    int y;

    getyx(win, y, x);
    draw_editor_buffer_line(state, last_line, state->write_area.y_end - 1);
    move(y, x);
}

// draw_line(): start_posからend_posまで水平線または垂直線を描く。
// fix_scr_line_damegeでは壊れた罫線だけを検査して補修する。
// 引数: start_pos=開始座標、end_pos=終了座標、win=描画先、mode=全描画か補修か。
// 返り値: なし。
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode){

    int x;
    int y;
    getyx(win,y,x);
    int range;
    int step_x;
    int step_y;
    chtype line_ch;

    if(!line_draw_info(start_pos, end_pos, &range, &step_x, &step_y, &line_ch)){
        move(y,x);
        return;
    }

    switch(mode){
        case all_draw_mode:
            draw_full_line(start_pos, range, step_x, step_y, line_ch);
            break;

        case fix_scr_line_damege:
            fix_line_damage(start_pos, range, step_x, step_y, line_ch);
            break;
    }
    move(y,x);
}

// draw_box(): 指定された矩形領域の枠線と四隅を描画し、表示中はカーソルを隠す。
// 引数: state=カーソル表示状態、box=描く矩形、win=描画先ウィンドウ。
// 返り値: なし。
void draw_box(struct box box, WINDOW *win){

    int x = box.pos.x;
    int y = box.pos.y;
    int w = box.w;
    int h = box.h;

    struct pos top_left     = {x,     y};
    struct pos top_right    = {x + w, y};
    struct pos bottom_left  = {x,     y + h};
    struct pos bottom_right = {x + w, y + h};

    draw_line(top_left,    bottom_left,  win, all_draw_mode);
    draw_line(top_right,   bottom_right, win, all_draw_mode);
    draw_line(top_left,    top_right,    win, all_draw_mode);
    draw_line(bottom_left, bottom_right, win, all_draw_mode);

    mvaddch(y,     x,     ACS_ULCORNER);
    mvaddch(y,     x + w, ACS_URCORNER);
    mvaddch(y + h, x,     ACS_LLCORNER);
    mvaddch(y + h, x + w, ACS_LRCORNER);

}

// draw_all_line(): 現在行が見える位置を基準に、編集バッファから画面全体を再描画する。
// 引数: win=描画先ウィンドウ、state=現在行・表示開始行・文字バッファ。
// 返り値: なし。
void draw_all_line(WINDOW *win,struct editor_state *state){
    int x = getcurx(win);

    int start_draw_line_num = (state->mouse.now_mouce_line > state->settings_data->jmp_set_cur_pos)
        ? state->mouse.now_mouce_line - state->settings_data->jmp_set_cur_pos : 0;

    state->scr.scr_start_num = start_draw_line_num;

    for(int i = start_draw_line_num;i < (start_draw_line_num + state->write_area.h);i++){

        int scr_pos_y = i - start_draw_line_num;
        draw_editor_buffer_line(state, i, state->write_area.y_start + scr_pos_y);
    }
    int scr_pos_y = state->mouse.now_mouce_line - state->scr.scr_start_num;
    move(state->write_area.y_start + scr_pos_y, x);
}

// draw_now_path_name(): ファイルブラウザ上部に現在ディレクトリのパスを表示する。
// 引数: file_browse_box=表示位置と幅、path_name=表示するパス文字列。
// 返り値: なし。
void draw_now_path_name(struct box file_browse_box,char *path_name){
    int x = file_browse_box.pos.x;
    int y = file_browse_box.pos.y;
    int w = file_browse_box.w;
    if(w <= 1 || y < 2){
        return;
    }

    mvaddch(y - 2, x, ACS_ULCORNER);
    for (int i = 1; i < w; i++)
        mvaddch(y - 2, x + i, ACS_HLINE);
    mvaddch(y - 2, x + w, ACS_URCORNER);

    mvaddch(y - 1, x, ACS_VLINE);
    int inner_w = w - 1;
    int len = (int)strlen(path_name);
    if (len > inner_w && inner_w > 3) {
        addstr("...");
        int diff = len - inner_w+strlen("...");
        char *path_start_ptr = strchr(&path_name[diff],'/');
        if(path_start_ptr == NULL){
            path_start_ptr = &path_name[diff];
        }
        mvaddnstr(y - 1, x +4,path_start_ptr, inner_w - 3);

    } else if(len > inner_w) {
        mvaddnstr(y - 1, x + 1, path_name, inner_w);
    } else {
        mvprintw(y - 1, x + 1, "%-*s", inner_w, path_name);
    }
    mvaddch(y - 1, x + w, ACS_VLINE);
    mvaddch(y, x, ACS_LTEE);
    mvaddch(y, x + w, ACS_RTEE);
}

// draw_edit_screen_base(): 編集画面の固定要素である区切り線と行番号を描画する。
// 引数: state=行番号情報、win=描画先、start_pos/end_pos=区切り線の端点。
// 返り値: なし。
void draw_edit_screen_base(struct editor_state *state,WINDOW *win,struct pos start_pos,struct pos end_pos){
    if(state->settings_data->draw_split_line){
        draw_line(start_pos,end_pos,win,all_draw_mode);
    }
    if(state->settings_data->show_status_bar){
        draw_status_bar_line(state,*state->status_bar,win);
        draw_line_status(state,win);
        if(state->settings_data->show_status_bar){
            draw_status_bar_path(state,win);
        }
    }
    draw_line_numbers(state);
}

// draw_box_inside_dir(): load_dir_table()が作ったディレクトリエントリ一覧を
// ファイルブラウザの内側へ描画する。
// 引数: state=ファイルブラウザ領域、table=固定幅で詰めたディレクトリエントリ一覧。
// 返り値: なし。
void draw_box_inside_dir(struct editor_state *state,char *table){
    if(state->file_browser_area.w <= 0 || state->file_browser_area.h <= 0){
        return;
    }
    char clear[state->file_browser_area.w + 1];
    memset(clear,' ',state->file_browser_area.w * sizeof(char));
    clear[state->file_browser_area.w] = '\0';
    for(int i = 0;i < state->file_browser_area.h;i++){
        mvaddstr(state->file_browser_area.pos.y + i, state->file_browser_area.pos.x,clear);
        char *entry = table + i * state->file_browser_area.w;
        if(*entry == '\0') continue;
        mvaddstr(state->file_browser_area.pos.y + i, state->file_browser_area.pos.x+1,entry);
    }
}

// draw_select_dir_scene_color(): ファイルブラウザの選択行に指定カラーペアを適用する。
// 引数: state=選択行と表示領域、num=適用するncursesカラーペア番号。
// 返り値: なし。
void draw_select_dir_scene_color(struct editor_state *state,int num){
    if(state->settings_data->file_select_scene_lighting == false)
        return;
    

    if(state->file_browser_area.w <= 0 || state->dir_num <= 0 ||
       state->file_select_line < 0 || state->file_select_line >= state->dir_num){
        return;
    }
    int ligthing_line = state->file_browser_area.pos.y + state->file_select_line;
    mvchgat(ligthing_line,state->file_browser_area.pos.x,state->file_browser_area.w,A_NORMAL,num,NULL);
   
}

// show_file_browse(): 画面状態をファイルブラウザへ切り替え、枠・パス・一覧・選択行を描く。
// 引数: state=画面状態、file_browse_box=枠、dir_name_table=一覧、path_name=現在パス、win=描画先。
// 返り値: なし。
void show_file_browse(struct editor_state *state,struct box file_browse_box,char *dir_name_table,char *path_name,WINDOW *win){
    draw_box(file_browse_box,win);
    draw_now_path_name(file_browse_box,path_name);
    draw_box_inside_dir(state,dir_name_table);
    draw_select_dir_scene_color(state,2);
}

// file_sellect_line_update(): 旧選択行のハイライトを消し、新しい選択行をハイライトする。
// 引数: state=現在の選択状態、line=新しく選択する行番号。
// 返り値: なし。
void file_sellect_line_update(struct editor_state *state,int line){
    if(state->dir_num <= 0 || state->settings_data->file_select_scene_lighting == false){
        return;
    }
    if(line < 0){
        line = 0;
    }
    if(line >= state->dir_num){
        line = state->dir_num - 1;
    }
    draw_select_dir_scene_color(state,1);
    state->file_select_line = line;
    draw_select_dir_scene_color(state,2);

    refresh();
}

// editor_screen_move_line(): 画面をnum行スクロールし、論理カーソル行と表示開始行を同期する。
// 引数: state=カーソル行と表示開始行、win=スクロール対象、num=移動行数。
// 返り値: なし。
void editor_screen_move_line(struct editor_state *state,WINDOW *win,int num){
    int line_limit = get_line_limit();
    int next_mouse_line = state->mouse.now_mouce_line + num;
    int next_scr_start = state->scr.scr_start_num + num;
    if(line_limit <= 0 || next_mouse_line < 0 || next_mouse_line >= line_limit || next_scr_start < 0){
        return;
    }

    wsetscrreg(win, state->write_area.y_start, state->write_area.y_end - 1);
    wscrl(win, num);
    wsetscrreg(win, 0, state->scr.scr_size.y - 1);

    state->mouse.now_mouce_line = next_mouse_line;
    state->scr.scr_start_num = next_scr_start;

    struct pos line_start_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_start};
    struct pos line_end_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_end};

    draw_line_numbers(state);
    draw_line(line_start_pos,line_end_pos,win,all_draw_mode);
}

// editor_error_screen(): エラー表示用の画面へ切り替え、中央にメッセージを表示する。
// 引数: state=画面状態と表示領域、error_comment=表示するエラーメッセージ。
// 返り値: なし。
void editor_error_screen(struct editor_state *state,char *error_comment){
    state->is_cur_show = false;
    curs_set(0);
    clear();
    state->screen_state = error_screen;

    int screen_center_x     = state->file_browser_area.pos.x + (state->file_browser_area.w/2);
    int error_comment_size  = strlen(error_comment);
    int error_size          = sizeof("error");
    int press_enter_comment = sizeof("press enter to back");
    int comment_start_pos_x = screen_center_x - (error_comment_size/2);
    int error_start_pos_x   = screen_center_x - (error_size/2);
    int press_enter_comment_start_pos_x = screen_center_x - (press_enter_comment/2);
    int screen_center_y        =  state->file_browser_area.pos.y + (state->file_browser_area.h/2);

    attron(COLOR_PAIR(3));
    mvaddstr(screen_center_y-10,error_start_pos_x,"error");
    attroff(COLOR_PAIR(3));
    mvaddstr(screen_center_y-9,comment_start_pos_x,error_comment);
    mvaddstr(screen_center_y-8,press_enter_comment_start_pos_x,"press enter to back");

    refresh();
    flushinp();
}

// draw_file_data(): 読み込んだファイル内容のうち、現在画面に見える範囲を描画する。
// 引数: state=表示開始行・書き込み領域・読み込み済み文字バッファ。
// 返り値: なし。
void draw_file_data(struct editor_state *state){
    for(int i = 0; i < state->write_area.h; i++){
        int line = state->scr.scr_start_num + i;
        draw_editor_buffer_line(state, line, state->write_area.y_start + i);
    }
    refresh();
}

// draw_status_bar_line(): ステータスバーの横線と区切り接続部を描画する。
// 引数: state=書き込み領域、status_bar=描画するバー領域、win=描画先ウィンドウ。
// 返り値: なし。
void draw_status_bar_line(struct editor_state *state,struct box status_bar,WINDOW *win){
    int y;
    int x;
    getyx(win, y, x);

    struct pos end_pos = (struct pos){status_bar.pos.x + status_bar.w,status_bar.pos.y};
    draw_line(status_bar.pos,end_pos,win,all_draw_mode);
    mvaddch(status_bar.pos.y,state->write_area.x_start-1,ACS_TTEE);
    move(y,x);
}

// draw_status_bar_path(): ステータスバー中央に現在開いているファイル名を描画する。
// 引数: state=ファイルパスとステータスバー設定、win=描画先ウィンドウ。
// 返り値: なし。
void draw_status_bar_path(struct editor_state *state, WINDOW *win){
    if(state->file_data.now_open_path_name[0] == '\0' || !state->settings_data->show_status_bar){
        return;
    }

    int x;
    int y;
    getyx(win, y, x);

    int status_y = (state->settings_data->bar_side_state == top)
        ? state->status_bar->pos.y - 1 : state->status_bar->pos.y;
    char *draw_path = strrchr(state->file_data.now_open_path_name, '/');
    if(draw_path == NULL){
        draw_path = state->file_data.now_open_path_name;
    }

    int path_len = strlen(draw_path);
    int draw_len = path_len;
    int max_len = state->status_bar->w - 2;

    if(max_len <= 0){
        move(y, x);
        return;
    }
    if(draw_len > max_len){
        draw_path += draw_len - max_len;
        draw_len = max_len;
    }

    int draw_x = state->status_bar->pos.x + (state->status_bar->w - draw_len) / 2;
    if(state->settings_data->bar_side_state == top){
        mvhline(status_y, state->status_bar->pos.x, ' ', state->status_bar->w);
    }
    else{
        draw_status_bar_line(state, *state->status_bar, win);
    }
    mvaddnstr(status_y, draw_x, draw_path, draw_len);
    move(y, x);
}

void clear_box(struct box box){
    for(int i = box.pos.y;i < box.pos.y + box.h;i++){
        char buff[box.w];
        memset(buff,' ',sizeof(buff));
        mvaddnstr(i,box.pos.x,buff,box.w);
    }
}

//ステータスバーに現在の行数と終端行を記述する
void draw_line_status(struct editor_state *state,WINDOW *win){
    if(!state->settings_data->show_status_bar){
        return;
    }

    int x;
    int y;
    getyx(win, y, x);
    char line_status_str[32];
    snprintf(line_status_str, sizeof(line_status_str), "%d/%ld",
             state->mouse.now_mouce_line+1, state->file_data.description_line_end);
    int total_line_len = strlen(line_status_str);
    struct pos write_start_pos;
    write_start_pos.y = (state->settings_data->bar_side_state == top)
        ? state->status_bar->pos.y - 1 : state->status_bar->pos.y;
    if(total_line_len > state->status_bar->w){
        total_line_len = state->status_bar->w;
    }
    write_start_pos.x = state->status_bar->pos.x + state->status_bar->w - total_line_len;

    int clear_len = (state->status_bar->w < (int)sizeof(line_status_str))
        ? state->status_bar->w : (int)sizeof(line_status_str);
    mvhline(write_start_pos.y,
            state->status_bar->pos.x + state->status_bar->w - clear_len,
            ' ', clear_len);
    mvaddnstr(write_start_pos.y, write_start_pos.x, line_status_str, total_line_len);
    move(y,x);
}

