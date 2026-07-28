#include <limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "txt_editor.h"

static int limit = 0;

// resize_file_browser(): 画面サイズに合わせてファイルブラウザの外枠と内側領域を作り直す。
// 一覧テーブルは「内側の幅×高さ」の固定幅レイアウトなので、幅が変わると既存の内容は
// 行の区切り位置がずれて無効になる。必要なら確保し直したうえで読み直す。
// 引数: ctx=画面サイズ・ブラウザ枠・一覧テーブルを持つ入力context。
// 返り値: なし。
void resize_file_browser(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;

    int old_w = state->file_browser_area.w;
    int old_h = state->file_browser_area.h;

    // main.cの初期化と同じ比率で作り直す
    struct box box;
    box.w     = state->scr.scr_size.x / 3;
    box.h     = state->scr.scr_size.y / 2;
    box.pos.x = (state->scr.scr_size.x / 2) - box.w / 2;
    box.pos.y = state->scr.scr_size.y / 4;

    // 枠の実体はmain.cのローカルで、state->file_browser_boxがそれを指している。
    // ctx側は値コピーを持っているため、両方を更新しないと表示がずれる。
    if(state->file_browser_box != NULL){
        *state->file_browser_box = box;
    }
    ctx->file_browse_box = box;

    //内側は枠の分だけ1セット内へ寄せる
    state->file_browser_area.pos.x = box.pos.x + 1;
    state->file_browser_area.pos.y = box.pos.y + 1;
    state->file_browser_area.w     = (box.w - 2 > 0) ? box.w - 2 : 0;
    state->file_browser_area.h     = (box.h - 2 > 0) ? box.h - 2 : 0;

    long need = (long)state->file_browser_area.w * state->file_browser_area.h;
    if(need > ctx->dir_name_table_size){
        char *table = realloc(ctx->dir_name_table, (size_t)need);
        if(table != NULL){
            ctx->dir_name_table      = table;
            ctx->dir_name_table_size = (int)need;
        }
        else{
            // 確保できないときは既存テーブルに収まる行数まで削って範囲外書き込みを防ぐ
            state->file_browser_area.h = (state->file_browser_area.w > 0)
                ? ctx->dir_name_table_size / state->file_browser_area.w : 0;
        }
    }

    // 幅が変われば行の区切り位置が、高さが変われば表示件数が変わるため読み直す。
    // サイズが同じならディレクトリ走査を省く(ドラッグ中に毎回走らせない)。
    if(state->file_browser_area.w != old_w || state->file_browser_area.h != old_h){
        load_dir_table(state, ctx->dir_name_table, ctx->dir_name_table_size,
                       ctx->path_name);
    }
}

// handle_resize(): 端末サイズ変更後に画面サイズと書き込み領域を更新し、
// 新しい区切り線の端点をctx->line_start_pos/line_end_posへ書き戻してカーソル位置を復元する。
// 引数: win=操作対象のncursesウィンドウ、ctx=更新するエディタ状態と区切り線座標を持つ入力context。
// 返り値: なし。
void handle_resize(WINDOW *win, struct editor_input_context *ctx){

    struct editor_state *state = ctx->state;

    int cx, cy;
    getyx(win, cy, cx);
    getmaxyx(win, state->scr.scr_size.y, state->scr.scr_size.x);

    // 最小サイズ未満の間は編集画面を組み立てず、警告だけ出して十分な広さになるまで待つ。
    const char *resize_msg = "Please set the dimensions to at least 60 by 20 cells.";
    int resize_msg_len = strlen(resize_msg);

    while(state->scr.scr_size.x <= 60 || state->scr.scr_size.y < 25){
        curs_set(0);

        // 画面幅に収まらない場合は右端で切り詰めてから中央へ寄せる
        int draw_len = (resize_msg_len < state->scr.scr_size.x)
            ? resize_msg_len : state->scr.scr_size.x;
        int msg_x = (state->scr.scr_size.x - draw_len) / 2;
        int msg_y = state->scr.scr_size.y / 2;

        clear();
        attron(COLOR_PAIR(3));
        mvaddnstr(msg_y, msg_x, resize_msg, draw_len);
        attroff(COLOR_PAIR(3));
        refresh();

        // リサイズ以外の入力は捨て、サイズが変わるたびに新しい中央へ描き直す
        if(getch() == KEY_RESIZE){
            getmaxyx(win, state->scr.scr_size.y, state->scr.scr_size.x);
        }
    }
    curs_set(state->is_cur_show ? 1 : 0);

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

    ctx->line_start_pos = (struct pos){state->write_area.x_start-1,state->write_area.y_start};
    ctx->line_end_pos   = (struct pos){state->write_area.x_start-1,state->write_area.y_end};

    

    //ファイルブラウザ表示中は編集画面ではなくブラウザを描き直す
    switch(editor_get_screen_state(state)){
        case file_browse_screen:
        case start_menu_file_browse_screen:
            state->render_flags |= RENDER_FILE_BROWSE;
            break;
        default:
            state->render_flags |= RENDER_FILE_DATA;
            break;
    }
}

