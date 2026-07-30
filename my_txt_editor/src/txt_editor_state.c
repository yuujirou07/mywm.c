#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "lsp_src/language_server_communication.h"
#include "txt_editor.h"
#include"language_server_communication.h"

static void reset_jump_mode(struct editor_state *state);
static long clamp_editor_target_line(struct editor_state *state, long target_line);
static int draw_start_line_for_target(struct editor_state *state, long target_line);
static void redraw_edit_screen(struct editor_state *state);
static void restore_edit_screen(struct editor_state *state);
static void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                                  int screen_center_y, struct pos screen_center_pos);
static bool handle_edit_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);
static bool handle_file_browse_screen_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_line_jump_mode_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch);
static bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch);
static void send_lsp_did_change(struct editor_input_context *ctx);


// editor_handle_screen_input(): 現在のscreen_stateに応じて入力処理を各state関数へ振り分ける。
// 引数: ctx=描画対象や共有状態をまとめた入力context、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue、終了要求ならfalse。
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch)
{
    switch (editor_get_screen_state(ctx->state)) {
        case start_menu_screen:
            return handle_start_menu_input(ctx, ch);
        case edit_screen:
            return handle_edit_screen_input(ctx, input_result, ch);
        case file_browse_screen:
            return handle_file_browse_screen_input(ctx, ch);
        case start_menu_file_browse_screen://関数内にstart_menu_file_browse_screenの場合の分岐を記述している
            return handle_file_browse_screen_input(ctx, ch);
        case line_jump_mode:
            return handle_line_jump_mode_input(ctx, ch);
        case error_screen:
            return handle_error_screen_input(ctx, ch);
        case ask_make_file_mode:
            return handle_ask_make_file_mode_input(ctx, input_result, ch);
    }
    return true;
}

// handle_edit_screen_input(): 通常編集画面のキー入力を処理する。
// 引数: ctx=編集画面の描画・状態更新に必要なcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue、qで終了するならfalse。
static bool handle_edit_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch)
{
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(state->file_data.now_open_path_name[0] != '\0' && state->settings_data->show_status_bar){
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    }
    if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state->is_cur_show) {
        handle_backspace(win, state);
        send_lsp_did_change(ctx);
        return true;
    }
    else if(ch == CTRL('h')){
        editor_set_screen_state(state, line_jump_mode);
        reset_jump_mode(state);
        state->render_flags |= RENDER_LINE_JUMP;
        return true;
    }
    if (ch == CTRL('f')) {
        ctx->state->is_cur_show = false;
        curs_set(0);
        editor_set_screen_state(state, file_browse_screen);
        show_file_browse(state, ctx->file_browse_box, ctx->dir_name_table, ctx->path_name, win);
        return true;
    }
    else if(ch == CTRL('s')){
        // 編集位置はstate->cursorが持ち続けるため、画面を離れる前の退避は不要。
        save_file(state);
        flushinp();
        if(editor_get_screen_state(state) == ask_make_file_mode){
            show_make_file_prompt(win, state, &state->ask_make_file_box,
                                  ctx->screen_center_y, ctx->screen_center_pos);
        }
        return true;
    }
    if (ch == KEY_MOUSE) {
        handle_mouse(win, ctx->mouse_event, state);
        state->render_flags |= RENDER_LINE;

        return true;
    }
    if (state->is_cur_show) {
        if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
            handle_newline(win, state);
        } else if (ch == '\t') {
            handle_tab(win, state);
        } else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            handle_input_allow(win, ch, state);
        } else if (input_result == OK && iswprint((wint_t)ch)) {
            if (ch == 'q') {
                return false;
                
            }
            handle_char_input(win, (wchar_t)ch, state);
            send_lsp_did_change(ctx);
        }
        state->render_flags |= RENDER_LINE_STATUS;
    } else {
        if (input_result == OK && iswprint((wint_t)ch)) {
            // カーソル非表示中は、論理行を見える位置へ戻してから入力する。
            if(editor_line_limit(state) == 0){
                return true;
            }
            move_view_to_line(state, state->cursor.line, state->cursor.col);
            state->is_cur_show = true;
            curs_set(1);
            handle_char_input(win, (wchar_t)ch, state);
            send_lsp_did_change(ctx);
            state->render_flags |= RENDER_LINE_STATUS;
        }
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            // nは現在の論理行。move_view_to_line()がstateを書き換える前に保持する。
            int n = state->cursor.line;
            state->is_cur_show = true;
            curs_set(1);
            clear();
            move_view_to_line(state, n - 1, 0);
        }
    }
    if(ch == 'q') {
        return false;
    }
    return true;
}

