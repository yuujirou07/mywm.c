#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "txt_editor.h"

static void reset_jump_mode(struct editor_state *state);
static struct pos jump_prompt_pos(struct editor_state *state);
static long clamp_editor_target_line(struct editor_state *state, long target_line);
static int draw_start_line_for_target(struct editor_state *state, long target_line);
static void redraw_edit_screen(WINDOW *win, struct editor_state *state,
                               struct pos line_start_pos, struct pos line_end_pos);
static void restore_edit_screen(WINDOW *win, struct editor_state *state,
                                struct pos line_start_pos, struct pos line_end_pos);
static void move_view_to_line(WINDOW *win, struct editor_state *state, long target_line,
                              int x, struct pos line_start_pos, struct pos line_end_pos);
static void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                                  int screen_center_y, struct pos screen_center_pos);
static bool handle_edit_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);
static bool handle_file_browse_screen_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_line_jump_mode_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch);
static bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch);
static bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch);


// editor_handle_screen_input(): 現在のscreen_stateに応じて入力処理を各state関数へ振り分ける。
// 引数: ctx=描画対象や共有状態をまとめた入力context、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue、終了要求ならfalse。
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch)
{
    switch (ctx->state->screen_state) {
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
        draw_status_bar_path(state, win);
        draw_status_bar_line(state, *state->status_bar, win);
        draw_line_status(state, win);
        refresh();
    }
    if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state->is_cur_show) {
        handle_backspace(win, state);
        return true;
    }
    else if(ch == CTRL('h')){
        state->screen_state = line_jump_mode;
        reset_jump_mode(state);
        getyx(win, state->mouse.scr_abs_now_pos.y, state->mouse.scr_abs_now_pos.x);
        struct pos prompt_pos = jump_prompt_pos(state);
        move(prompt_pos.y, prompt_pos.x + 1);
        clrtoeol();
        addstr("JMP_LINE ");
        draw_line_status(state, win);
        refresh();
        return true;
    }
    if (ch == CTRL('f')) {
        state->screen_state = file_browse_screen;
        getyx(win, state->mouse.scr_abs_now_pos.y, state->mouse.scr_abs_now_pos.x);
        show_file_browse(state, ctx->file_browse_box, ctx->dir_name_table, ctx->path_name, win);
        refresh();
        return true;
    }
    else if(ch == CTRL('s')){
        getyx(win, state->scr.cursor_pos.y, state->scr.cursor_pos.x);
        save_file(state);
        flushinp();
        if(state->screen_state == ask_make_file_mode){
            show_make_file_prompt(win, state, &state->ask_make_file_box,
                                  ctx->screen_center_y, ctx->screen_center_pos);
        }
        return true;
    }
    if (ch == KEY_MOUSE) {
        handle_mouse(win, ctx->mouse_event, state);
        draw_line(ctx->line_start_pos, ctx->line_end_pos, win, all_draw_mode);
        if(state->settings_data->show_status_bar){
            draw_status_bar_line(state, *state->status_bar, win);
            draw_line_status(state, win);
        }
        return true;
    }
    if (state->is_cur_show) {
        if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
            handle_newline(win, state);
        } else if (ch == '\t') {
            handle_tab(state);
        } else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            handle_input_allow(win, ch, state);
        } else if (input_result == OK && iswprint((wint_t)ch)) {
            if (ch == 'q') {
                return false;
            }
            handle_char_input(win, (wchar_t)ch, state);
        }
        draw_line_status(state, win);
        refresh();
    } else {
        if (input_result == OK && iswprint((wint_t)ch)) {
            // カーソル非表示中は、論理行を見える位置へ戻してから入力する。
            int x = getcurx(win);

            if(editor_line_limit(state) == 0){
                return true;
            }
            move_view_to_line(win, state, state->mouse.now_mouce_line, x,
                              ctx->line_start_pos, ctx->line_end_pos);
            state->is_cur_show = true;
            curs_set(1);
            handle_char_input(win, (wchar_t)ch, state);
            draw_line_status(state, win);
            refresh();
        }
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            // nは現在の論理行。move_view_to_line()がstateを書き換える前に保持する。
            int n = state->mouse.now_mouce_line;
            state->is_cur_show = true;
            curs_set(1);
            clear();
            draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
            move_view_to_line(win, state, n - 1, state->write_area.x_start,
                              ctx->line_start_pos, ctx->line_end_pos);
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


    if (ch == 'q' && ctx->has_start_menu) {
        state->screen_state = start_menu_screen;
        *ctx->open_start_menu = true;
        return true;
    }
    if (ch == CTRL('f')) {
        restore_edit_screen(win, state, ctx->line_start_pos, ctx->line_end_pos);
        refresh();
        return true;
    }
    
    if(state->settings_data->file_select_scene_lighting){
        if (ch == KEY_UP || ch == 'k') {
            // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line;
            if (state->file_select_line <= 0){
                next_line = state->dir_num - 1;
            }
            else{
            next_line = state->file_select_line - 1;
            }
        
            file_sellect_line_update(state, next_line);
            
        } else if (ch == KEY_DOWN  || ch == 'j') {
            // next_lineはハイライトを移す先。端では上下に循環させる。
            int next_line;
            if (state->file_select_line >= state->dir_num - 1){
                next_line = 0;
            }
            else{

                next_line = state->file_select_line + 1;
            }
            file_sellect_line_update(state, next_line);
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
      
            draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
            
            load_dir_table(state, ctx->dir_name_table, ctx->dir_name_table_size, ctx->path_name);
            show_file_browse(state, ctx->file_browse_box, ctx->dir_name_table, ctx->path_name, win);
            refresh();
        }
        if(state->file_data.now_open_file != NULL && select_state.select_state == file){
            load_screen_size(state);
            state->mouse.scr_abs_now_pos = (struct pos){state->write_area.x_start, state->write_area.y_start};
            restore_edit_screen(win, state, ctx->line_start_pos, ctx->line_end_pos);
            draw_status_bar_path(state, win);
            draw_line_status(state, win);
            refresh();
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
    WINDOW *win = ctx->win;

    if(ch >= '0' && ch <= '9' &&
       state->jump_mode_data.jump_line_num_counter < (int)sizeof(state->jump_mode_data.jump_line_num) - 1){
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter++] = (char)ch;
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter] = '\0';
        addch(ch);
    }
    else if(ch == CTRL('h')){
        reset_jump_mode(state);
        restore_edit_screen(win, state, ctx->line_start_pos, ctx->line_end_pos);
        refresh();
    }
    else if(ch == KEY_BACKSPACE && state->jump_mode_data.jump_line_num_counter > 0){

        state->jump_mode_data.jump_line_num_counter--;
        state->jump_mode_data.jump_line_num[state->jump_mode_data.jump_line_num_counter] = '\0';

        struct pos prompt_pos = jump_prompt_pos(state);
        // prompt_xは削除後に空白で消す1文字の画面上x座標。
        int prompt_x = prompt_pos.x + (int)strlen("JMP_LINE ") + 1 +
            state->jump_mode_data.jump_line_num_counter;

        mvaddch(prompt_pos.y, prompt_x, ' ');
        move(prompt_pos.y, prompt_x);
        refresh();
    }
    else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){

        struct pos prompt_pos = jump_prompt_pos(state);
        mvhline(prompt_pos.y, prompt_pos.x + 1, ' ',
                strlen("JMP_LINE ") + JUMP_LINE_NUM_DIGITS);
        char *end;
        // nはユーザー入力の1始まり行番号。内部の論理行は0始まりなので確定時に1引く。
        long n = strtol(state->jump_mode_data.jump_line_num, &end, 10);

        if(n < 1){
            n = 1;
        }
        else if( n > DEFAULT_LOAD_LINE_SiZE ){
            n = DEFAULT_LOAD_LINE_SiZE;
        }

        move_view_to_line(win, state, n - 1, state->write_area.x_start,
                          ctx->line_start_pos, ctx->line_end_pos);
        draw_line_status(state, win);
        curs_set(1);
        refresh();
        reset_jump_mode(state);
        draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
        state->screen_state = edit_screen;
    }
    return true;
}