// handle_backspace(): カーソル左の1文字を削除し、行バッファを左へ詰める。
// 行頭では前の行末へカーソルを移動する。
// 引数: win=現在カーソル位置を持つウィンドウ、state=文字バッファと行情報。
// 返り値: なし。
void handle_backspace(WINDOW *win, struct editor_state *state) {
    int x, y;
    getyx(win, y, x);
    int line = state->mouse.now_mouce_line;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    if (x > state->write_area.x_start) {
        // 行内の1文字削除。列容量が要るのはこちらの経路だけ。
        int col_limit = editor_col_limit(state, line);
        if(col_limit <= 0){
            return;
        }
        int del_pos   = (x - 1) - state->write_area.x_start;
        if(del_pos < 0 || del_pos >= col_limit || del_pos >= state->str.line[line]){
            return;
        }
        wint_t *cells = editor_line_cells(state, line);
        if(cells == NULL){
            return;
        }
        state->str.line[line]--;

        int new_len = state->str.line[line];

        //画面外にある桁も含めて行末まで詰める
        int move_count = new_len - del_pos;
        if (move_count > 0)
            memmove(&cells[del_pos], &cells[del_pos + 1],
                    move_count * sizeof(wint_t));

        cells[new_len] = 0;
        move(y, x - 1);

        
    } else if (state->mouse.now_mouce_line > 0) {
        // 結合位置(=上の行の元の長さ)を結合前に控えておく。
        // 結合後はここがカーソル位置になる。上の行が5文字・下の行が3文字なら、
        // 8文字になった行の6文字目(桁5)へ置く。
        int join_col = editor_line_len(state, state->mouse.now_mouce_line - 1);

        if(remove_line_join_str_data(state,state->mouse.now_mouce_line) < 0){
            return;
        }

        if(state->write_area.y_start < y){
            editor_move_cursor_line(state, -1);
            move(y - 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line,
                                                state->write_area.x_start + join_col));
            state->render_flags |= RENDER_LINE_STATUS;
        }
        else{
            //関数内でmouse.now_mouce_lineの値も変更される
            editor_screen_move_line(state,win,-1);
            //editor_screen_move_line()はx座標を触らないため、ここで結合位置へ寄せる
            move(y, editor_cursor_x_on_line(state, state->mouse.now_mouce_line,
                                            state->write_area.x_start + join_col));
        }
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    }
    state->render_flags |= RENDER_FILE_DATA;
}

