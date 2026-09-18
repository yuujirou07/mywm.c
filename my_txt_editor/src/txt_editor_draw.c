#include <dirent.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "filetree.h"
#include "settings_screen.h"
#include "txt_editor.h"
#include"txt_editor_syntax.h"
#include"error_log.h"

#define SETTINGS_EXPLATAION_BOX_MIN_W 12

static void draw_search_box(struct box search_box,WINDOW *win);
static int settings_item_name_width(const char *name);
static void draw_settings_title(struct box box,WINDOW *win);
static void draw_settings_screen(struct editor_input_context *ctx);
static void draw_settings_explanation_box(struct editor_input_context *ctx);
static int draw_settings_search_box(struct editor_input_context *ctx);
static int draw_filetree(struct editor_input_context *ctx,file_tree_data *filetree_data);

static void draw_explanation_str(const char *str,struct box box);
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
    //描画に使うのは可視幅だけ。バッファ側の容量とは無関係。
    int col_limit = editor_view_cols(state);
    if(screen_y < state->write_area.y_start || screen_y >= state->write_area.y_end || col_limit <= 0){
        return;
    }

    mvhline(screen_y, state->write_area.x_start, ' ', col_limit);
    if(line < 0 || line >= editor_line_limit(state)) return;
    if(state->str.line[line] <= 0) return;

    // wint_line_str_dataは画面セル位置に合わせて格納している。
    // addwstrで詰めて描くと2桁幅文字の後ろでズレるため、
    // セルごとのx座標へ1文字ずつ描く。
    wint_t *cells = editor_line_cells(state, line);
    if(cells == NULL) return;

    int max_col = editor_line_len(state, line);
    if(max_col > col_limit) max_col = col_limit;

    for(int col = 0; col < max_col; col++){
        wint_t cell = cells[col];
        if(cell == 0) continue;

        wchar_t ch = (wchar_t)cell;
        mvaddnwstr(screen_y, state->write_area.x_start + col, &ch, 1);
    }
}

// draw_line_numbers(): 表示開始行(scr_start_num)を基準に、左端へ行番号を描画する。
// カーソルの退避・復元は行わない。描画で動いた端末カーソルは、
// 編集画面の全描画後にmain()がeditor_sync_cursor()でモデルから置き直す。
// 引数: state=画面サイズ・表示開始行・書き込み領域を持つエディタ状態。
// 返り値: なし。
void draw_line_numbers(struct editor_state *state) {
    struct scr_data *scr_data = &state->scr;
    struct write_possible_area *area = &state->write_area;
    int line_number_space = (area->x_start > 0) ? area->x_start - 1 : 0;
    
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
}

// draw_line(): start_posからend_posまで水平線または垂直線を描く。
// fix_scr_line_damageでは壊れた罫線だけを検査して補修する。
// 引数: start_pos=開始座標、end_pos=終了座標、win=描画先、mode=全描画か補修か。
// 返り値: なし。
void draw_line(struct pos start_pos,struct pos end_pos,WINDOW *win,enum line_mode mode){
    (void)win;

    int range;
    int step_x;
    int step_y;
    chtype line_ch;

    if(!line_draw_info(start_pos, end_pos, &range, &step_x, &step_y, &line_ch)){
        return;
    }

    switch(mode){
        case all_draw_mode:
            draw_full_line(start_pos, range, step_x, step_y, line_ch);
            break;

        case fix_scr_line_damage:
            fix_line_damage(start_pos, range, step_x, step_y, line_ch);
            break;
    }
}

// box_existing_chr(): 指定セルに既に描かれている文字だけを取り出す。
// mvinch()は色やA_ALTCHARSETも一緒に返すため、文字の部分だけを残す。
// 引数: y/x=確認する座標。
// 返り値: その位置の文字。何も無ければ空白。
static chtype box_existing_chr(int y, int x){
    return (chtype)(mvinch(y, x) & A_CHARTEXT);
}

// box_joint_chr(): 角に重なる既存の罫線から、つなぎ目のT字を選ぶ。
// 既に横線が通っていればこちらの縦線が突き当たるので上下のT、
// 縦線が通っていればこちらの横線が突き当たるので左右のTになる。
// つなぐ相手がいなければ角のまま返す。
// 引数: corner=本来の角の文字、existing=その位置に既にある文字。
// 返り値: 描くべき罫線文字。
static chtype box_joint_chr(chtype corner, chtype existing){
    if(existing == ACS_HLINE){
        //上側の角なら下へ、下側の角なら上へ伸びる
        if(corner == ACS_ULCORNER || corner == ACS_URCORNER)return ACS_TTEE;
        if(corner == ACS_LLCORNER || corner == ACS_LRCORNER)return ACS_BTEE;
        return corner;
    }
    if(existing == ACS_VLINE){
        //左側の角なら右へ、右側の角なら左へ伸びる
        if(corner == ACS_ULCORNER || corner == ACS_LLCORNER)return ACS_LTEE;
        if(corner == ACS_URCORNER || corner == ACS_LRCORNER)return ACS_RTEE;
        return corner;
    }
    return corner;
}