// handle_error_screen_input(): エラー画面でEnterが押されたら編集画面へ戻す。
// 引数: ctx=編集画面復帰に必要なcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch)
{
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
        clear();
        curs_set(true);
        state->is_cur_show = true;
        state->screen_state = edit_screen;
        draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
        refresh();
    }
    return true;
}

// handle_ask_make_file_mode_input(): 未保存ファイル作成確認と新規ファイル名入力を処理する。
// 引数: ctx=確認ダイアログと編集画面復帰に必要なcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
static bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch)
{
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(state->make_file_mode_status.is_input_scene){
        if(input_result == OK && iswprint((wint_t)ch) && state->ask_make_file_box.w >
            state->make_file_mode_status.new_file_name_counter){
            state->make_file_mode_status.new_file_name[state->make_file_mode_status.new_file_name_counter++] = ch;
            addch(ch);
        }
        else if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state->is_cur_show
            && state->make_file_mode_status.new_file_name_counter > 0){
            state->make_file_mode_status.new_file_name_counter--;

            int x;
            int y;
            getyx(win, y, x);
            mvaddch(y, x - 1, ' ');
            move(y, x - 1);
        }
        else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){
            state->make_file_mode_status.new_file_name[state->make_file_mode_status.new_file_name_counter + 1] = '\0';
            state->screen_state = edit_screen;
            memcpy(state->file_data.now_open_path_name,
                state->make_file_mode_status.new_file_name,
                sizeof(state->file_data.now_open_path_name));

            save_file(state);

            state->make_file_mode_status.new_file_name_counter = 0;
            memset(state->make_file_mode_status.new_file_name,
                0,
                sizeof(state->make_file_mode_status.new_file_name));

            clear();
            draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
            draw_file_data(state);
            move(state->scr.cursor_pos.y, state->scr.cursor_pos.x);
            refresh();
        }
    }
    else{
        if(ch == 'y'){
            // write_file_name_scene_boxは確認ダイアログをファイル名入力用に縦へ広げた枠。
            struct pos str_start_pos = (struct pos){state->ask_make_file_box.pos.x,state->ask_make_file_box.pos.y + 1};
            struct box write_file_name_scene_box =
                (struct box){state->ask_make_file_box.pos,state->ask_make_file_box.w,state->ask_make_file_box.h + 2};
            // input_new_file_name_wtrite_areaは実際にファイル名を入力する内側の小枠。
            struct box input_new_file_name_wtrite_area =
                (struct box){(struct pos){write_file_name_scene_box.pos.x + 1,
                write_file_name_scene_box.pos.y  + write_file_name_scene_box.h - 3},
                write_file_name_scene_box.w - 2,2};

            clear_box(write_file_name_scene_box);
            draw_box(write_file_name_scene_box,win);
            draw_box(input_new_file_name_wtrite_area,win);
            ask_new_file_name(str_start_pos,write_file_name_scene_box.w,write_file_name_scene_box.h);
            state->make_file_mode_status.is_input_scene = true;
            state->is_cur_show = true;
            move(input_new_file_name_wtrite_area.pos.y + 1,input_new_file_name_wtrite_area.pos.x + 1);
            curs_set(true);
            state->write_file_name_area = input_new_file_name_wtrite_area;
            return true;

        }
        else if(ch == 'n'){
            clear();
            draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
            redraw_edit_screen(win, state, ctx->line_start_pos, ctx->line_end_pos);
            state->screen_state = edit_screen;

        }

    }
    refresh();
    return true;
}