// handle_newline(): カーソル位置で現在行を分割し、右側の文字列を下の新しい行へ移す。
// その後、次の画面行の先頭へカーソルを移動し、編集中の論理行番号(now_mouce_line)を1つ進める。
// 引数: win=改行操作を反映するウィンドウ、state=カーソル行と書き込み領域。
// 返り値: なし。
void handle_newline(WINDOW *win, struct editor_state *state) {
    int line = state->mouse.now_mouce_line;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    // カーソルから右側を新しい行へ切り出す。行スロットの伸長もこの中で行うため、
    // editor_line_limit()が広がる可能性がある。カーソルを動かす前に呼ぶ必要がある
    // (分割位置は現在のカーソル桁から決まるため)。
    if(make_new_line_space(state, line) < 0){
        return;
    }

    int y = getcury(win);
    if (y + 1 < state->write_area.y_end){
        editor_move_cursor_line(state, 1);
        
        move(y + 1, state->write_area.x_start);
    }
    else{
        // editor_screen_move_line()がnow_mouce_lineの+1も行うため、ここでは動かさない
        editor_screen_move_line(state,win,1);
        move(y, state->write_area.x_start);
    }

    if(state->mouse.now_mouce_line >= state->file_data.description_line_end){
        //行カウントは0から始まるので1足す
        state->file_data.description_line_end = state->mouse.now_mouce_line + 1;
    }
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// handle_tab(): indent_range個の空白をカーソル位置へ挿入する。
// 以前はaddch(' ')で画面へ出すだけでバッファへ書いていなかったため、
// 再描画で消え、保存にも残らなかった。空白1文字の挿入を繰り返す形にして、
// バッファへの書き込み・行長更新・容量伸長をhandle_char_input()へ一本化する。
// 引数: win=描画先ウィンドウ、state=編集バッファ。
// 返り値: なし。
void handle_tab(WINDOW *win, struct editor_state *state) {
    int line = state->mouse.now_mouce_line;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    for(int i = 0; i < state->settings_data->indent_range; i++){
        handle_char_input(win, L' ', state);
    }
}

// handle_char_input(): 通常文字をカーソル位置へ挿入する。
// 既存文字がある場所では後続文字を右へずらしてから書き込む。
// 引数: win=描画先ウィンドウ、ch=挿入するwide文字、state=編集バッファ。
// 返り値: なし。
void handle_char_input(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);

    int line = state->mouse.now_mouce_line;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    int char_width = wcwidth(ch);
    if(char_width < 1){
        char_width = 1;
    }

    int view_cols = editor_view_cols(state);
    if(view_cols <= 0){
        return;
    }
    x = editor_clamp_int(x, state->write_area.x_start, state->write_area.x_start + view_cols - 1);
    move(y, x);

    int writing_area = x - state->write_area.x_start;
    if(writing_area < 0 || writing_area >= view_cols){
        return;
    }

    // 挿入後に必要となる桁数を先に確保する。行末追記なら書き込み位置+文字幅、
    // 途中挿入なら既存の行長+文字幅まで伸びる。
    int line_len = editor_line_len(state, line);
    int need = (writing_area < line_len) ? line_len + char_width
                                         : writing_area + char_width;
    if(!editor_ensure_line_cap(state, line, need)){
        return;
    }

    int col_limit = editor_col_limit(state, line);
    if(col_limit <= 0 || writing_area + char_width > col_limit){
        return;
    }

    wint_t *cells = editor_line_cells(state, line);
    if(cells == NULL){
        return;
    }

    if(writing_area < line_len){
        //画面外にある桁も含めて右へずらす
        int insert_count = line_len - writing_area;
        if (insert_count > 0)
            memmove(&cells[writing_area + char_width], &cells[writing_area],
                    insert_count * sizeof(wint_t));
    }

    cells[writing_area] = ch;
    for(int i = 1; i < char_width; i++){
        cells[writing_area + i] = 0;
    }
    if(state->str.line[line] <= writing_area){
        state->str.line[line] = writing_area + char_width;
    }
    else if(state->str.line[line] + char_width <= editor_line_cap(state, line)){
        state->str.line[line] += char_width;
    }

    // 保存対象はdescription_line_endまでなので、それより後ろの行へ書き込んだら伸ばす。
    // これが無いと、矢印キーで下へ移動して打った内容が丸ごと保存されない。
    if(line >= state->file_data.description_line_end){
        //行カウントは0から始まるので1足す
        state->file_data.description_line_end = line + 1;
    }

    move(y, x + char_width);
    //自動で改行する仕様
    if (x >= state->write_area.x_end-1 && state->mouse.now_mouce_line + 1 < editor_line_limit(state)){
        move(y + 1, state->write_area.x_start);
        editor_move_cursor_line(state, 1);
        if(state->mouse.now_mouce_line >= state->file_data.description_line_end){
            //行カウントは0から始まるので1足す
            state->file_data.description_line_end = state->mouse.now_mouce_line + 1;
        }
    }
    state->render_flags |= RENDER_FILE_DATA;
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

    if (event->bstate & BUTTON5_PRESSED && editor_get_screen_state(state) == edit_screen && can_scroll_down) {
        if (state->scr.scr_start_num < state->mouse.now_mouce_line){
            move(editor_clamp_int(y - 1, state->write_area.y_start, state->write_area.y_end - 1), x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num++;

    } else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0 &&
               editor_get_screen_state(state) == edit_screen) {
        if(state->scr.scr_start_num + state->write_area.y_end >= state->mouse.now_mouce_line){
            move(editor_clamp_int(y + 1, state->write_area.y_start, state->write_area.y_end - 1), x);
        }
        else{
            state->is_cur_show = false;
        }

        state->scr.scr_start_num--;

        if(state->scr.scr_start_num <= state->mouse.now_mouce_line){
            if(state->is_cur_show == false){
                move(state->write_area.y_start, x);
                state->is_cur_show = true;
            }
        }
    }
    curs_set(state->is_cur_show ? 1 : 0);
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// handle_input_allow(): 矢印キー入力を処理し、行長を超えない位置へカーソルを移動する。
// 画面端ではスクロールしながら表示内容を補う。
// 引数: win=カーソル移動対象のウィンドウ、ch=KEY_UP/DOWN/LEFT/RIGHT、state=行長と表示位置。
// 返り値: なし。
void handle_input_allow(WINDOW *win, wchar_t ch, struct editor_state *state){
    int x, y;
    getyx(win, y, x);
    int line_limit = get_line_limit();
    if(line_limit <= 0){
        return;
    }

    switch(ch){
        case KEY_UP:{
            if (y > state->write_area.y_start && state->mouse.now_mouce_line > 0) {
                editor_move_cursor_line(state, -1);
                move(y - 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line, x));
            }
            else if(state->scr.scr_start_num > 0){
              editor_screen_move_line(state, win,-1);
            }
            break;
        }
        case KEY_DOWN:{
            if(state->mouse.now_mouce_line + 1 >= line_limit){
                break;
            }
            if (y + 1 < state->write_area.y_end) {
                editor_move_cursor_line(state, 1);
                move(y + 1, editor_cursor_x_on_line(state, state->mouse.now_mouce_line, x));
            }
            else{
                editor_screen_move_line(state,win,1);
            }
            break;
        }
        case KEY_LEFT:{
            if (x > state->write_area.x_start)
                move(y, x - 1);
            else if (y > state->write_area.y_start && state->mouse.now_mouce_line > 0) {
                editor_move_cursor_line(state, -1);
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
                editor_move_cursor_line(state, 1);
                move(y + 1, state->write_area.x_start);
            }
            break;
        }
    }
    state->render_flags |= RENDER_LINE_STATUS;
}

void set_line_limit(int line_limit){
    limit = line_limit;
}
int get_line_limit(){
    return limit;
}

char *uint_to_str(unsigned int value, char *buf)
{
    char tmp[10];
    int i = 0;
    int j = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    while (value > 0) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0) {
        buf[j++] = tmp[--i];
    }

    buf[j] = '\0';

    return buf;
}

