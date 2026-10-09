#include <limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "default_settings.h"
#include "editor_types.h"
#include "error_log.h"
#include "filetree.h"
#include "ftj.h"
#include "input_complete.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"
#include"txt_editor_syntax.h"
#include"editor_types.h"

static int limit = 0;

// stateの画面寸法からブラウザの枠・入力欄・内側領域を再計算し、高さ変更時に一覧を再読込する。
// 返り値: なし。一覧の容量不足は再確保し、失敗時は表示行数を既存容量へ制限する。
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

// winの寸法からctxの編集領域・各画面配置・カーソル列を更新し、再描画を要求する。
// 返り値: なし。幅61セル・高さ25行未満では、サイズが足りるまで入力を待ち続ける。
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
    state->cursor.file_pos.x = editor_cursor_col_boundary(state, state->cursor.file_pos.y,
        state->cursor.file_pos.x);

    //各画面の枠やカーソル位置を新しい画面サイズの比率へ合わせ、再描画を要求する
    update_screen_ratio(ctx);
}

// ctxのカーソル左の1文字をセル幅ごと削除する。行頭では前行へ結合して結合位置へ移動する。
// 返り値: なし。不正な行・セルや結合失敗では処理を打ち切り、変更時は本文の再描画を要求する。
void handle_backspace(struct editor_input_context *ctx) {
    struct editor_state *state = ctx->state;
    int line = state->cursor.file_pos.y;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }
    state->cursor.file_pos.x = editor_cursor_col_boundary(state, line,
        state->cursor.file_pos.x);

    if (state->cursor.file_pos.x > 0) {
        // 行内の1文字削除。列容量が要るのはこちらの経路だけ。
        int col_limit = editor_col_limit(state, line);
        if(col_limit <= 0){
            return;
        }
        int del_pos = editor_previous_char_col(state, line, state->cursor.file_pos.x);
        if(del_pos < 0 || del_pos >= col_limit || del_pos >= state->str.line[line]){
            return;
        }
        wint_t *cells = editor_line_cells(state, line);
        if(cells == NULL){
            return;
        }
        int old_len = editor_line_len(state, line);
        int char_width = wcwidth((wchar_t)cells[del_pos]);
        if(char_width < 1){
            char_width = 1;
        }
        if(del_pos + char_width > old_len){
            char_width = old_len - del_pos;
        }
        int new_len = old_len - char_width;
        state->str.line[line] = new_len;

        //画面外にある桁も含めて行末まで詰める
        int move_count = old_len - del_pos - char_width;
        if (move_count > 0)
            memmove(&cells[del_pos], &cells[del_pos + char_width],
                    move_count * sizeof(wint_t));

        memset(&cells[new_len], 0, char_width * sizeof(wint_t));
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

// カーソル位置で現在行を分割し、右側の文字列を下の新しい行へ移す。その後、カーソルを次の行の行頭(col=0)へ進める。
// 引数: ctx=ウィンドウ、カーソル行、書き込み領域を持つ入力context。 返り値: なし。
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

// stateのカーソル位置へindent_range個の空白を、handle_char_inputを通して順に挿入する。
// 引数: win=文字入力関数へ渡すウィンドウ、state=編集状態。返り値: なし。挿入できない文字があっても通知しない。
void handle_tab(WINDOW *win, struct editor_state *state) {
    int line = state->cursor.file_pos.y;
    if(line < 0 || line >= editor_line_limit(state)){
        return;
    }

    for(int i = 0; i < state->settings_data->indent_range; i++){
        handle_char_input(win, L' ', state);
    }
}

// stateのカーソル位置へワイド文字chを挿入し、後続セル・行長・カーソル・補完状態を更新する。winは未使用。
// 返り値: なし。行が無効、表示幅不足、容量確保失敗なら挿入しない。全角の継続セルは0を格納する。
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
    state->cursor.file_pos.x = editor_cursor_col_boundary(state, line,
        state->cursor.file_pos.x);

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

    //入力候補ウィンドウ処理
    if(state->settings_data->auto_complete_settings_data.auto_complete_enabled){
        if(ch != L' '){
            state->edit_input_complete_data.show = true;
            set_edit_comp_candidacy_part_data(state,ch);
        }
        else{
            init_edit_comt_candidacy_part_data(state);
        }
    }
    state->render_flags |= RENDER_FILE_DATA;
}