// handle_file_browse_screen_input(): ファイルブラウザ画面の移動・選択・復帰を処理する。
// 引数: ctx=ファイル一覧・現在パス・描画先を持つcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_file_browse_screen_input(struct editor_input_context *ctx, wint_t ch)
{
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;



    if (ch == 'q') {
        if(editor_get_screen_state(state) == start_menu_file_browse_screen){
            editor_set_screen_state(state, start_menu_screen);
            *ctx->open_start_menu = true;
        }
        else{
            editor_set_screen_state(state, edit_screen);
            restore_edit_screen(state);
        }
        return true;
    }
    if (ch == CTRL('f')) {
        restore_edit_screen(state);
        return true;
    }

    if(state->settings_data->file_select_scene_lighting){
        if(ch == KEY_MOUSE){
            handle_mouse(win,ctx->mouse_event,ctx->state);
        }
        if (ch == KEY_UP || ch == 'k') {
            // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line = (state->file_select_line_data.now_line <= 0) 
                ? state->dir_num - 1:state->file_select_line_data.now_line  - 1;
            set_file_select_line(state, next_line);
        } else if (ch == KEY_DOWN  || ch == 'j') {
            // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line = (state->file_select_line_data.now_line  >= state->dir_num - 1)
                ?0:state->file_select_line_data.now_line  + 1;
            set_file_select_line(state, next_line);
        }
        
    } 
    if (ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' ') {
        // select_state.select_nameが空でなければディレクトリ選択、空ならファイル読み込み完了側を見る。
        struct file_browse_select_state select_state;
        load_file(state, ctx->dir_name_table, ctx->path_name, &select_state);

        if(select_state.select_name[0] != '\0'){
            
            char now_path_name[PATH_MAX] = {0};
            size_t path_len = strlen(ctx->path_name);
            size_t select_name_len = strlen(select_state.select_name);

            if (path_len + 1 + select_name_len + 1 > sizeof(now_path_name)) {
                editor_error_screen(state, "path too long");
                return true;
            }

            memcpy(now_path_name, ctx->path_name, path_len);
            now_path_name[path_len] = '/';
            memcpy(now_path_name + path_len + 1, select_state.select_name, select_name_len + 1);
            memcpy(ctx->path_name, now_path_name, path_len + 1 + select_name_len + 1);
            
            load_dir_table(state, ctx->dir_name_table, ctx->dir_name_table_rows, ctx->path_name);
            show_file_browse(state, ctx->file_browse_box, ctx->dir_name_table, ctx->path_name, win);
        }
        if(state->file_data.now_open_file != NULL && select_state.select_state == file){
            load_screen_size(state);
            char uri[(PATH_MAX * 3) + sizeof("file://")];

            if(state->settings_data->lsp.lsp_use && ctx->lsp_data != NULL &&
               ctx->lsp_data->initialized && ctx->lsp_data->to_server_fd >= 0 &&
               lsp_path_to_file_uri(uri, sizeof(uri), state->file_data.now_open_path_name) == 0 &&
               lsp_send_did_open(ctx->lsp_data->to_server_fd, uri,
                                 ctx->lsp_data->lsp_language_id,
                                 state->str.chr_file_all_str_data) == 0){
                snprintf(ctx->lsp_data->update_data.path_name,
                         sizeof(ctx->lsp_data->update_data.path_name), "%s",
                         state->file_data.now_open_path_name);
                ctx->lsp_data->update_data.file_update_counter = 1;
            }

            // 読み込み直後は先頭行の行頭から編集を始める。
            editor_set_cursor(state, 0, 0);
            restore_edit_screen(state);
    
        }
    }
    return true;
}