// remove_line_join_str_data(): remove_line_num行目を削除し、内容を直前の行の末尾へ連結する。
// 連続レイアウトなので削除行のセルは前の行の直後に隣接している。
// セル自体は前の行の末尾へ詰めるだけでよく、後続行のデータやline_offsetは動かさない
// (削除行の領域を前の行がまるごと吸収するので、境界が1つ消えるだけで済む)。
// 引数: state=編集バッファ、remove_line_num=削除する論理行番号(1以上)。
// 返り値: 連結後の前行の桁数。引数が不正なら-1。
int remove_line_join_str_data(struct editor_state *state,long remove_line_num){
    if(state == NULL || state->str.line_offset == NULL || state->str.line_cap == NULL ||
       state->str.line == NULL || remove_line_num < 1 ||
       remove_line_num >= state->str.line_capacity){
        return -1;
    }

    int prev   = (int)remove_line_num - 1;
    int target = (int)remove_line_num;

    wint_t *prev_cells   = editor_line_cells(state, prev);
    wint_t *target_cells = editor_line_cells(state, target);
    if(prev_cells == NULL || target_cells == NULL){
        return -1;
    }

    int prev_len = state->str.line[prev];
    int join_len = state->str.line[target];

    // 前の行の実データ末尾へ、削除行の実データをそのまま詰める
    if(join_len > 0){
        memmove(&prev_cells[prev_len], target_cells, (size_t)join_len * sizeof(wint_t));
    }

    // 削除行の領域終端までを前の行が吸収する
    long region_end = state->str.line_offset[target] + state->str.line_cap[target];
    int joined_len = prev_len + join_len;
    int joined_cap = (int)(region_end - state->str.line_offset[prev]);

    // memmoveは移動元を消さないため、結合部より後ろに古いセルが残る。
    // line[]までしか通常は読まないが、行が伸びたときに露出しないよう0で埋めておく。
    if(joined_cap > joined_len){
        memset(&prev_cells[joined_len], 0,
               (size_t)(joined_cap - joined_len) * sizeof(wint_t));
    }
    state->str.line[prev]     = joined_len;
    state->str.line_cap[prev] = joined_cap;

    // 削除行より後ろの行情報を1つ前へ詰めて、境界を1つ消す
    int move_count = state->str.line_capacity - target - 1;
    if(move_count > 0){
        memmove(&state->str.line_offset[target], &state->str.line_offset[target + 1],
                sizeof(long) * (size_t)move_count);
        memmove(&state->str.line_cap[target], &state->str.line_cap[target + 1],
                sizeof(int) * (size_t)move_count);
        memmove(&state->str.line[target], &state->str.line[target + 1],
                sizeof(int) * (size_t)move_count);
    }

    // 詰めた分だけ空いた末尾スロットを、未確保として扱われる状態にしておく
    int last = state->str.line_capacity - 1;
    state->str.line[last]        = 0;
    state->str.line_cap[last]    = 0;
    state->str.line_offset[last] = state->str.total_capacity;

    if(state->file_data.description_line_end > 0){
        state->file_data.description_line_end--;
    }
    if(state->file_data.file_line_start_num_counter > 0){
        state->file_data.file_line_start_num_counter--;
    }

    return joined_len;
}