// draw_box(): 指定された矩形領域の枠線と四隅を描画する。
// 既に罫線の通っている位置へ角が重なる場合は、その線とつながるT字へ
// 置き換える。枠を引く前に角を読むため、自分の線は写り込まない。
// 引数: box=描く矩形、win=描画先ウィンドウ。
// 返り値: なし。
void draw_box(struct box box, WINDOW *win){

    int x = box.pos.x;
    int y = box.pos.y;
    int w = box.w;
    int h = box.h;

    if(w <= 0 || h <= 0)return;
    

    struct pos top_left     = {x,     y};
    struct pos top_right    = {x + w - 1, y};
    struct pos bottom_left  = {x,         y + h - 1};
    struct pos bottom_right = {x + w - 1, y + h - 1};

    //角に重なる罫線は、線を引く前に読んでおく。
    chtype corner_ul = box_joint_chr(ACS_ULCORNER,box_existing_chr(y,         x));
    chtype corner_ur = box_joint_chr(ACS_URCORNER,box_existing_chr(y,         x + w - 1));
    chtype corner_ll = box_joint_chr(ACS_LLCORNER,box_existing_chr(y + h - 1, x));
    chtype corner_lr = box_joint_chr(ACS_LRCORNER,box_existing_chr(y + h - 1, x + w - 1));

    draw_line(top_left,    bottom_left,  win, all_draw_mode);
    draw_line(top_right,   bottom_right, win, all_draw_mode);
    draw_line(top_left,    top_right,    win, all_draw_mode);
    draw_line(bottom_left, bottom_right, win, all_draw_mode);

    mvaddch(y,         x,         corner_ul);
    mvaddch(y,         x + w - 1, corner_ur);
    mvaddch(y + h - 1, x,         corner_ll);
    mvaddch(y + h - 1, x + w - 1, corner_lr);

}

// flush_box_queue(): キューに積まれた枠を積んだ順に描画し、キューを空にする。
// 引数: queue=描画する枠を持つキュー、win=描画先ウィンドウ。
// 返り値: なし。
static void flush_box_queue(struct box_queue *queue, WINDOW *win){
    for(int i = 0; i < queue->count; i++){
        draw_box(queue->box[i], win);
    }
    queue->count = 0;
}

// request_draw_box(): 次回更新で描く枠を描画要求キューの末尾へ追加する。
// 引数: state=描画要求の保存先、box=枠線を含む描画領域。
// 返り値: なし。キューが満杯なら追加しない。
void request_draw_box(struct editor_state *state,struct box box){
    struct box_queue *queue = &state->draw_box_queue;

    if(queue->count >= DRAW_BOX_REQUEST_MAX)return;
    queue->box[queue->count++] = box;
    state->render_flags |= RENDER_BOX;
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
    for (int i = 1; i < w - 1; i++)
        mvaddch(y - 2, x + i, ACS_HLINE);
    mvaddch(y - 2, x + w - 1, ACS_URCORNER);

    mvaddch(y - 1, x, ACS_VLINE);
    int inner_w = w - 2;
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
    mvaddch(y - 1, x + w - 1, ACS_VLINE);
    mvaddch(y, x, ACS_LTEE);
    mvaddch(y, x + w - 1, ACS_RTEE);
}

// draw_edit_screen_base(): 編集画面の固定要素である区切り線と行番号を描画する。
// 引数: state=行番号情報、win=描画先、start_pos/end_pos=区切り線の端点。
// 返り値: なし。
void draw_edit_screen_base(struct editor_state *state,WINDOW *win,struct pos start_pos,struct pos end_pos){
    if(state->settings_data->draw_split_line){
        draw_line(start_pos,end_pos,win,all_draw_mode);
    }
    if(state->settings_data->show_status_bar){
        // draw_status_bar_path()はステータスバー行を丸ごと塗り直すため、
        // 行ステータスより先に描かないと"N/M"が消える。
        draw_status_bar_line(state,*state->status_bar,win);
        draw_status_bar_path(state,win);
        draw_line_status(state,win);
    }
    draw_line_numbers(state);
}

// draw_box_inside_dir(): load_dir_table()が作ったディレクトリエントリ一覧を
// ファイルブラウザの内側へ描画する。
// 引数: state=ファイルブラウザ領域、table=名前と種別を持つディレクトリエントリ一覧。
// 返り値: なし。
void draw_box_inside_dir(struct editor_state *state,struct dir_entry *table){
    
    if(table == NULL || state->file_browse.area.w <= 0 || state->file_browse.area.h <= 0){return;}
    char clear[state->file_browse.area.w + 1];
    memset(clear,' ',state->file_browse.area.w * sizeof(char));
    clear[state->file_browse.area.w] = '\0';

    // エントリ名は幅に関係なく丸ごと保持しているため、はみ出す分はここで詰める。
    // 描き始めがpos.x+1なので、使える幅は内側幅から1引いた分。
    int max_len = state->file_browse.area.w - 3;

    // now_logical_line以降の全件テーブルを、i番目の画面行へ対応付ける。
    for(int i = 0;i < state->file_browse.area.h;i++){
        mvaddstr(state->file_browse.area.pos.y + i, state->file_browse.area.pos.x,clear);
        if(max_len <= 0) continue;

        struct dir_entry *entry = &table[state->file_browse.select_line.now_logical_line + i];
        if(entry->name[0] == '\0') continue;

        int draw_y = state->file_browse.area.pos.y + i;
        int draw_x = state->file_browse.area.pos.x + 3;
        int len = (int)strlen(entry->name);

        wchar_t icon_code[2];
        get_icon(state,*entry,&icon_code[0]);
        mvaddwstr(draw_y,draw_x - 2,icon_code);
        if(len <= max_len){
            mvaddstr(draw_y, draw_x, entry->name);
        }
        else if(max_len > 3){
            // 末尾を"..."にして省略したことが分かるようにする。
            mvaddnstr(draw_y, draw_x, entry->name, max_len - 3);
            mvaddstr(draw_y, draw_x + max_len - 3, "...");
        }
        else{
            mvaddnstr(draw_y, draw_x, entry->name, max_len);
        }
    }
}