// handle_line_jump_mode_input(): 行ジャンプ入力中の数字入力・確定・キャンセルを処理する。
// 引数: ctx=ジャンプ後の再描画に必要なcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_line_jump_mode_input(struct editor_input_context *ctx, wint_t ch)
{
    struct editor_state *state = ctx->state;

    if(ch >= '0' && ch <= '9' &&
       state->jump_mode_data.jump_line_num_counter < (int)sizeof(state->jump_mode_data.jump_line_num) - 1){
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter++] = (char)ch;
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter] = '\0';
        state->render_flags |= RENDER_LINE_JUMP;
    }
    else if(ch == CTRL('h')){
        reset_jump_mode(state);
        restore_edit_screen(state);
    }
    else if(ch == KEY_BACKSPACE && state->jump_mode_data.jump_line_num_counter > 0){
        state->jump_mode_data.jump_line_num_counter--;
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter] = '\0';
        state->render_flags |= RENDER_LINE_JUMP;
    }
    else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){

        char *end;
        // nはユーザー入力の1始まり行番号。内部の論理行は0始まりなので確定時に1引く。
        long n = strtol(state->jump_mode_data.jump_line_num, &end, 10);

        if(n < 1){
            n = 1;
        }
        else if( n > DEFAULT_LOAD_LINE_SIZE ){
            n = DEFAULT_LOAD_LINE_SIZE;
        }

        move_view_to_line(state, n - 1, 0);
        curs_set(1);
        reset_jump_mode(state);
        editor_set_screen_state(state, edit_screen);
    }
    return true;
}

// handle_error_screen_input(): エラー画面でEnterが押されたら遷移元に応じて画面へ戻す。
// 引数: ctx=編集画面復帰に必要なcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch)
{
    struct editor_state *state = ctx->state;

    if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
        clear();
        enum now_screen_state previous_state = editor_get_previous_screen_state(state);

        if(previous_state == start_menu_file_browse_screen ||
           previous_state == start_menu_screen){
            state->is_cur_show = false;
            curs_set(false);
            editor_set_screen_state(state, start_menu_screen);
        }
        else{
            curs_set(true);
            state->is_cur_show = true;
            editor_set_screen_state(state, edit_screen);
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
        }
    }
    return true;
}

// handle_ask_make_file_mode_input(): 未保存ファイル作成確認と新規ファイル名入力を処理する。
// 引数: ctx=確認ダイアログと編集画面復帰に必要なcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch)
{
    struct editor_state *state = ctx->state;

    if(state->make_file_mode_status.is_input_scene){
        int input_limit = state->write_file_name_area.w - 2;
        if(input_result == OK && iswprint((wint_t)ch) &&
           state->make_file_mode_status.new_file_name_counter < input_limit &&
           state->make_file_mode_status.new_file_name_counter <
               (int)sizeof(state->make_file_mode_status.new_file_name) - 1){
            int index = state->make_file_mode_status.new_file_name_counter++;
            state->make_file_mode_status.new_file_name[index] = (char)ch;
            state->make_file_mode_status.new_file_name[index + 1] = '\0';
            state->render_flags |= RENDER_MAKE_FILE;
        }
        else if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state->is_cur_show
            && state->make_file_mode_status.new_file_name_counter > 0){
            state->make_file_mode_status.new_file_name_counter--;
            state->make_file_mode_status.new_file_name[
                state->make_file_mode_status.new_file_name_counter] = '\0';
            state->render_flags |= RENDER_MAKE_FILE;
        }
        else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){
            //もし何も入力されていなかったら
            if(state->make_file_mode_status.new_file_name_counter <= 0){
                return true;
            }
            state->make_file_mode_status.
                new_file_name[state->make_file_mode_status.new_file_name_counter] = '\0';

            editor_set_screen_state(state, edit_screen);
            memcpy(state->file_data.now_open_path_name,
                state->make_file_mode_status.new_file_name,
                sizeof(state->file_data.now_open_path_name));

            save_file(state);

            state->make_file_mode_status.new_file_name_counter = 0;
            state->make_file_mode_status.is_input_scene = false;
            memset(state->make_file_mode_status.new_file_name,
                0,
                sizeof(state->make_file_mode_status.new_file_name));

            clear();
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            editor_sync_cursor(state);
        }
    }
    else{
        if(ch == 'y'){
            state->make_file_mode_status.is_input_scene = true;
            state->is_cur_show = true;
            state->render_flags |= RENDER_MAKE_FILE;
            return true;

        }
        else if(ch == 'n'){
            clear();
            state->make_file_mode_status.is_input_scene = false;
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            editor_set_screen_state(state, edit_screen);
            editor_sync_cursor(state);
            state->is_cur_show = true;
            curs_set(true);
        }

    }
    return true;
}