// make_new_line_space(): make_space_line_num行目をカーソル位置で分割し、
// カーソルから右側の文字列を新しい行(make_space_line_num+1)として下に作る。
// remove_line_join_str_data()が2行の容量域を1つへ併合するのの逆操作にあたり、
// 連続レイアウトなので物理セルは一切動かさず、元の行が持っていた容量域を
// 2つに割り直すだけで済む(左側=元の行、右側=新しい行)。
// 引数: state=編集バッファ、make_space_line_num=分割する論理行(カーソルがいる行)。
// 返り値: 新しくできた行の桁数。引数が不正・確保失敗なら-1。
int make_new_line_space(struct editor_state *state,long make_space_line_num){
    if(state == NULL || state->str.line == NULL || state->str.line_offset == NULL ||
       state->str.line_cap == NULL || make_space_line_num < 0 ||
       make_space_line_num >= editor_line_limit(state)){
        return -1;
    }

    int line    = (int)make_space_line_num;
    int old_len = state->str.line[line];

    // 分割位置はカーソルの現在桁(この関数はwin引数を持たないため、
    // handle_tab()と同様にstdscr基準で読む)。行長を超えないよう丸める。
    int x, y;
    getyx(stdscr, y, x);
    (void)y;
    int col = x - state->write_area.x_start;
    col = editor_clamp_int(col, 0, old_len);

    // 分割後、行make_space_line_num+1が実データとして加わる分だけ、
    // 「実際に使用中の行数」を伸ばす必要がある。
    long used_rows = state->file_data.description_line_end;
    if(used_rows < make_space_line_num + 1){
        used_rows = make_space_line_num + 1;
    }
    if(used_rows > INT_MAX - 2){
        return -1;
    }

    // 挿入先(line+1)を空けるための空き行スロットが末尾に無ければ、行情報配列を伸ばす
    if(!editor_ensure_row_capacity(state, (int)used_rows + 1)){
        return -1;
    }

    int target = line + 1;

    // targetより後ろの実データ行(target..used_rows-1)を1つ後ろへずらし、
    // targetの位置を新しい行のために空ける
    int move_count = (int)used_rows - target;
    if(move_count > 0){
        memmove(&state->str.line_offset[target + 1], &state->str.line_offset[target],
                sizeof(long) * (size_t)move_count);
        memmove(&state->str.line_cap[target + 1], &state->str.line_cap[target],
                sizeof(int) * (size_t)move_count);
        memmove(&state->str.line[target + 1], &state->str.line[target],
                sizeof(int) * (size_t)move_count);
    }

    // lineが持っていた容量域を2つに割り直す。物理セル(wint_line_str_data)は動かさない。
    long old_offset = state->str.line_offset[line];
    int  old_cap    = state->str.line_cap[line];
    int  new_len    = old_len - col;

    state->str.line[line]     = col;
    state->str.line_cap[line] = col;

    state->str.line_offset[target] = old_offset + col;
    state->str.line_cap[target]    = old_cap - col;
    state->str.line[target]        = new_len;

    // 元の行に余白が無い状態で行末分割すると、割り当てる容量が0の行ができてしまう。
    // 容量0の行は編集経路の列容量チェックに引っかかって操作を受け付けなくなるため、
    // 最低限の余白を持たせておく(伸長は後続行のoffsetまで面倒を見てくれる)。
    if(state->str.line_cap[target] <= 0){
        editor_ensure_line_cap(state, target, EDITOR_LINE_COL_SLACK);
    }
    if(state->str.line_cap[line] <= 0){
        editor_ensure_line_cap(state, line, EDITOR_LINE_COL_SLACK);
    }

    state->file_data.description_line_end = used_rows + 1;
    if(state->file_data.file_line_start_num_counter <
       state->settings_data->default_load_line_size){
        state->file_data.file_line_start_num_counter++;
    }

    return new_len;
}