// draw_select_dir_scene_color(): ファイルブラウザの選択行に指定カラーペアを適用する。
// 引数: state=選択行と表示領域、num=適用するncursesカラーペア番号。
// 返り値: なし。
void draw_select_dir_scene_color(struct editor_state *state,int dir_num,int num){
    int cur_x;
    int cur_y;
    getyx(stdscr,cur_y,cur_x);
    if(state->settings_data->file_select_scene_lighting == false)
        return;

    if(state->file_browse.area.w <= 0 || dir_num <= 0 ||
       state->file_browse.select_line.now_line < 0 || state->file_browse.select_line.now_line >= dir_num){
        return;
    }
    int lighting_line = state->file_browse.area.pos.y + state->file_browse.select_line.now_line;
    mvchgat(lighting_line,state->file_browse.area.pos.x,state->file_browse.area.w,A_NORMAL,num,NULL);

    if(state->file_browse.select_line.previous_line != state->file_browse.select_line.now_line){
        int previous_line = state->file_browse.area.pos.y + state->file_browse.select_line.previous_line;
        mvchgat(previous_line,state->file_browse.area.pos.x,state->file_browse.area.w,A_NORMAL,1,NULL);
    }
    move(cur_y,cur_x);
}

// show_file_browse(): ファイルブラウザ全体の再描画を要求する。
// 引数: state=描画要求の保存先。枠やパスはstate->file_browseから参照する。
// 返り値: なし。
void show_file_browse(struct editor_state *state){
    state->render_flags |= RENDER_FILE_BROWSE;
}

// set_file_select_line(): 選択行を更新し、選択表示の再描画を要求する。
// 引数: state=現在の選択状態、line=新しく選択する行番号。
// 返り値: なし。
void set_file_select_line(struct editor_state *state,int dir_num,int line){
    if(dir_num <= 0 || state->settings_data->file_select_scene_lighting == false){
        return;
    }
    if(line < 0){
        line = 0;
    }
    if(line >= dir_num){
        line = dir_num - 1;
    }
    
    file_select_line_update(&state->file_browse.select_line, line);
    state->render_flags |= RENDER_SELECT_DIR_SCENE_COLOR;
}