// show_make_file_prompt(): 保存先が無いときにファイル作成確認の小画面を描く。
// 引数: win=描画先、state=画面状態、file_box=作成した確認枠の保存先、screen_center_y/screen_center_pos=配置基準。
// 返り値: なし。
static void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                                  int screen_center_y, struct pos screen_center_pos){
    (void)win;
    char comment_str[] = "The file cannot be found; would you like to create it?";
    int comment_str_len = strlen(comment_str);
    int box_h = (comment_str_len / state->scr.scr_size.x + 1) + 2;
    int box_w = (state->scr.scr_size.x > comment_str_len + 2)
        ? comment_str_len + 2 : state->scr.scr_size.x;
    struct box make_file_box;
    make_file_box.pos.y = screen_center_y - (screen_center_y / 2);
    make_file_box.pos.x = screen_center_pos.x - (comment_str_len / 2);
    make_file_box.h = box_h;
    make_file_box.w = box_w;

    state->render_flags |= RENDER_MAKE_FILE;
    state->is_cur_show = false;
    *file_box = make_file_box;
}

// reset_jump_mode(): 行ジャンプ入力中の数字バッファを空に戻す。
// 引数: state=ジャンプ入力状態を持つエディタ状態。
// 返り値: なし。
static void send_lsp_did_change(struct editor_input_context *ctx)
{
  

    struct editor_state *state = ctx->state;
    struct lsp_process *lsp = ctx->lsp_data;
    char uri[(PATH_MAX * 3) + sizeof("file://")];
    char *text;
    int next_version;

    if(!state->settings_data->lsp.lsp_use || lsp == NULL ||
       !lsp->initialized || lsp->to_server_fd < 0 ||
       lsp->update_data.file_update_counter < 1 ||
       strcmp(lsp->update_data.path_name, state->file_data.now_open_path_name) != 0 ||
       lsp_path_to_file_uri(uri, sizeof(uri), state->file_data.now_open_path_name) == -1){
        return;
    }

    // 危険: 文字入力ごとに全文を確保・UTF-8化し、blockingなpipeへ同期送信する。
    // ファイルやLSP応答が重いと入力処理そのものが長時間停止する。
    text = editor_buffer_to_utf8(state);
    if(text == NULL){
        return;
    }

    next_version = lsp->update_data.file_update_counter + 1;
    if(lsp_send_did_change(lsp->to_server_fd, uri, next_version, text) == 0){
        lsp->update_data.file_update_counter = next_version;
        free(state->str.chr_file_all_str_data);
        state->str.chr_file_all_str_data = text;
        return;
    }

    free(text);
}

static void reset_jump_mode(struct editor_state *state){
    memset(state->jump_mode_data.jump_line_num, 0, sizeof(state->jump_mode_data.jump_line_num));
    state->jump_mode_data.jump_line_num_counter = 0;
}

// jump_prompt_pos(): 行ジャンププロンプトを描画する座標を返す。
// 引数: state=ステータスバー設定と書き込み領域を持つエディタ状態。
// 返り値: プロンプトの左端座標。
// clamp_editor_target_line(): 移動先行番号を編集可能な範囲へ丸める。
// 引数: state=有効行数を持つエディタ状態、target_line=移動したい論理行番号。
// 返り値: 有効範囲内の論理行番号。
static long clamp_editor_target_line(struct editor_state *state, long target_line){
    int line_limit = editor_line_limit(state);

    if(line_limit <= 0){
        return 0;
    }
    if(target_line >= line_limit){
        return line_limit - 1;
    }
    if(target_line < 0){
        return 0;
    }
    return target_line;
}

// draw_start_line_for_target(): 指定行を少し下に表示するための表示開始行を返す。
// 引数: state=表示領域の高さを持つエディタ状態、target_line=画面内に表示したい論理行番号。
// 返り値: scr_start_numへ設定する表示開始行。
static int draw_start_line_for_target(struct editor_state *state, long target_line){
    int line_limit = get_line_limit();
    target_line = ( target_line + state->write_area.h > line_limit ) ? line_limit -  state->write_area.h + 15 : target_line;
    return (target_line > 15) ? (target_line - 15) : 0;
}