// show_make_file_prompt(): 保存先が無いときにファイル作成確認の小画面を描く。
// 引数: win=描画先、state=画面状態、file_box=作成した確認枠の保存先、screen_center_y/screen_center_pos=配置基準。
// 返り値: なし。
static void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                                  int screen_center_y, struct pos screen_center_pos){
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

    draw_box(make_file_box, win);
    struct pos in_box_str_pos = {make_file_box.pos.x + 1, make_file_box.pos.y + 1};
    my_mvaddstr(in_box_str_pos, comment_str);

    int yes_str = strlen("YES[y]");
    int yes_str_pos_x = in_box_str_pos.x + (make_file_box.w / 2) - (yes_str + 2);
    int no_str_pos_x  = in_box_str_pos.x + (make_file_box.w / 2) + 2;

    mvaddstr(in_box_str_pos.y + 1, yes_str_pos_x, "YES[y]");
    mvaddstr(in_box_str_pos.y + 1, no_str_pos_x, "NO[n]");

    state->is_cur_show = false;
    curs_set(0);
    move(state->scr.cursor_pos.y, state->scr.cursor_pos.x);
    *file_box = make_file_box;
}

// reset_jump_mode(): 行ジャンプ入力中の数字バッファを空に戻す。
// 引数: state=ジャンプ入力状態を持つエディタ状態。
// 返り値: なし。
static void reset_jump_mode(struct editor_state *state){
    memset(state->jump_mode_data.jump_line_num, 0, sizeof(state->jump_mode_data.jump_line_num));
    state->jump_mode_data.jump_line_num_counter = 0;
}