// editor_screen_move_line(): 画面をnum行スクロールし、論理カーソル行と表示開始行を同期する。
// cursor.file_pos.yとscr_start_numを両方有効な場合だけ同時に更新するため、
// editor_move_cursor_line()は使わずここで直接書き込む。呼び出し側でcursor.file_pos.yを
// 重ねて動かさないこと(この関数がすでに+num分を反映済み)。
// 桁は移動先の行長へ丸めるが、呼び出し側が別の桁を指定したい場合は戻ってから上書きする。
// 引数: ctx=カーソル行と表示開始行を持つ入力context、num=-1または1の移動行数。
// 返り値: なし。
void editor_screen_move_line(struct editor_input_context *ctx,int num){
    struct editor_state *state = ctx->state;
    int line_limit = get_line_limit();
    int next_cursor_line = state->cursor.file_pos.y + num;
    int next_scr_start = state->scr.scr_start_num + num;
    if(line_limit <= 0 || next_cursor_line < 0 || next_cursor_line >= line_limit || next_scr_start < 0){
        return;
    }

    state->cursor.file_pos.y = next_cursor_line;
    state->cursor.file_pos.x = editor_clamp_col(state, next_cursor_line,
        state->cursor.file_pos.x);
    state->scr.scr_start_num = next_scr_start;

    if(state->settings_data->built_in_syntax){
        scroll_syntax_pos_data(-num,state->write_area.h);
        int update_line = num > 0 ? state->write_area.h - 1 : 0;
        update_line_syntax_data(ctx,update_line);
    }

    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// editor_error_screen(): エラー表示用の画面へ切り替え、中央にメッセージを表示する。
// 引数: state=画面状態と表示領域、error_comment=表示するエラーメッセージ。
// 返り値: なし。
void editor_error_screen(struct editor_state *state,char *error_comment){
    my_cur_set(state,false);
    clear();
    editor_set_screen_state(state, error_screen);

    int screen_center_x     = state->file_browse.area.pos.x + (state->file_browse.area.w/2);
    int error_comment_size  = strlen(error_comment);
    int error_size          = sizeof("error");
    int press_enter_comment = sizeof("press enter to back");
    int comment_start_pos_x = screen_center_x - (error_comment_size/2);
    int error_start_pos_x   = screen_center_x - (error_size/2);
    int press_enter_comment_start_pos_x = screen_center_x - (press_enter_comment/2);
    int screen_center_y        =  state->file_browse.area.pos.y + (state->file_browse.area.h/2);

    attron(COLOR_PAIR(3));
    mvaddstr(screen_center_y-10,error_start_pos_x,"error");
    attroff(COLOR_PAIR(3));
    mvaddstr(screen_center_y-9,comment_start_pos_x,error_comment);
    mvaddstr(screen_center_y-8,press_enter_comment_start_pos_x,"press enter to back");
    error_log(error_comment);//ファイル出力
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
}

// draw_status_bar_line(): ステータスバーの横線と区切り接続部を描画する。
// 引数: state=書き込み領域、status_bar=描画するバー領域、win=描画先ウィンドウ。
// 返り値: なし。
void draw_status_bar_line(struct editor_state *state,struct box status_bar,WINDOW *win){
    struct pos end_pos = (struct pos){status_bar.pos.x + status_bar.w - 1,status_bar.pos.y};
    draw_line(status_bar.pos,end_pos,win,all_draw_mode);
    mvaddch(status_bar.pos.y,state->write_area.x_start-1,ACS_TTEE);
}

// draw_status_bar_path(): ステータスバー中央に現在開いているファイル名を描画する。
// 引数: state=ファイルパスとステータスバー設定、win=描画先ウィンドウ。
// 返り値: なし。
void draw_status_bar_path(struct editor_state *state, WINDOW *win){
    if(!state->settings_data->show_status_bar){
        return;
    }
    
    int status_y = (state->settings_data->bar_side_state == top)
        ? state->status_bar->pos.y - 1 : state->status_bar->pos.y;
    
    char *draw_path = NULL; 
    if(state->file_data.now_open_path_name[0] == '\0'){ 
        draw_path = "Unknown file";
    }
    else{
        
        draw_path = strrchr(state->file_data.now_open_path_name, '/');
        if(draw_path == NULL){
            draw_path = state->file_data.now_open_path_name;
        }

        int path_len = strlen(draw_path);
        int draw_len = path_len;
        int max_len = state->status_bar->w - 2;

        if(max_len <= 0){
            return;
        }
        if(draw_len > max_len){
            draw_path += draw_len - max_len;
            draw_len = max_len;
        }
    }
    size_t draw_len = strlen(draw_path);
    int draw_x = state->status_bar->pos.x + (state->status_bar->w - draw_len) / 2;
    if(state->settings_data->bar_side_state == top){
        mvhline(status_y, state->status_bar->pos.x, ' ', state->status_bar->w);
    }
    else{
        draw_status_bar_line(state, *state->status_bar, win);
    }
    mvaddnstr(status_y, draw_x, draw_path, draw_len);
}

// clear_box(): 登録済み矩形を空白で消し、消去要求件数を0へ戻す。
// 引数: clear_box=消去対象の矩形配列と件数。
// 返り値: なし。ncursesの描画失敗は通知しない。
void clear_box(struct clear_box_data *clear_box){
    for(int f = 0;f < clear_box->clear_box_counter;f++){
        struct box box = clear_box->clear_box[f];
        for(int i = box.pos.y;i < box.pos.y + box.h;i++){
            char buff[box.w];
            memset(buff,' ',sizeof(buff));
            mvaddnstr(i,box.pos.x,buff,box.w);
        }
    }
    clear_box->clear_box_counter = 0;
}

// draw_line_status(): ステータスバー右端へ現在行と総行数を描画する。
// 引数: state=カーソル・行数・ステータスバー設定、win=描画先。現在は標準画面へ描くため未使用。
// 返り値: なし。ステータスバー非表示時は何もしない。
void draw_line_status(struct editor_state *state,WINDOW *win){
    if(!state->settings_data->show_status_bar){
        return;
    }

    (void)win;
    char line_status_str[32];
    snprintf(line_status_str, sizeof(line_status_str), "%d/%ld",
             state->cursor.file_pos.y+1, state->file_data.description_line_end);
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
}

// draw_make_file_dialog(): 新規作成の確認画面またはファイル名入力画面を描画する。
// 引数: ctx=画面状態・配置基準・描画先を持つ入力context。
// 返り値: なし。入力画面ではカーソル反映位置と入力欄の矩形も更新する。
static void draw_make_file_dialog(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(!state->make_file_mode_status.is_input_scene){
        char comment[] = "The file cannot be found; would you like to create it?";
        int comment_len = strlen(comment);
        int box_h = comment_len / state->scr.scr_size.x + 4;
        int box_w = (state->scr.scr_size.x > comment_len + 2)
            ? comment_len + 2 : state->scr.scr_size.x;
        struct box box = {
            .pos = {ctx->ask_make_file_mode.screen_center_pos.x - comment_len / 2,
                    ctx->ask_make_file_mode.screen_center_y -
                    ctx->ask_make_file_mode.screen_center_y / 2},
            .w = box_w,
            .h = box_h,
        };
        struct pos text_pos = {box.pos.x + 1, box.pos.y + 1};

        state->ask_make_file_box = box;
        draw_box(box, win);
        my_mvaddstr(text_pos, comment);
        mvaddstr(text_pos.y + 1, text_pos.x + box.w / 2 - (int)strlen("YES[y]") - 2, "YES[y]");
        mvaddstr(text_pos.y + 1, text_pos.x + box.w / 2 + 2, "NO[n]");
        my_cur_set(state,false);
        editor_sync_cursor(state);
        return;
    }

    struct box write_box = {
        .pos = state->ask_make_file_box.pos,
        .w = state->ask_make_file_box.w,
        .h = state->ask_make_file_box.h + 2,
    };
    struct box input_box = {
        .pos = {write_box.pos.x + 1, write_box.pos.y + write_box.h - 4},
        .w = write_box.w - 2,
        .h = 3,
    };
    char *label = "write a new file name";
    int label_x = write_box.pos.x + (write_box.w - (int)strlen(label)) / 2;

    struct clear_box_data clear_data = {
        .clear_box = {write_box},
        .clear_box_counter = 1,
    };

    clear_box(&clear_data);
    draw_box(write_box, win);
    draw_box(input_box, win);
    mvaddstr(write_box.pos.y + 1, label_x, label);
    mvaddnstr(input_box.pos.y + 1, input_box.pos.x + 1,
               state->make_file_mode_status.new_file_name,
               state->make_file_mode_status.new_file_name_counter);
    state->write_file_name_area = input_box;
    cur_pos_push((struct pos){input_box.pos.x + 1 + state->make_file_mode_status.new_file_name_counter,
                 input_box.pos.y + 1},state);
    my_cur_set(state,true);
}

// update_screen(): render_flagsに登録された描画要求を順に処理する。
// 引数: ctx=画面状態・各画面の配置・描画先を持つ入力context。
// 返り値: なし。処理後はrender_flagsをRENDER_NONEへ戻す。
void update_screen(struct editor_input_context *ctx){
    unsigned int flags = ctx->state->render_flags;
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;
    

    if(flags & RENDER_ALL){
        
    }
    else{
        if(flags & RENDER_CLEAR_BOX){
            clear_box(&state->clear_box_data);
        }
        if(flags & RENDER_STATUS_BAR_LINE){
            draw_status_bar_line(state, *state->status_bar, win);
        }

        if(flags & RENDER_LINE_STATUS){
            draw_line_status(state, win);
        }
        if(flags & RENDER_SETTINGS){
            bool is_cur_move = false;
            struct pos mouse_pos;
            draw_settings_screen(ctx);   
            if(state->settings_screen_data.value_input_mode){
                draw_settings_search_box(ctx);
                getyx(ctx->win,mouse_pos.y,mouse_pos.x);
                is_cur_move = true;
            }
            draw_settings_explanation_box(ctx);
            if(is_cur_move)cur_pos_push(mouse_pos,state);
        }

        if(flags & RENDER_LINE){
            draw_line(ctx->edit_screen.line_start_pos,
                      ctx->edit_screen.line_end_pos, win, all_draw_mode);
        }
        if(flags & RENDER_SELECT_DIR_SCENE_COLOR){
            draw_select_dir_scene_color(state,state->file_browse.dir_name_table_num,2);
        }
        if(flags & RENDER_EDIT_SCREEN_BASE){
            draw_edit_screen_base(state, win, ctx->edit_screen.line_start_pos,
                                  ctx->edit_screen.line_end_pos);
            // 行番号の消去は画面左端から始まりツリーの枠まで及ぶため、
            // ツリーを表示中は必ずこの後で描き直す。
            if(state->file_tree_data.is_show){
                draw_filetree(ctx,&state->file_tree_data);
            }
        }
        if(flags & RENDER_FILE_DATA){
            draw_file_data(state);
        }
        if(flags & RENDER_FILE_BROWSE){
            //ブラウザ画面を後ろのコードが見えないように消す
            set_clear_box(&state->clear_box_data,state->file_browse.box);
            clear_box(&state->clear_box_data);
            draw_box(state->file_browse.box, win);
            draw_now_path_name(state->file_browse.box,
                               state->file_browse.path_name);
            draw_box_inside_dir(state, state->file_browse.dir_name_table);
            draw_select_dir_scene_color(state,state->file_browse.dir_name_table_num,2);

            //サーチボックスの描画とサーチボックス内のパス描画
            if(get_file_browse_path_input_mode(&state->file_browse)){
                my_cur_set(state,true);

                struct box search_box = state->file_browse.search_box;
                draw_search_box(search_box,ctx->win);

                int cur_line = search_box.pos.y + 1;
                int col = search_box.pos.x + 1;

                const wchar_t *path = now_open_path_name(NULL,get);
                size_t path_len = wcslen(path);

                //マウスカーソル分を確保するため両サイド合わせて-3する
                size_t show_path_size = search_box.w - 3;
                const wchar_t *str_start_ptr = path;
                if(path_len > show_path_size){
                    str_start_ptr = &path[path_len - show_path_size];
                }
                else{
                    char clear_area[show_path_size - path_len+1];
                    memset(clear_area,' ',sizeof(char)*(show_path_size - path_len));
                    clear_area[show_path_size - path_len] = '\0';
                    mvaddnstr(cur_line,col + path_len,clear_area,show_path_size);
                }
                
                mvaddwstr(cur_line,col,str_start_ptr);
                int x;
                int y;
                getyx(ctx->win,y,x);
                cur_pos_push((struct pos){x,y},state);
            }
        }
        if(flags & RENDER_BOX){
            flush_box_queue(&state->draw_box_queue, win);
        }
        if(flags & RENDER_LINE_JUMP){
           draw_line_jump(state);
        }
        if(flags & RENDER_MAKE_FILE){
            draw_make_file_dialog(ctx);
        }
    }
    set_cur_pos(state);
    ctx->state->render_flags = RENDER_NONE;
    if(editor_get_screen_state(state) != edit_screen)refresh();
}

// request_clear_box(): 次回更新で消す矩形を消去要求配列へ追加する。
// 引数: state=消去要求の保存先、box=消去対象領域。
// 返り値: なし。要求配列が満杯ならエラー画面へ遷移する。
void request_clear_box(struct editor_state *state, struct box box){
    if(state->clear_box_data.clear_box_counter >= box_retention_max){
        editor_error_screen(state,"clear box over flow ");
        return;
    }
    int *counter = &state->clear_box_data.clear_box_counter;
    state->clear_box_data.clear_box[(*counter)++] = box; 
    state->render_flags |= RENDER_CLEAR_BOX;
}


// draw_line_jump(): 行ジャンプの入力欄と入力済み行番号を描画する。
// 引数: state=入力値・ステータスバー配置・カーソル反映待ち位置を持つ状態。
// 返り値: なし。
void draw_line_jump(struct editor_state *state){
    struct pos prompt_pos = {state->write_area.x_start - 1,
                             state->write_area.y_start};

    if(state->settings_data->show_status_bar){
        prompt_pos.y = (state->settings_data->bar_side_state == top)
            ? state->status_bar->pos.y - 1 : state->status_bar->pos.y;
        prompt_pos.x = state->status_bar->pos.x;
    }
    mvhline(prompt_pos.y, prompt_pos.x + 1, ' ',
            strlen("JMP_LINE ") + JUMP_LINE_NUM_DIGITS);
    mvaddstr(prompt_pos.y, prompt_pos.x + 1, "JMP_LINE ");
    mvaddnstr(prompt_pos.y, prompt_pos.x + 1 + (int)strlen("JMP_LINE "),
                state->jump_mode_data.jump_line_num,
                state->jump_mode_data.jump_line_num_counter);
    cur_pos_push((struct pos){prompt_pos.x + 1 + (int)strlen("JMP_LINE ") +
                 state->jump_mode_data.jump_line_num_counter,prompt_pos.y},state);
}

// set_clear_box(): 消去対象の矩形をclear_box_dataの末尾へ追加する。
// 引数: clear_box_data=追加先、box=消去対象領域。
// 返り値: 成功時0、引数がNULLまたは配列が満杯なら-1。
int set_clear_box(struct clear_box_data *clear_box_data,struct box box){
        if(clear_box_data == NULL){
            return -1;
        }
        if((int)(sizeof(clear_box_data->clear_box)/sizeof(struct box)) <= clear_box_data->clear_box_counter){
            return -1;   
        }

        clear_box_data->clear_box[clear_box_data->clear_box_counter++] = box;
        return 0;
}

// draw_search_box(): ファイルブラウザのパス入力枠を描画する。
// 引数: search_box=枠線を含む描画領域、win=描画先ウィンドウ。
// 返り値: なし。winがNULLなら何もしない。
static void draw_search_box(struct box search_box,WINDOW *win){
    if(win == NULL)return;
    draw_box(search_box,win);
    mvaddch(search_box.pos.y,search_box.pos.x,ACS_LTEE);
    mvaddch(search_box.pos.y,search_box.pos.x + search_box.w -1,ACS_RTEE);
}


// settings_item_name_width(): 設定項目名の画面上の表示幅を返す。
// UTF-8のバイト数と画面セル数は日本語やアイコンで一致しないため、
// 一度ワイド文字へ変換してからwcswidth()で数える。
// 引数: name=UTF-8の項目名。
// 返り値: 表示幅。NULLや変換失敗時は-1。
static int settings_item_name_width(const char *name){
    if(name == NULL)return -1;

    wchar_t wide_name[256];
    size_t converted = mbstowcs(wide_name,name,sizeof(wide_name)/sizeof(wide_name[0]) - 1);
    if(converted == (size_t)-1)return -1;
    wide_name[converted] = L'\0';

    int width = wcswidth(wide_name,converted);
    return (width < 0) ? -1 : width;
}

// draw_settings_title(): 設定画面の枠上辺の中央へタイトルを埋め込む。
// 枠線の上に重ね書きするため、必ずdraw_box()のあとに呼ぶ。
// 引数: box=設定画面の枠、win=描画先ウィンドウ。
// 返り値: なし。
static void draw_settings_title(struct box box,WINDOW *win){
    (void)win;

    const char *title = " Settings ";
    int title_len = (int)strlen(title);
    if(box.w < title_len + 2)return;

    int title_x = box.pos.x + (box.w - title_len) / 2;
    attron(COLOR_PAIR(SETTINGS_ACCENT_COLOR_PAIR) | A_BOLD);
    mvaddstr(box.pos.y,title_x,title);
    attroff(COLOR_PAIR(SETTINGS_ACCENT_COLOR_PAIR) | A_BOLD);
}

// set_settings_screen_box(): 設定項目の中身に合わせて設定画面の枠を決める。
// 幅はキーと項目名が収まる列幅から、高さは項目数から求め、画面の中央へ置く。
// 高さだけでは収まらない分は列を増やして横へ送るため、項目が増えるほど
// 枠は横に広がる。項目が少ないときはSETTINGS_SCREEN_MIN_*まで広げ、画面に
// 収まらない分の間引きはdraw_settings_screen()が行う。
// 引数: state=設定項目・画面サイズ・枠を持つエディタ状態。
// 返り値: なし。
void set_settings_screen_box(struct editor_state *state){
    if(state == NULL)return;
    settings_screen_data *st_scr_data = &state->settings_screen_data;

    //枠の上限。画面の端まで届かせず、上下左右に余白を残す。
    int limit_w = state->scr.scr_size.x - state->scr.scr_size.x / SETTINGS_SCREEN_MARGIN_DIV;
    int limit_h = state->scr.scr_size.y - state->scr.scr_size.y / SETTINGS_SCREEN_MARGIN_DIV;

    //一番長い項目名にキーと余白を足して、1列分の幅を決める。
    int name_width_max = 0;
    for(int i = 0;i < st_scr_data->settings_item_data_num;i++){
        int name_width = settings_item_name_width(st_scr_data->item_data[i].name);
        if(name_width > name_width_max)name_width_max = name_width;
    }
    int col_w = name_width_max + SETTINGS_ITEM_KEY_WIDTH + 5;
    if(col_w < SETTINGS_ITEM_COLUMN_MIN_WIDTH)col_w = SETTINGS_ITEM_COLUMN_MIN_WIDTH;
    if(col_w > SETTINGS_ITEM_COLUMN_MAX_WIDTH)col_w = SETTINGS_ITEM_COLUMN_MAX_WIDTH;

    //枠の内側に並べられる行数。上下の枠線の分を引く。
    int rows = limit_h - 2;
    if(rows < 1)rows = 1;

    //縦に収まらない分は列を増やして横へ送る。
    int col_count = (st_scr_data->settings_item_data_num + rows - 1) / rows;
    if(col_count < 1)col_count = 1;

    int box_w = col_w * col_count + 2;
    int box_h = st_scr_data->settings_item_data_num + 2;
    //項目が少なくても小さくなりすぎないよう、最小サイズまで広げる。
    if(box_w < SETTINGS_SCREEN_MIN_W)box_w = SETTINGS_SCREEN_MIN_W;
    if(box_h < SETTINGS_SCREEN_MIN_H)box_h = SETTINGS_SCREEN_MIN_H;
    if(box_w > limit_w)box_w = limit_w;
    if(box_h > limit_h)box_h = limit_h;

    //残った余白は左右と上下へ均等に割って中央へ寄せる。
    st_scr_data->box.pos = (struct pos){
        (state->scr.scr_size.x - box_w) / 2,
        (state->scr.scr_size.y - box_h) / 2
    };
    st_scr_data->box.w = box_w;
    st_scr_data->box.h = box_h;
}

// draw_settings_screen(): 設定画面の枠・タイトル・項目を描画する。
// 項目は上から下へ並べ、枠の高さに収まらなくなった分は次の列へ送る。
// 1列が広くなりすぎないよう幅に上限を設け、余った幅は左右へ分けて
// 項目の並びを中央へ寄せる。これで項目名とキーが離れすぎない。
// 選択行はマーカーと反転表示で示す。項目名は列の左端、キーは列の右端に
// 寄せるため、名前の長さが違ってもキーの位置がそろう。
// 引数: ctx=設定項目と描画領域を持つ入力context。
// 返り値: なし。
static void draw_settings_screen(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;
    settings_screen_data st_scr_data = state->settings_screen_data;

    draw_box(st_scr_data.box,ctx->win);
    draw_settings_title(st_scr_data.box,ctx->win);

    int item_num = st_scr_data.settings_item_data_num;
    int inner_w = st_scr_data.box.w - 2;
    //内側に並べられるのは上下の枠線を除いた分だけ。
    int inner_h = st_scr_data.box.h - 2;
    if(item_num <= 0 || inner_w <= 0 || inner_h <= 0)return;

    //1列に入りきらない分を次の列へ回す。端数は最終列に入る。
    int col_count = (item_num + inner_h - 1) / inner_h;
    //狭い枠で列を増やしすぎると1列が潰れるため、最小幅で頭打ちにする。
    int col_count_max = inner_w / SETTINGS_ITEM_COLUMN_MIN_WIDTH;
    if(col_count > col_count_max)col_count = col_count_max;
    if(col_count < 1)col_count = 1;

    //余りは切り捨てる。はみ出した分は使わないだけで、枠線へは被らない。
    int col_w = inner_w / col_count;
    //枠が広いときはキーが右端まで離れてしまうため、列幅に上限を設ける。
    if(col_w > SETTINGS_ITEM_COLUMN_MAX_WIDTH)col_w = SETTINGS_ITEM_COLUMN_MAX_WIDTH;
    //使わなくなった幅は左右へ均等に割って、項目の並びを中央へ寄せる。
    int items_pos_x = st_scr_data.box.pos.x + 1 + (inner_w - col_w * col_count) / 2;

    //列の外側に前回の描画が残らないよう、内側を一度消してから並べる。
    for(int y = 0; y < inner_h;y++){
        mvhline(st_scr_data.box.pos.y + y + 1,st_scr_data.box.pos.x + 1,' ',inner_w);
    }

    for(int c = 0; c < col_count;c++){
        //列の左端。マーカーはこの位置、名前はその2つ右に置く。
        int col_pos_x = items_pos_x + c * col_w;
        int mark_pos_x = col_pos_x;
        int item_pos_x = col_pos_x + 2;
        //キーは列の右端から桁数と余白1つ分だけ内側へ寄せる。
        int key_pos_x = col_pos_x + col_w - 1 - SETTINGS_ITEM_KEY_WIDTH - 1;

        for(int i = 0; i < inner_h;i++){
            int item_index = c * inner_h + i;
            if(item_index >= item_num)break;

            settings_items_data item = st_scr_data.item_data[item_index];
            int scr_pos_y = st_scr_data.box.pos.y + i + 1;
            bool is_select = (item_index == st_scr_data.select_line);

            if(is_select){
                mvaddnwstr(scr_pos_y,mark_pos_x,L"\u276F",1);
            }

            attron(A_BOLD);
            my_mvaddstr((struct pos){item_pos_x,scr_pos_y},(char *)item.name);
            attroff(A_BOLD);

            //名前の後ろからキーの手前まで空白で埋める。
            int name_width = settings_item_name_width(item.name);
            if(name_width < 0)name_width = 0;
            for(int blank = item_pos_x + name_width;blank < key_pos_x;blank++){
                mvaddch(scr_pos_y,blank,' ');
            }

            wchar_t key_code[SETTINGS_ITEM_KEY_WIDTH + 1] =
                {L'[',(wchar_t)item.key_code,L']',L'\0'};
            attron(COLOR_PAIR(SETTINGS_ACCENT_COLOR_PAIR) | A_BOLD);
            mvaddnwstr(scr_pos_y,key_pos_x,key_code,SETTINGS_ITEM_KEY_WIDTH);
            attroff(COLOR_PAIR(SETTINGS_ACCENT_COLOR_PAIR) | A_BOLD);

            //反転は最後に重ねる。名前とキーの色より選択表示を優先する。
            if(is_select){
                mvchgat(scr_pos_y,col_pos_x,col_w,A_NORMAL,2,NULL);
            }
        }
    }
}


// draw_settings_search_box(): 選択項目の型名と入力中の値を設定画面上部へ描画する。
// 引数: ctx=設定画面状態と描画先を持つ入力context。
// 返り値: 成功時0、入力枠を配置できない場合は-1。
static int draw_settings_search_box(struct editor_input_context *ctx){

    struct editor_state *state = ctx->state;
    settings_screen_data st_scr_data = state->settings_screen_data;

    if(st_scr_data.box.pos.y <= 0)return -1;

    struct pos search_box_pos = (struct pos){st_scr_data.box.pos.x,st_scr_data.box.pos.y - 3};
    struct box search_box = {search_box_pos,st_scr_data.box.w,3};
    draw_box(search_box,ctx->win);
    int input_w = search_box.w - 2;
    if(input_w <= 0)return -1;

    mvhline(search_box.pos.y + 1,search_box.pos.x + 1,' ',input_w);

    settinge_value_type value_type = st_scr_data.item_data[st_scr_data.select_line].value_type;
    wchar_t value_type_str[32];
    switch(value_type){
        case VALUE_TYPE_BOOL:
            wcscpy(value_type_str,L"BOOL:");
            break;
        case VALUE_TYPE_INT:
            wcscpy(value_type_str,L"INT:");
            break;
        case VALUE_TYPE_STR:
            wcscpy(value_type_str,L"STR:");
            break;
        case VALUE_TYPE_UNKNOWN:
            wcscpy(value_type_str,L"UNKNOWN:");
            break;
    }

    int input_start = st_scr_data.input_value_len - input_w;
    if(input_start < 0)input_start = 0;
    int value_type_str_len = wcslen(value_type_str);
    mvaddwstr(search_box.pos.y + 1,search_box.pos.x + 1,value_type_str);
    if(st_scr_data.input_value_len > input_start){
        mvaddnwstr(search_box.pos.y + 1,search_box.pos.x + 1 + value_type_str_len,
                   &st_scr_data.input_value[input_start],input_w);
    }
    return 0;
}


// draw_settings_explanation_box(): 選択中設定項目の説明枠を設定画面の右側へ描画する。
// 引数: ctx=設定項目・画面寸法・描画先を持つ入力context。
// 返り値: なし。必要な幅または説明文が無い場合は描画しない。
static void draw_settings_explanation_box(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;
    settings_screen_data st_scr_data = state->settings_screen_data;

    int set_mouse_pos_x;
    int set_mouse_pos_y;
    getyx(ctx->win,set_mouse_pos_y,set_mouse_pos_x);

    bool is_cur_showed = false;
    if(state->is_cur_show){
        my_cur_set(state,false);
        is_cur_showed = true;
    }
    int x;
    int y;
    getmaxyx(ctx->win,y,x);

    int explanation_box_pos_x = st_scr_data.box.pos.x + st_scr_data.box.w;
    int explanation_box_max_w = x - explanation_box_pos_x;
    if(explanation_box_max_w < SETTINGS_EXPLATAION_BOX_MIN_W)return;
    

    struct pos explanation_pos = {explanation_box_pos_x,st_scr_data.box.pos.y};
    size_t explanation_str_len = strlen(st_scr_data.item_data[st_scr_data.select_line].explanation);
    if(explanation_str_len <= 0)return;
    //説明ウィンドウの座標から画面下までのセル数
    int explanation_box_max_h = (y - st_scr_data.box.pos.y);
    //説明ウィンドウの横幅に説明文を入れきれるかの計算
    int explanation_str_h = (explanation_str_len/(SETTINGS_EXPLATAION_BOX_MIN_W - 2)) + 1;
    
    int area_w = 0;
    int area_h = 0;
    for(int w = SETTINGS_EXPLATAION_BOX_MIN_W;w < explanation_box_max_w;w++){
        for(int h = st_scr_data.box.h; h < explanation_box_max_h;h++){
            int explanation_box_area = w * h;
            if(explanation_box_area < explanation_str_h)continue;
            area_w = w;
            area_h = h;
            break;
        }
        if(area_h != 0)break;
    }

    struct box explanation_box = {explanation_pos,area_w,area_h};
    draw_box(explanation_box,ctx->win);
    draw_explanation_str(st_scr_data.item_data[st_scr_data.select_line].explanation,
        explanation_box);
    move(set_mouse_pos_y,set_mouse_pos_x);
    if(is_cur_showed)my_cur_set(state,true);
}
// draw_explanation_str(): 説明文をboxの内側幅で分割し、上から順に描画する。
// 引数: str=描画するNUL終端文字列、box=枠線を含む描画領域。
// 返り値: なし。空文字列なら何も描画しない。strがNULLまたはbox.wが2以下の場合の動作は未定義。
static void draw_explanation_str(const char *str,struct box box){
    int str_len = strlen(str);
    if(str_len <= 0)return;

    int loop_h = (str_len + box.w - 3) / (box.w - 2);
    int offset = 0;
    for(int h = 0;h < loop_h;h++){
        struct pos explanation_str_pos = {box.pos.x + 1,box.pos.y + h+1};
        int split_len = (str_len > box.w)?box.w - 2:str_len;
        char splited_explanation_str[split_len + 1];
        memcpy(splited_explanation_str,&str[offset],sizeof(char) * split_len);
        splited_explanation_str[split_len] = '\0';
        my_mvaddstr(explanation_str_pos,splited_explanation_str);
        offset += split_len;
    }
    return;
}


// draw_filetree(): ファイルツリーの枠を描画する。
// 編集領域の左端はshow_filetree()が決めるため、ここでは描画だけを行う。
// 引数: ctx=描画先ウィンドウを持つ入力context、filetree_data=枠と表示状態を持つツリー状態。
// 返り値: 常に0。
static int draw_filetree(struct editor_input_context *ctx,file_tree_data *filetree_data){
    struct box box = filetree_data->file_tree_box;

    if(box.w <= 2 || box.h <= 2){
        return 0;
    }

    // ツリーは編集画面の上に重ねるため、内側に残った編集画面の罫線や
    // 行番号を消してから枠を描く。
    for(int y = box.pos.y + 1;y < box.pos.y + box.h - 1;y++){
        mvhline(y,box.pos.x + 1,' ',box.w - 2);
    }
    draw_box(box,ctx->win);
    return 0;
}