// redraw_edit_screen(): 編集画面の再描画を要求する。
// 実際に描くのはupdate_screen()で、区切り線の座標もそちらがctxから読む。
// 引数: state=描画要求を積むエディタ状態。
// 返り値: なし。
static void redraw_edit_screen(struct editor_state *state){
    clear();
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// restore_edit_screen(): ファイルブラウザやジャンプ入力から編集画面へ戻す。
// 引数: state=復帰させる状態。
// 返り値: なし。
static void restore_edit_screen(struct editor_state *state){
    editor_set_screen_state(state, edit_screen);
    state->is_cur_show = true;
    curs_set(true);
    redraw_edit_screen(state);
    // 編集位置はstate->cursorに残っているため、退避しておいた画面座標は要らない。
    editor_sync_cursor(state);
}

// move_view_to_line(): 指定行が見える位置へ表示開始行とカーソルを移動する。
// 引数: state=表示位置とカーソル行、target_line=移動先論理行、col=移動後の桁数。
// 返り値: なし。
void move_view_to_line(struct editor_state *state, long target_line, int col){
    target_line = clamp_editor_target_line(state, target_line);
    int draw_start_line = draw_start_line_for_target(state,target_line);
    state->scr.scr_start_num = draw_start_line;
    editor_set_cursor(state, (int)target_line, col);
    redraw_edit_screen(state);

    editor_sync_cursor(state);
}



static bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch){
    (void)ch;
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(ctx->start_menu == NULL){
        restore_edit_screen(state);
        return true;
    }

    state->is_cur_show = false;
    curs_set(0);
    clear();
    int start_menu_result = ctx->start_menu(state->scr.scr_size.x,state->scr.scr_size.y,
                                            ctx->ascii_data,
                                            ctx->startup_start_time,
                                            ctx->startup_log_path);
    flushinp();
    *ctx->open_start_menu = false;

    if(start_menu_result == resize_request){
        handle_resize(win, ctx);
        return true;
    }
    else if(start_menu_result == quit){
        return false;
    }
    else if(start_menu_result == select_folder){
        editor_set_screen_state(state, start_menu_file_browse_screen);
        state->is_cur_show = false;
        curs_set(0);

        struct box clear_area;
        int logo_h = ctx->ascii_data != NULL ? ctx->ascii_data->h : 0;
        clear_area.pos = (struct pos){0,logo_h};
        clear_area.w = state->scr.scr_size.x - 1;
        clear_area.h = state->scr.scr_size.y - logo_h;

        request_clear_box(state,clear_area);
        show_file_browse(state,ctx->file_browse_box,ctx->dir_name_table,ctx->path_name,win);
        refresh();
        return true;
    }

    else if(start_menu_result == new_file){
        editor_set_screen_state(state, edit_screen);
        state->is_cur_show = true;
        curs_set(1);
        clear();
        state->scr.scr_start_num = 0;
        editor_set_cursor(state, 0, 0);
        editor_sync_cursor(state);
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
        return true;
    }
    
    editor_set_screen_state(state, edit_screen);
    state->is_cur_show = true;
    curs_set(1);
    return true;
}
// browser_clear_area(): ファイルブラウザが実際に塗っていた範囲を返す。
// 枠の右端(w+1桁目)と、枠の上に出るパス表示2行分を含める。
// 引数: browse_box=消去したいファイルブラウザ外枠。
// 返り値: 消去対象の矩形。
static struct box browser_clear_area(struct box browse_box){
    struct box area = browse_box;

    area.pos.y = (browse_box.pos.y >= 2) ? browse_box.pos.y - 2 : 0;
    area.h     = browse_box.h + (browse_box.pos.y - area.pos.y) + 1;
    area.w     = browse_box.w + 1;
    return area;
}

