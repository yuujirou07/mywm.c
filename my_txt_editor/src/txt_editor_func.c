#include <limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "editor_types.h"
#include "error_log.h"
#include "ftj.h"
#include "txt_editor.h"
#include"txt_editor_syntax.h"

static int limit = 0;

// resize_file_browser(): 画面サイズに合わせてファイルブラウザの外枠と内側領域を作り直す。
// 一覧テーブルは1行DIR_ENTRY_NAME_MAXバイト固定の2次元配列なので、幅が変わっても
// 既存の内容はそのまま使える。行数が足りなくなったときだけ確保し直して読み直す。
// 引数: state=画面サイズ・ブラウザ状態・一覧テーブルを持つエディタ状態。
// 返り値: なし。
void resize_file_browser(struct editor_state *state){
    struct file_browse_state *browse = &state->file_browse;

    int old_h = browse->area.h;

    // main.cの初期化と同じ比率で作り直す
    struct box box;
    box.w     = state->scr.scr_size.x / 3;
    box.h     = state->scr.scr_size.y / 2;
    box.pos.x = (state->scr.scr_size.x / 2) - box.w / 2;
    box.pos.y = state->scr.scr_size.y / 4;

    // 枠の実体はstateが持つので、ここを書き換えれば描画側にもそのまま反映される。
    browse->box = box;
    browse->search_box = (struct box){
        .pos = {box.pos.x, box.pos.y + box.h - 1},
        .w = box.w,
        .h = 3,
    };

    //内側は枠の分だけ1セット内へ寄せる
    browse->area.pos.x = box.pos.x + 1;
    browse->area.pos.y = box.pos.y + 1;
    browse->area.w     = (box.w - 2 > 0) ? box.w - 2 : 0;
    browse->area.h     = (box.h - 2 > 0) ? box.h - 2 : 0;

    if(browse->area.h > browse->dir_name_table_rows){
        struct dir_entry *table =
            realloc(browse->dir_name_table,
                    (size_t)browse->area.h * sizeof(*table));
        if(table != NULL){
            browse->dir_name_table      = table;
            browse->dir_name_table_rows = browse->area.h;
        }
        else{
            // 確保できないときは既存テーブルに収まる行数まで削って範囲外書き込みを防ぐ
            browse->area.h = browse->dir_name_table_rows;
        }
    }

    // 幅が変わっても行の内容は有効なままなので、表示件数が変わる高さの変化のときだけ
    // ディレクトリを走査し直す(ドラッグ中に毎回走らせない)。
    if(browse->area.h != old_h){
        load_dir_table(state, &browse->dir_name_table,
            &browse->dir_name_table_rows,
            browse->path_name,
            browse->select_line.now_logical_line,
            &browse->dir_num,
            &browse->dir_name_table_num);
    }
}