// jump_prompt_pos(): 行ジャンププロンプトを描画する座標を返す。
// 引数: state=ステータスバー設定と書き込み領域を持つエディタ状態。
// 返り値: プロンプトの左端座標。
static struct pos jump_prompt_pos(struct editor_state *state){
    struct pos prompt_pos = {0, state->write_area.y_start};

    if(state->settings_data->show_status_bar){
        prompt_pos.y = (state->settings_data->bar_side_state == top)
            ? state->status_bar->pos.y - 1 : state->status_bar->pos.y;
        prompt_pos.x = state->status_bar->pos.x;
    }
    return prompt_pos;
}

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

// redraw_edit_screen(): 編集画面の固定要素と表示中のファイル内容を再描画する。
// 引数: win=描画先、state=描画対象の状態、line_start_pos/line_end_pos=区切り線の端点。
// 返り値: なし。
static void redraw_edit_screen(WINDOW *win, struct editor_state *state,
                               struct pos line_start_pos, struct pos line_end_pos){
    clear();
    draw_edit_screen_base(state, win, line_start_pos, line_end_pos);
    draw_file_data(state);
}

// restore_edit_screen(): ファイルブラウザやジャンプ入力から編集画面へ戻す。
// 引数: win=描画先、state=復帰させる状態、line_start_pos/line_end_pos=区切り線の端点。
// 返り値: なし。
static void restore_edit_screen(WINDOW *win, struct editor_state *state,
                                struct pos line_start_pos, struct pos line_end_pos){
    state->screen_state = edit_screen;
    state->is_cur_show = true;
    curs_set(true);
    redraw_edit_screen(win, state, line_start_pos, line_end_pos);
    move(state->mouse.scr_abs_now_pos.y, state->mouse.scr_abs_now_pos.x);
}

// move_view_to_line(): 指定行が見える位置へ表示開始行とカーソルを移動する。
// 引数: win=描画先、state=表示位置とカーソル行、target_line=移動先論理行、x=移動後のx座標、line_start_pos/line_end_pos=区切り線の端点。
// 返り値: なし。
static void move_view_to_line(WINDOW *win, struct editor_state *state, long target_line,
                              int x, struct pos line_start_pos, struct pos line_end_pos){
    target_line = clamp_editor_target_line(state, target_line);
    int draw_start_line = draw_start_line_for_target(state,target_line);
    state->mouse.now_mouce_line = target_line;
    state->scr.scr_start_num = draw_start_line;
    redraw_edit_screen(win, state, line_start_pos, line_end_pos);

    int screen_mouce_pos_y = target_line - draw_start_line;
    x = editor_cursor_x_on_line(state, target_line, x);
    move(state->write_area.y_start + screen_mouce_pos_y, x);
}

static bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch){
    (void)ch;
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(ctx->start_menu == NULL){
        restore_edit_screen(win, state, ctx->line_start_pos, ctx->line_end_pos);
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

    if(start_menu_result == quit){
        return false;
    }
    else if(start_menu_result == select_folder){
        state->screen_state = start_menu_file_browse_screen;
        state->is_cur_show = false;
        curs_set(0);

        struct box clear_area;
        int logo_h = ctx->ascii_data != NULL ? ctx->ascii_data->h : 0;
        clear_area.pos = (struct pos){0,logo_h};
        clear_area.w = state->scr.scr_size.x - 1;
        clear_area.h = state->scr.scr_size.y - logo_h;

        clear_box(clear_area);
        show_file_browse(state,ctx->file_browse_box,ctx->dir_name_table,ctx->path_name,win);
        refresh();
        return true;
    }
    else if(start_menu_result == new_file){
        state->screen_state = edit_screen;
        state->is_cur_show = true;
        curs_set(1);
        clear();
        draw_edit_screen_base(state, win, ctx->line_start_pos, ctx->line_end_pos);
        move(state->write_area.y_start, state->write_area.x_start);
        refresh();
        return true;
    }
    state->screen_state = edit_screen;
    state->is_cur_show = true;
    curs_set(1);
    return true;
}