// clamp_box_to_screen(): 矩形を現在の画面内へ収める。
// リサイズ前の矩形は新しい画面からはみ出すことがあり、clear_box()は
// box.w分の一時バッファを取るため、負値や画面外を渡さないようにする。
// 引数: state=現在の画面サイズ、box=丸める矩形。
// 返り値: 画面内に収めた矩形。収まる部分が無ければw/hが0。
static struct box clamp_box_to_screen(struct editor_state *state, struct box box){
    if(box.pos.x < 0){
        box.w += box.pos.x;
        box.pos.x = 0;
    }
    if(box.pos.y < 0){
        box.h += box.pos.y;
        box.pos.y = 0;
    }
    if(box.pos.x + box.w > state->scr.scr_size.x){
        box.w = state->scr.scr_size.x - box.pos.x;
    }
    if(box.pos.y + box.h > state->scr.scr_size.y){
        box.h = state->scr.scr_size.y - box.pos.y;
    }
    if(box.w < 0){
        box.w = 0;
    }
    if(box.h < 0){
        box.h = 0;
    }
    return box;
}

// update_screen_ratio(): 画面サイズが変わったあと、今表示している画面の配置を
// 新しい画面サイズの比率で作り直し、必要な再描画要求をrender_flagsへ積む。
// 実際の描画はupdate_screen()が行うため、ここでは配置更新と要求だけを行う。
// 引数: ctx=画面状態・各領域・中央寄せ基準を持つ入力context。
// 返り値: 配置を更新したら1、ctxが無効で何もしなかったら0。
int update_screen_ratio(struct editor_input_context *ctx){
    if(ctx == NULL || ctx->state == NULL)return 0;

    struct editor_state *state = ctx->state;

    // 中央寄せ基準はどの画面でも使うため先に更新する
    ctx->screen_center_y   = state->scr.scr_size.y / 2;
    ctx->screen_center_pos = (struct pos){state->scr.scr_size.x / 2, ctx->screen_center_y};

    enum now_screen_state now_state = editor_get_screen_state(ctx->state);
    switch(now_state){
        case file_browse_screen:
        case start_menu_file_browse_screen: {
            // 枠を作り直すと縮小時に古い枠が残る。start menu側のロゴを消さないよう、
            // clear()ではなく「前の枠+上のパス表示2行」の範囲だけを消去予約する。
            struct box old_box = ctx->file_browse_box;

            resize_file_browser(ctx);

            struct box clear_area = clamp_box_to_screen(state, browser_clear_area(old_box));
            bool is_box_moved = (old_box.pos.x != ctx->file_browse_box.pos.x ||
                                 old_box.pos.y != ctx->file_browse_box.pos.y ||
                                 old_box.w     != ctx->file_browse_box.w ||
                                 old_box.h     != ctx->file_browse_box.h);

            if(is_box_moved && clear_area.w > 0 && clear_area.h > 0){
                request_clear_box(state, clear_area);
            }
            state->render_flags |= RENDER_FILE_BROWSE;
            break;
        }
        case ask_make_file_mode:
            // 確認枠と入力欄は新しい中央基準で置き直す。下の編集画面ごと描き直す。
            clear();
            show_make_file_prompt(ctx->win, state, &state->ask_make_file_box,
                                  ctx->screen_center_y, ctx->screen_center_pos);
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            state->render_flags |= RENDER_MAKE_FILE;
            break;
        case line_jump_mode:
            // 入力欄の位置はステータスバー基準で毎回計算されるので描き直すだけでよい
            clear();
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            state->render_flags |= RENDER_LINE_JUMP;
            break;
        case error_screen:
            // エラー文言を保持していないため描き直せない。表示位置の基準になる
            // file_browser_areaだけ新サイズへ合わせ、戻ったときにずれないようにする。
            resize_file_browser(ctx);
            break;
        case start_menu_screen:
            // start menu pluginが次のループで新しい画面サイズを見て描き直す
            break;
        case edit_screen:
        default: {
            // 高さが縮むとカーソル行が編集領域の外へ出るため、はみ出したときだけ
            // 表示開始行を取り直す。収まっているならスクロール位置は動かさない。
            if(!editor_cursor_is_visible(state)){
                //内部でclear()と再描画要求、カーソル移動まで行う
                move_view_to_line(state, state->cursor.line, state->cursor.col);
                break;
            }

            clear();
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            editor_sync_cursor(state);
            break;
        }
    }

    return 1;
}