// handle_resize(): 端末サイズ変更後に画面サイズと書き込み領域を更新し、
// 新しい区切り線の端点をctx->edit_screenへ書き戻す。
// カーソルはstate->cursorが論理位置で持っているため、リサイズで退避・復元する必要はない。
// 新しい可視幅で桁が溢れる場合だけ丸める。
// 引数: win=操作対象のncursesウィンドウ、ctx=更新するエディタ状態と区切り線座標を持つ入力context。
// 返り値: なし。
void handle_resize(WINDOW *win, struct editor_input_context *ctx){

    struct editor_state *state = ctx->state;
    bool is_cur_show = state->is_cur_show;
    int right_margin = state->scr.scr_size.x - state->write_area.x_end;
    int bottom_margin = state->scr.scr_size.y - state->write_area.y_end;

    getmaxyx(win, state->scr.scr_size.y, state->scr.scr_size.x);

    // 最小サイズ未満の間は編集画面を組み立てず、警告だけ出して十分な広さになるまで待つ。
    const char *resize_msg = "Please set the dimensions to at least 60 by 20 cells.";
    int resize_msg_len = strlen(resize_msg);

    while(state->scr.scr_size.x <= 60 || state->scr.scr_size.y < 25){
        my_cur_set(state,false);

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
    my_cur_set(state,is_cur_show);

    state->write_area.x_end = state->scr.scr_size.x - right_margin;
    state->write_area.y_end = state->scr.scr_size.y - bottom_margin;
    if(state->settings_data->show_status_bar){
        state->status_bar->w = state->scr.scr_size.x;
        state->status_bar->h = 1;
        if(state->settings_data->bar_side_state == top){
            state->status_bar->pos.y = state->write_area.y_start - state->status_bar->h;
        }
        else{
            state->status_bar->pos.y = state->write_area.y_end;
        }
    }
    // ファイルツリーを表示中なら、新しい画面幅でも寄せた左端を保つ。
    editor_apply_write_area(state);
    state->write_area.h = state->write_area.y_end - state->write_area.y_start;

    editor_sync_split_line(ctx);

    // 幅が縮むと桁が可視範囲外へ出るため、新しい可視幅で丸め直す。
    // 行番号は変わらないので、行方向はupdate_screen_ratio()側の判定に任せる。
    state->cursor.file_pos.x = editor_clamp_col(state, state->cursor.file_pos.y,
        state->cursor.file_pos.x);

    //各画面の枠やカーソル位置を新しい画面サイズの比率へ合わせ、再描画を要求する
    update_screen_ratio(ctx);
}

// handle_backspace(): カーソル左の1文字を削除し、行バッファを左へ詰める。
// 行頭では前の行末へカーソルを移動する。
// 引数: ctx=ウィンドウ、文字バッファ、行情報を持つ入力context。
// 返り値: なし。
void handle_backspace(struct editor_input_context *ctx) {
    struct editor_state *state = ctx->state;
    int line = state->cursor.file_pos.y;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    if (state->cursor.file_pos.x > 0) {
        // 行内の1文字削除。列容量が要るのはこちらの経路だけ。
        int col_limit = editor_col_limit(state, line);
        if(col_limit <= 0){
            return;
        }
        int del_pos = state->cursor.file_pos.x - 1;
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
        state->cursor.file_pos.x = del_pos;


    } else if (line > 0) {
        // 結合位置(=上の行の元の長さ)を結合前に控えておく。
        // 結合後はここがカーソル位置になる。上の行が5文字・下の行が3文字なら、
        // 8文字になった行の6文字目(桁5)へ置く。
        int join_col = editor_line_len(state, line - 1);

        if(remove_line_join_str_data(state,line) < 0){
            return;
        }

        // 移動先の行が画面内に残るなら行だけ動かし、画面先頭より上へ出るならスクロールする。
        if(line > state->scr.scr_start_num){
            editor_move_cursor_line(state, -1);
            state->render_flags |= RENDER_LINE_STATUS;
        }
        else{
            //関数内でcursor.file_pos.yの値も変更される
            editor_screen_move_line(ctx,-1);
        }
        //どちらの経路も桁は触らないため、ここで結合位置へ寄せる
        state->cursor.file_pos.x = editor_clamp_col(state, state->cursor.file_pos.y, join_col);
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    }
    state->render_flags |= RENDER_FILE_DATA;
}

// handle_newline(): カーソル位置で現在行を分割し、右側の文字列を下の新しい行へ移す。
// その後、カーソルを次の行の行頭(col=0)へ進める。
// 引数: ctx=ウィンドウ、カーソル行、書き込み領域を持つ入力context。
// 返り値: なし。
void handle_newline(struct editor_input_context *ctx) {
    struct editor_state *state = ctx->state;
    int line = state->cursor.file_pos.y;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    // カーソルから右側を新しい行へ切り出す。行スロットの伸長もこの中で行うため、
    // editor_line_limit()が広がる可能性がある。カーソルを動かす前に呼ぶ必要がある
    // (分割位置は現在のカーソル桁から決まるため)。
    if(make_new_line_space(state, line) < 0){
        return;
    }

    // 次の行が編集領域の中に収まるならカーソル行だけ進め、下端なら画面をスクロールする。
    if (line - state->scr.scr_start_num + 1 < state->write_area.h){
        editor_move_cursor_line(state, 1);
    }
    else{
        // editor_screen_move_line()がcursor.file_pos.yの+1も行うため、ここでは動かさない
        editor_screen_move_line(ctx,1);
    }
    state->cursor.file_pos.x = 0;

    if(state->cursor.file_pos.y >= state->file_data.description_line_end){
        //行カウントは0から始まるので1足す
        state->file_data.description_line_end = state->cursor.file_pos.y + 1;
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
    int line = state->cursor.file_pos.y;
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
    (void)win;

    int line = state->cursor.file_pos.y;
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
    state->cursor.file_pos.x = editor_clamp_int(state->cursor.file_pos.x, 0, view_cols - 1);

    int writing_area = state->cursor.file_pos.x;
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

    state->cursor.file_pos.x = writing_area + char_width;
    //自動で改行する仕様。判定は挿入前の桁で行う(可視幅の右端に居たかどうか)。
    if (writing_area >= view_cols - 1 && line + 1 < editor_line_limit(state)){
        editor_move_cursor_line(state, 1);
        state->cursor.file_pos.x = 0;
        if(state->cursor.file_pos.y >= state->file_data.description_line_end){
            //行カウントは0から始まるので1足す
            state->file_data.description_line_end = state->cursor.file_pos.y + 1;
        }
    }
    state->render_flags |= RENDER_FILE_DATA;
}

// handle_mouse(): マウスホイールで表示開始行を上下に動かし、
// カーソルが表示範囲外へ出る場合は一時的に非表示にする。
// 引数: ctx=ウィンドウ、マウスイベント、表示状態を持つ入力context、dir_num=ディレクトリ選択行。
// 返り値: なし。
void handle_mouse(struct editor_input_context *ctx,int dir_num) {
    WINDOW *win = ctx->win;
    MEVENT *event = ctx->mouse_event;
    struct editor_state *state = ctx->state;

    if (getmouse(event) != OK){
        return;
    }
    switch(editor_get_screen_state(state)){
        case edit_screen:{
            int old_scr_start_num = state->scr.scr_start_num;
            editor_screen_mouse_event(ctx);
            if(old_scr_start_num != state->scr.scr_start_num){
                state->render_flags |= RENDER_EDIT_SCREEN_BASE;
                state->render_flags |= RENDER_FILE_DATA;
            }
            break;
        }
        case file_browse_screen:{
            file_browse_screen_mouse_event(win,event,state,dir_num);
            break;
        }
        case filetree_screen:{
            filetree_mouse_event(ctx);
            break;
        }
        default:
            break;
    }
}

// handle_input_allow(): 矢印キー入力を処理し、行長を超えない位置へカーソルを移動する。
// 画面端ではスクロールしながら表示内容を補う。
// 引数: ctx=カーソルと表示位置を持つ入力context、ch=KEY_UP/DOWN/LEFT/RIGHT。
// 返り値: なし。
void handle_input_allow(struct editor_input_context *ctx,wchar_t ch){
    struct editor_state *state = ctx->state;
    int line_limit = get_line_limit();
    if(line_limit <= 0)return;
    

    // 画面内に留まったまま動けるかどうかの判定。画面座標ではなく
    // 「カーソル行が表示範囲のどこにいるか」で決める。
    int line = state->cursor.file_pos.y;
    bool can_move_up_in_view   = (line > state->scr.scr_start_num);
    bool can_move_down_in_view = (line - state->scr.scr_start_num + 1 < state->write_area.h);

    switch(ch){
        case KEY_UP:{
            if (can_move_up_in_view && line > 0) {
                editor_move_cursor_line(state, -1);
            }
            else if(state->scr.scr_start_num > 0){
              editor_screen_move_line(ctx,-1);
            }
            break;
        }
        case KEY_DOWN:{
            if(line + 1 >= line_limit)break;
            if (can_move_down_in_view)editor_move_cursor_line(state, 1);
            else editor_screen_move_line(ctx,1);
            break;
        }
        case KEY_LEFT:{
            if (state->cursor.file_pos.x > 0)state->cursor.file_pos.x--;
            else if (can_move_up_in_view && line > 0) {
                editor_move_cursor_line(state, -1);
                state->cursor.file_pos.x = editor_clamp_col(state, state->cursor.file_pos.y,
                    editor_line_len(state, state->cursor.file_pos.y));
            }
            break;
        }
        case KEY_RIGHT:{
            if(line < 0 || line >= line_limit){
                break;
            }
            if (state->cursor.file_pos.x < editor_clamp_col(state, line, editor_line_len(state, line))){
                state->cursor.file_pos.x++;
            }
            else if (can_move_down_in_view && line + 1 < line_limit) {
                editor_move_cursor_line(state, 1);
                state->cursor.file_pos.x = 0;
            }
            break;
        }
    }
    state->render_flags |= RENDER_LINE_STATUS;
}

// set_line_limit(): カーソル移動などが参照する共有行数上限を更新する。
// 引数: line_limit=新しい行数上限。
// 返り値: なし。
void set_line_limit(int line_limit){
    limit = line_limit;
}

// get_line_limit(): 共有されている行数上限を返す。
// 引数: なし。
// 返り値: set_line_limit()で最後に設定した値。
int get_line_limit(){
    return limit;
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

    // 分割位置はカーソルの現在桁。端末へ問い合わせず、モデルの値をそのまま使う。
    // 行長を超えないよう丸める。
    int col = editor_clamp_int(state->cursor.file_pos.x, 0, old_len);

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


// editor_screen_mouse_event(): ホイールで表示開始行だけを動かす。
// スクロールは編集位置を変えないため、cursorは書き換えない。カーソルの画面座標は
// scr_start_numから自動的にずれるので、表示可否だけを取り直す。
// 引数: ctx=ウィンドウ、getmouse()済みのイベント、表示位置とカーソルを持つ入力context。
// 返り値: なし。
void editor_screen_mouse_event(struct editor_input_context *ctx){
    MEVENT *event = ctx->mouse_event;
    struct editor_state *state = ctx->state;

    int line_limit = editor_line_limit(state);
    bool can_scroll_down = state->scr.scr_start_num + state->write_area.h < line_limit;

    //エディター画面下スクロール処理
    if (event->bstate & BUTTON5_PRESSED && can_scroll_down) {
        state->scr.scr_start_num++;
        if(state->settings_data->built_in_syntax){
            scroll_syntax_pos_data(-1,state->write_area.h);
            update_line_syntax_data(ctx,state->write_area.h - 1);
        }
        
    //エディター画面上スクロール処理
    } else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0) {
        state->scr.scr_start_num--;
        if(state->settings_data->built_in_syntax){
            scroll_syntax_pos_data(+1,state->write_area.h);
            update_line_syntax_data(ctx,0);
        }
    }
    else if(event->bstate & BUTTON1_PRESSED){
        int x = event->x;
        int y = event->y;

        if(y < state->write_area.y_start || y >= state->write_area.y_end ||
            x < state->write_area.x_start || x >= state->write_area.x_end){
            return;
        }
        struct pos write_area_pos;
        write_area_pos.y = y - state->write_area.y_start;
        write_area_pos.x = x - state->write_area.x_start;
        int line_num = state->scr.scr_start_num + write_area_pos.y;
        editor_set_cursor(state,line_num,write_area_pos.x);
    }
    else{
        return;
    }

    // カーソル行が編集領域から出たら隠す。戻ってきたらまた出す。
    state->is_cur_show = editor_cursor_is_visible(state);
}

// file_browse_screen_mouse_event(): ホイール入力でファイルブラウザの選択行を循環移動する。
// 引数: win=描画先。現在は未使用、event=マウスイベント、state=選択状態、dir_num=表示項目数。
// 返り値: なし。選択行の強調表示が無効なら何もしない。
void file_browse_screen_mouse_event(WINDOW *win, MEVENT *event, struct editor_state *state,int dir_num){
    //ホイールで選択行を動かすだけなので描画先ウィンドウは使わない
    (void)win;

    if(state->settings_data->file_select_scene_lighting){
        if(event->bstate & BUTTON4_PRESSED){
             // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line = (state->file_browse.select_line.now_line <= 0) 
                ? dir_num - 1:state->file_browse.select_line.now_line - 1;
            set_file_select_line(state, dir_num, next_line);
            
        }  
        if(event->bstate & BUTTON5_PRESSED){
            // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line = (state->file_browse.select_line.now_line  >= dir_num - 1)
                ?0:state->file_browse.select_line.now_line + 1;
            set_file_select_line(state, dir_num, next_line);
        }      
    }
}


// set_file_browse_path_input_mode(): ファイルブラウザのパス入力モードを設定する。
// 引数: file_browse=更新対象、flag=設定する有効状態。
// 返り値: なし。file_browseがNULLなら何もしない。
void set_file_browse_path_input_mode(struct file_browse_state *file_browse,bool flag){
    if(file_browse == NULL)return;
    file_browse->path_input_mode = flag;
}

// get_file_browse_path_input_mode(): ファイルブラウザのパス入力モードを取得する。
// 引数: file_browse=取得元。
// 返り値: 現在の有効状態。file_browseがNULLならfalse。
bool get_file_browse_path_input_mode(struct file_browse_state *file_browse){
    if(file_browse == NULL)return 0;
    return file_browse->path_input_mode;
}

// my_cur_set(): エディタ状態とncursesのカーソル表示状態を同時に更新する。
// 引数: state=更新対象、set=trueで表示、falseで非表示。
// 返り値: なし。stateがNULLなら何もしない。curs_set()の失敗は通知しない。
void my_cur_set(struct editor_state *state,bool set){
    if(state == NULL)return;
    state->is_cur_show = set;
    curs_set(set);
}

// cur_pos_push(): 次回ncursesへ反映する画面カーソル座標をstateへ保存する。
// 引数: pos=保存する画面座標、state=保存先のエディタ状態。posは値コピーされる。
// 返り値: 常に0。stateがNULLの場合の動作は未定義。
int cur_pos_push(struct pos pos, struct editor_state *state){
    state->cursor.show_cur_pos_queue = pos;
    return 0;
}

// set_cur_pos(): stateに保存された画面カーソル座標をncursesへ反映する。
// 引数: state=反映する座標を持つエディタ状態。
// 返り値: 常に0。move()の失敗は呼び出し元へ通知しない。
int set_cur_pos(struct editor_state *state){
    move(state->cursor.show_cur_pos_queue.y,state->cursor.show_cur_pos_queue.x);
    return 0;
}

int filetree_mouse_event(struct editor_input_context *ctx){
    MEVENT *ev = ctx->mouse_event;
    struct editor_state *state = ctx->state;
    if(ev->bstate & BUTTON1_PRESSED){
        int x = ev->x;
        int y = ev->y;

        struct box ft_box = ctx->state->file_tree_data.ft_box;
        if(ft_box.pos.x >= x)return 0;
        else if(ft_box.pos.x + ft_box.w <= x)return 0;
        if(ft_box.pos.y >= y)return 0;
        else if(ft_box.pos.y + ft_box.h <= y)return 0;

        for(int i = 0;i < state->file_tree_data.open_count_num;i++){
            ft_path_open_check_data *item =
                &state->file_tree_data.open_check_data[i];
            if(item->screen_y != y)continue;

            enum select_state path_state =
                get_path_state(item->table_ptr->absolute_path);
            if(path_state == folder){
                item->is_open = !item->is_open;
                state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            }
            else if(path_state == file){
                struct file_browse_select_state select_state;
                load_file(state,NULL,0,item->table_ptr->absolute_path,&select_state);
                if(select_state.select_state == file){
                    load_screen_size(state);
                    editor_set_cursor(state,0,0);
                    state->render_flags |= RENDER_FILE_DATA;
                    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
                    if(state->settings_data->built_in_syntax){
                        syntax *syntax_data = now_usint_syntax_ptr_ctl(NULL,get);
                        if(syntax_data != NULL)set_syntax_data(syntax_data,ctx);
                    }
                }
            }
            break;
        }
    }
    return 0;
}