// getmouseで取得したイベントをctxの現在画面へ振り分ける。dir_numはブラウザの表示項目数。
// 返り値: なし。イベント取得失敗や未対応画面では何もしない。
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

// 矢印キー入力を処理し、行長を超えない位置へカーソルを移動する。画面端ではスクロールしながら表示内容を補う。
// 引数: ctx=カーソルと表示位置を持つ入力context、ch=KEY_UP/DOWN/LEFT/RIGHT。 返り値: なし。
void handle_input_allow(struct editor_input_context *ctx,wchar_t ch){
    struct editor_state *state = ctx->state;
    int line_limit = get_line_limit();
    if(line_limit <= 0)return;
    

    // 画面内に留まったまま動けるかどうかの判定。画面座標ではなく
    // 「カーソル行が表示範囲のどこにいるか」で決める。
    int line = state->cursor.file_pos.y;
    bool can_move_up_in_view   = (line > state->scr.scr_start_num);
    bool can_move_down_in_view = (line - state->scr.scr_start_num + 1 < state->write_area.h);
    state->cursor.file_pos.x = editor_cursor_col_boundary(state, line,
        state->cursor.file_pos.x);

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
            if (state->cursor.file_pos.x > 0){
                state->cursor.file_pos.x = editor_previous_char_col(state, line,
                    state->cursor.file_pos.x);
            }
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
                state->cursor.file_pos.x = editor_next_char_col(state, line,
                    state->cursor.file_pos.x);
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

// カーソル移動などが参照する共有行数上限を更新する。
// 引数: line_limit=新しい行数上限。 返り値: なし。
void set_line_limit(int line_limit){
    limit = line_limit;
}

// 共有されている行数上限を返す。
// 引数: なし。 返り値: set_line_limit()で最後に設定した値。
int get_line_limit(){
    return limit;
}


// stateのremove_line_num行（1以上）を直前の行へ結合し、後続の行情報を前へ詰める。
// 返り値: 結合後のセル数、状態・行・セルが不正なら-1。結合した行が削除行の容量も引き継ぐ。
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


// stateのmake_space_line_num行を現在のカーソル列で分割し、後半を直後の新しい行とする。
// 返り値: 新しい行のセル数、行・状態不正や行情報確保失敗は-1。本文の所有権はstateに残る。
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











// ctxの取得済みマウスイベントでスクロール・本文クリック・ツリーへの移行を処理する。
// 返り値: なし。ホイールは表示開始行だけ、本文クリックは論理カーソルを動かし、最後にカーソル表示可否を更新する。
void editor_screen_mouse_event(struct editor_input_context *ctx){
    MEVENT *event = ctx->mouse_event;
    struct editor_state *state = ctx->state;

    int line_limit = editor_line_limit(state);
    bool can_scroll_down = state->scr.scr_start_num + state->write_area.h < line_limit;

    //エディター画面下スクロール処理
    if (event->bstate & BUTTON5_PRESSED && can_scroll_down){
        
        bool is_cur_on_edit_screen = true;
        //ファイルツリーが表示されている場合
        // スクロール時のマウスポインターがファイルツリー上にあるか判断する
        if(state->file_tree_data.is_show){
            struct box tmp_write_area_box = 
                {(struct pos){state->write_area.x_start,state->write_area.y_start},
                    state->write_area.w,state->write_area.h};    
            if(!box_contains_point(tmp_write_area_box,(struct pos){event->x,event->y})){
                editor_set_screen_state(state,filetree_screen);
                is_cur_on_edit_screen = false;
            }
        }
        if(is_cur_on_edit_screen){
            state->scr.scr_start_num++;
            // 構文情報は画面相対行で持つため、既存分を1行ずらし、新しく見える最下行だけ解析する。
            if(state->settings_data->built_in_syntax){
                scroll_syntax_pos_data(&ctx->syntax_data,-1,state->write_area.h);
                update_line_syntax_data(ctx,state->write_area.h - 1);
            }
        }
        
    //エディター画面上スクロール処理
    }else if ((event->bstate & BUTTON4_PRESSED) && state->scr.scr_start_num > 0) {
        bool is_cur_on_edit_screen = true;
         if(state->file_tree_data.is_show){
            struct box tmp_write_area_box = 
                {(struct pos){state->write_area.x_start,state->write_area.y_start},
                    state->write_area.w,state->write_area.h};    
            if(!box_contains_point(tmp_write_area_box,(struct pos){event->x,event->y})){
                editor_set_screen_state(state,filetree_screen);
                is_cur_on_edit_screen = false;
            }
        }
        if(is_cur_on_edit_screen){ 
            state->scr.scr_start_num--;
            // 下スクロールの逆。既存分を1行下げ、新しく見える最上行(0)だけ解析する。
            if(state->settings_data->built_in_syntax){
                scroll_syntax_pos_data(&ctx->syntax_data,+1,state->write_area.h);
                update_line_syntax_data(ctx,0);
            }
        }
    }
    else if(event->bstate &
            (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)){
        int x = event->x;
        int y = event->y;

      
        if(box_contains_point(ctx->state->file_tree_data.ft_box,(struct pos){event->x,event->y})){
            editor_set_screen_state(state,filetree_screen);
            filetree_mouse_event(ctx);
            return;
        }
        else{
            struct box tmp_write_area_box = 
                {(struct pos){
                    ctx->state->write_area.x_start,
                    ctx->state->write_area.y_start
                },
                    ctx->state->write_area.w,
                    ctx->state->write_area.h
                };
            if(box_contains_point(tmp_write_area_box,(struct pos){x,y})){
                struct pos write_area_pos;
                write_area_pos.y = y - state->write_area.y_start;
                write_area_pos.x = x - state->write_area.x_start;
                int line_num = state->scr.scr_start_num + write_area_pos.y;
                if(state->file_data.description_line_end < line_num){
                    line_num = *state->str.line;
                }
                editor_set_cursor(state,line_num,write_area_pos.x);
            }
        }
    }
    else{
        return;
    }

    // カーソル行が編集領域から出たら隠す。戻ってきたらまた出す。
    my_cur_set(state,editor_cursor_is_visible(state));
}

// ホイール入力でファイルブラウザの選択行を循環移動する。
// 引数: win=描画先。現在は未使用、event=マウスイベント、state=選択状態、dir_num=表示項目数。 返り値: なし。選択行の強調表示が無効なら何もしない。
void file_browse_screen_mouse_event(WINDOW *win, MEVENT *event, struct editor_state *state,int dir_num){
    //ホイールで選択行を動かすだけなので描画先ウィンドウは使わない
    (void)win;

    if(event->bstate & BUTTON1_DOUBLE_CLICKED){
        if(box_contains_point(state->file_browse.box,(struct pos){event->x,event->y})){

            


        }
    }

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


// ファイルブラウザのパス入力モードを設定する。
// 引数: file_browse=更新対象、flag=設定する有効状態。 返り値: なし。file_browseがNULLなら何もしない。
void set_file_browse_path_input_mode(struct file_browse_state *file_browse,bool flag){
    if(file_browse == NULL)return;
    file_browse->path_input_mode = flag;
}

// ファイルブラウザのパス入力モードを取得する。
// 引数: file_browse=取得元。 返り値: 現在の有効状態。file_browseがNULLならfalse。
bool get_file_browse_path_input_mode(struct file_browse_state *file_browse){
    if(file_browse == NULL)return 0;
    return file_browse->path_input_mode;
}

// エディタ状態とncursesのカーソル表示状態を同時に更新する。
// 引数: state=更新対象、set=trueで表示、falseで非表示。 返り値: なし。stateがNULLなら何もしない。curs_set()の失敗は通知しない。
void my_cur_set(struct editor_state *state,bool set){
    if(state == NULL)return;
    state->is_cur_show = set;
    curs_set(set);
}

// 次回ncursesへ反映する画面カーソル座標をstateへ保存する。
// 引数: pos=保存する画面座標、state=保存先のエディタ状態。posは値コピーされる。 返り値: 常に0。stateがNULLの場合の動作は未定義。
int cur_pos_push(struct pos pos, struct editor_state *state){
    // 描画中にカーソルを動かすと表示がちらつくため、座標は貯めておき最後にset_cur_posで反映する。
    state->cursor.show_cur_pos_queue = pos;
    return 0;
}

// stateに保存された画面カーソル座標をncursesへ反映する。
// 引数: state=反映する座標を持つエディタ状態。 返り値: 常に0。move()の失敗は呼び出し元へ通知しない。
int set_cur_pos(struct editor_state *state){
    move(state->cursor.show_cur_pos_queue.y,state->cursor.show_cur_pos_queue.x);
    return 0;
}

// ctxのマウスイベントでツリーの開閉・ファイル読込・境界のドラッグ開始・編集画面への移行を処理する。
// 返り値: 常に0。イベントはgetmouseで取得済みとし、必要な再描画と構文情報更新を要求する。
int filetree_mouse_event(struct editor_input_context *ctx){
    MEVENT *ev = ctx->mouse_event;
    struct editor_state *state = ctx->state;
    //左クリック
    if(ev->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)){
        struct box ft_box = ctx->state->file_tree_data.ft_box;
        //ファイルツリーボックスは枠線も含むサイズのため、枠線のクリックで
        //ファイルなどが開かないよう横幅を−1する
        ft_box.w--;
        // ファイルツリー内クリック時の処理
        if(box_contains_point(ft_box,(struct pos){ev->x,ev->y})){
            for(int i = 0;i < state->file_tree_data.open_count_num;i++){
                ft_path_open_check_data *item =
                    &state->file_tree_data.open_check_data[i];
                if(item->screen_y != ev->y)continue;

                enum select_state path_state =
                    get_path_state(item->table_ptr->absolute_path);
                if(path_state == folder){
                    item->is_open = !item->is_open;
                    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
                }
                else if(path_state == file){
                    struct file_browse_select_state select_state;
                    load_file(state,NULL,0
                        ,item->table_ptr->absolute_path,
                        &select_state);
                    
                    if(select_state.select_state == file){
                        load_screen_size(state);
                        editor_set_cursor(state,0,0);
                        state->render_flags |= RENDER_FILE_DATA;
                        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
                        if(state->settings_data->built_in_syntax){
                            set_syntax_data(&ctx->syntax_data,ctx);
                        }
                    }
                }
                break;
            }
        }
        else{
            struct box tmp_write_area_box = 
                {(struct pos){state->write_area.x_start,state->write_area.y_start},
                    state->write_area.w,state->write_area.h};

            if(box_contains_point(tmp_write_area_box,(struct pos){ev->x,ev->y})){
                struct pos write_area_cur_pos = 
                    editor_mouse_to_buffer_pos(state,(struct pos){ev->x,ev->y});
                //カーソル論理座標
                struct pos cur_logical_pos = 
                    editor_pos_to_buffer_pos(state,write_area_cur_pos);
                //表示されている行数内に収める
                if(*state->str.line <= cur_logical_pos.y){
                    cur_logical_pos.y = *state->str.line;
                }
                editor_set_cursor(state,cur_logical_pos.x,cur_logical_pos.y);
                editor_set_screen_state(state,edit_screen);
                my_cur_set(state,true);
            }
        }
    }
    //ファイルツリーの枠線を右クリックするとファイルツリーサイズを変更する
    if(ev->bstate & BUTTON1_DOUBLE_CLICKED ){
        //枠線クリック判定
        if(state->file_tree_data.ft_box.pos.x + state->file_tree_data.ft_box.w == ev->x){
            state->file_tree_data.is_grabed = true;
        }
    }
    //スクロール判定
    else if(ev->bstate == BUTTON5_PRESSED || ev->bstate == BUTTON4_PRESSED){
        struct box tmp_write_area_box = 
            {(struct pos){state->write_area.x_start,state->write_area.y_start},
                state->write_area.w,state->write_area.h};
        if(box_contains_point(tmp_write_area_box,(struct pos){ev->x,ev->y})){
            editor_set_screen_state(state,edit_screen);
            my_cur_set(state,true);
        }
    }
    return 0;
}


// 指定座標がボックスの範囲内か判定する。
// 引数: b=画面座標と幅・高さを持つボックス、p=判定する画面座標。 返り値: pが左上から右下の境界を含む範囲内ならtrue、それ以外はfalse。
bool box_contains_point(struct box b,struct pos p){
    // 右端・下端も含める(<=)。枠線を含む箱でクリック判定をするための仕様。
    if(b.pos.x <= p.x && b.pos.x + b.w >= p.x && 
        b.pos.y <= p.y && b.pos.y + b.h >= p.y)return true;
    return false;
}

// マウスの画面座標を編集領域の左上基準の座標へ変換する。
// 引数: state=編集領域の画面上の位置と大きさ、mouse_pos=変換する画面座標。 返り値: 編集領域内なら領域左上を(0,0)とする座標、範囲外なら(-1,-1)。 スクロール開始行は加算しない。
struct pos editor_mouse_to_buffer_pos(struct editor_state *state,
                                    struct pos mouse_pos){
    struct box write_area = 
        (struct box){(struct pos){state->write_area.x_start,
            state->write_area.y_start},
            state->write_area.w,
            state->write_area.h};
    struct pos tmp_pos = (struct pos){-1,-1};  
    if(screen_pos_to_box_pos(write_area,mouse_pos,&tmp_pos)){
        error_log("error");
        return tmp_pos;
    }
    return tmp_pos;  
}

// 画面座標p1をb1左上からの相対座標へ変換して非NULLのrp1へ格納する。右下境界も範囲に含む。
// 返り値: 成功false、範囲外true。失敗時は*rp1を変更しない。
bool screen_pos_to_box_pos(struct box b1,struct pos p1,struct pos *rp1){
    if(!box_contains_point(b1,p1))return 1;
    *rp1 = (struct pos){p1.x - b1.pos.x,p1.y - b1.pos.y};
    return 0;
}

// stateの表示開始行をeditor_pos.yへ加え、編集領域の相対位置から論理位置を返す。
// 返り値: xが論理行、yが列のpos。通常の座標と順序が逆で、範囲検査は行わない。
struct pos editor_pos_to_buffer_pos(
                struct editor_state *state,
                struct pos editor_pos){
    return(struct pos){state->scr.scr_start_num + editor_pos.y,editor_pos.x};
}




// 言語を保存し、有効な構文着色と補完データを初期化する。
// 引数: ctx=stateとsettings_dataが設定済みの入力context、lang=新しい言語。 返り値: 常に0。現実装では補完データの初期化失敗を返さない。
int editor_set_env_lang(struct editor_input_context *ctx,language lang){
    if(ctx->state->settings_data->built_in_syntax){
        init_syntax(&ctx->syntax_data);
        set_syntax_language(lang,&ctx->syntax_data);
    }
    env_language_ctl(&lang,set);
    if(ctx->state->settings_data->auto_complete_settings_data.auto_complete_enabled){
        init_edit_complete_data(ctx);
    }

    return 0;
}



// ctx->state->settings_dataへ既定値を設定してから、設定JSONの有効な項目で上書きする。
// 返り値: 常に0。ctx・state・settings_dataは呼び出し前に接続しておく。
int init_settings_data(struct editor_input_context *ctx){
    //最初にデフォルト設定を読み込みユーザーが設定している項目だけ更新する
    load_default_editor_settings(ctx->state->settings_data);
    load_custom_editor_settings(ctx->state->settings_data);
    return 0;
}


// env_language_ctl()に保存された現在の言語を取得する。
// 返り値: 未設定ならUNKNOWN、設定済みなら最後に保存した言語。
language get_env_language(){
    language tmp_lang;
    env_language_ctl(&tmp_lang,get);
    return tmp_lang;
}


// getなら保存済み言語をlangへ書き、setならlangの値を保存する。
// 引数: lang=読み書き先の有効なポインタ、flags=getまたはset。 返り値: 現実装ではflagsにかかわらず0。
int env_language_ctl(language *lang,enum flags flags){
    static language static_env_lang = UNKNOWN;
    if(flags == get){
        *lang = static_env_lang;
        return 0;
    }
    else if(flags == set){
        static_env_lang = *lang;
        return 0;
    }
    return 0;
}



// stateのキー履歴へ文字ch・get_wch結果result・入力時のscreen_stateを追加する。
// 履歴は設定上限までで打ち止めにし、長時間使ってもメモリが増え続けないようにしている。
// 返り値: 通常0、再確保失敗-1。確保容量が設定上限以上なら、空きがあっても追加せず0を返す。
int key_log_add(struct editor_state *state,wchar_t ch,int result,screen_state screen_state){
    if(state->key_bord_data.Key_log_data.key_allocate_num >= 
            state->settings_data->key_log_settings.key_log_buffer_size){
        return 0; 
    }

    if(state->key_bord_data.Key_log_data.key_log == NULL){
        state->key_bord_data.Key_log_data.key_allocate_num = 32;
        state->key_bord_data.Key_log_data.key_count = 0;
        state->key_bord_data.Key_log_data.key_log = 
            malloc(sizeof(key_data) * 
                state->key_bord_data.Key_log_data.key_allocate_num);

        if(state->key_bord_data.Key_log_data.key_log == NULL){
            error_log("malloc");
            return -1;
        }
    }
    else if(state->key_bord_data.Key_log_data.key_allocate_num <= 
        state->key_bord_data.Key_log_data.key_count){
        uint16_t tmp_realloc_num = 
            (state->key_bord_data.Key_log_data.key_count * 2 >= 
                state->settings_data->key_log_settings.key_log_buffer_size)?
                state->settings_data->key_log_settings.key_log_buffer_size:
                state->key_bord_data.Key_log_data.key_count * 2;
        

        key_data *tmp_key_data = 
            realloc(
                state->key_bord_data.Key_log_data.key_log,
                sizeof(key_data) * tmp_realloc_num
            );
        if(tmp_key_data == NULL){
            error_log("malloc");
            return -1;
        }
        state->key_bord_data.Key_log_data.key_log = tmp_key_data;
        state->key_bord_data.Key_log_data.key_allocate_num = tmp_realloc_num;
    }

    key_data *tmp_key_log = 
        &state->key_bord_data.Key_log_data.key_log[state->key_bord_data.Key_log_data.key_count];
    tmp_key_log->screen_state = screen_state;
    tmp_key_log->input_result = result;
    tmp_key_log->key = ch;
    state->key_bord_data.Key_log_data.key_count++;
    return 0;
}

int get_write_screen_pos_chr(struct editor_state *state,struct pos pos,char *chr){
    if(state->file_data.is_open_file == false){
        chr = NULL;
        return -1;
    }

    //write_area.wは0からカウントしないため-1して判定
    if(pos.x < 0 || pos.y < 0 ||
        state->write_area.w-1 <= pos.x ||
        state->write_area.h-1 <= pos.y){
            chr = NULL;
            return -1;
    }

    int logical_line_pos = state->scr.scr_start_num + pos.y;
    int line_start_num = state->file_data.file_line_start_num[logical_line_pos];
    char *str_line_start_ptr = state->file_data.file_str_data[line_start_num];
    if((int)strlen(str_line_start_ptr) < pos.x){
        chr = NULL;
        return -1;
    }
    
    *chr = str_line_start_ptr[pos.x];
    return pos.x;
}

