#include <stdio.h>
#include <ncurses.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>   // SYS_getdents64
#include <sys/types.h>     // uint64_t など
#include <dirent.h>
#include <wchar.h>
#include <wctype.h>
#include<dirent.h>
#include <libgen.h>
#include <limits.h>
#include<unistd.h>
#include "txt_editor.h"

static void end_process(struct editor_state *state);
static void reset_jump_mode(struct editor_state *state);
static struct pos jump_prompt_pos(struct editor_state *state);
static long clamp_editor_target_line(struct editor_state *state, long target_line);
static int draw_start_line_for_target(struct editor_state *state,long target_line);
static void redraw_edit_screen(WINDOW *win, struct editor_state *state,
                               struct pos line_start_pos, struct pos line_end_pos);
static void restore_edit_screen(WINDOW *win, struct editor_state *state,
                                struct pos line_start_pos, struct pos line_end_pos);
static void move_view_to_line(WINDOW *win, struct editor_state *state, long target_line,
                              int x, struct pos line_start_pos, struct pos line_end_pos);
static void show_make_file_prompt(WINDOW *win, struct editor_state *state,struct box * file_box,
                                  int screen_center_y, struct pos screen_center_pos);


// main(): ncursesを初期化し、エディタ画面・ファイルブラウザ・エラー画面の
// 入力ループを切り替えながら各処理関数へイベントを振り分ける。
// 引数: なし。
// 返り値: 正常終了なら0、ncurses初期化やメモリ確保に失敗したら1。
int main(void)
{
    struct editor_settings settings_data = {0};
    struct editor_state state = {0};
    state.settings_data = &settings_data;
    struct box file_browse_box;
    struct box status_bar;
    MEVENT mouse_event;
    WINDOW *win;

    load_default_editor_settings(state.settings_data);
    load_custom_editor_settings(state.settings_data);

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
    state.mouse.scr_abs_now_pos = (struct pos){0,0};

    getmaxyx(win, state.scr.scr_size.y, state.scr.scr_size.x);

    set_line_limit(state.settings_data->default_load_line_size);
    int line_cap = get_line_limit();
    int total_str_buff_size = state.scr.scr_size.x * line_cap;
    state.str.line_str_data = calloc(total_str_buff_size, sizeof(wint_t));
    if(state.str.line_str_data == NULL){
        printf("state.str.line_str_data calloc error");
        return 1;
    }
    //行に入っている文字数を入れる
    state.str.line = calloc(line_cap, sizeof(int));
    if(state.str.line == NULL){
        free(state.str.line_str_data);
        return 1;
    }
    state.str.line_capacity = line_cap;
    state.str.col_capacity = state.scr.scr_size.x;

    int screen_center_y =  state.scr.scr_size.y / 2;
    file_browse_box.w = state.scr.scr_size.x / 3;
    file_browse_box.h = screen_center_y;
    file_browse_box.pos.x = (state.scr.scr_size.x / 2) - file_browse_box.w / 2;
    file_browse_box.pos.y = state.scr.scr_size.y / 4;

    state.is_cur_show = true;
    state.file_browser_box = &file_browse_box;

    curs_set(1);
    raw();
    start_color();
    scrollok(win, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    keypad(win, TRUE);   

    if (has_colors()) {
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        attrset(COLOR_PAIR(1));
    }
    
    char path_name[PATH_MAX];
    if(getcwd(path_name, sizeof(path_name)) == NULL) {
        perror("getcwd");
        return 1;
    }
    state.file_data.description_line_end = 0;
    state.write_area.x_start = state.settings_data->line_number_space + 1;
    state.write_area.y_start = 0;
    state.write_area.x_end   = state.scr.scr_size.x-1;
    state.write_area.y_end   = state.scr.scr_size.y;
    if(state.settings_data->show_status_bar){
        status_bar.w = state.scr.scr_size.x;
        status_bar.h = 1;
        if(state.settings_data->bar_side_state == top){
            status_bar.pos = (struct pos){0, 1};
            state.write_area.y_start = status_bar.pos.y + status_bar.h;
        }
        else{
            status_bar.pos = (struct pos){0, state.scr.scr_size.y - 1};
            state.write_area.y_end = status_bar.pos.y;
        }
    }
    state.status_bar = &status_bar;
    state.write_area.w = state.write_area.x_end - state.write_area.x_start;
    state.write_area.h = state.write_area.y_end - state.write_area.y_start;

    state.file_browser_area.pos.x = file_browse_box.pos.x + 1;
    state.file_browser_area.pos.y = file_browse_box.pos.y + 1;
    state.file_browser_area.h = file_browse_box.h - 2; //底辺から1引く
    state.file_browser_area.w = file_browse_box.w - 2;//同上 

    state.make_file_mode_status.is_input_scene = false;
    state.make_file_mode_status.new_file_name_counter = 0;
    
    memset(&state.write_file_name_area,0,sizeof(struct box));

    state.file_select_line = 0;
    state.dir_num = 0;
    state.file_data.now_open_file = NULL;
    state.file_data.is_open_file = 0;
    state.file_data.file_line_start_num_counter = 0;
    state.file_data.file_line_start_num = calloc(state.settings_data->default_load_line_size, sizeof(long));
    if(state.file_data.file_line_start_num == NULL){
        free(state.str.line_str_data);
        free(state.str.line);
        return 1;
    }

    state.jump_mode_data.jump_line_num_counter = 0;

    state.screen_state              = edit_screen;
    int dir_name_table_size         = state.file_browser_area.w * state.file_browser_area.h;
    char *dir_name_table            = calloc(dir_name_table_size,sizeof(char));
    int allocate_total_str_size     = state.settings_data->load_buffer_lines;
    state.file_data.file_str_data   = calloc(allocate_total_str_size,sizeof(char*));
    if(dir_name_table == NULL || state.file_data.file_str_data == NULL){
        free(dir_name_table);
        free(state.file_data.file_str_data);
        free(state.file_data.file_line_start_num);
        free(state.str.line_str_data);
        free(state.str.line);
        return 1;
    }
    load_dir_table(&state,dir_name_table,dir_name_table_size,path_name);

    struct pos line_start_pos       = (struct pos){state.write_area.x_start-1,state.write_area.y_start};
    struct pos line_end_pos         = (struct pos){state.write_area.x_start-1,state.write_area.y_end};
    struct pos screen_center_pos    = (struct pos){state.scr.scr_size.x/2,screen_center_y};

    draw_edit_screen_base(&state, win, line_start_pos, line_end_pos);

    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_BLACK, COLOR_RED);

    bkgd(COLOR_PAIR(1));
    move(state.write_area.y_start, state.write_area.x_start);
    refresh();
    int running = true;
    while (running) {
        
        wint_t ch = 0;
        int input_result;
  
        input_result = get_wch(&ch);

        if (input_result == ERR)
            continue;

        if (input_result == KEY_CODE_YES && ch == KEY_RESIZE) {
            handle_resize(win, &state,&line_start_pos,&line_end_pos);
            continue;
        }
        switch (state.screen_state) {
            case edit_screen:
                if(state.file_data.now_open_path_name[0] != '\0' && state.settings_data->show_status_bar){
                    draw_status_bar_path(&state, win);
                    draw_status_bar_line(&state,*state.status_bar,win);
                    draw_line_status(&state,win);
                    refresh();
                }
                if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state.is_cur_show) {
                    handle_backspace(win, &state);
                    break;
                }
                else if(ch == CTRL('h')){
                    state.screen_state = line_jump_mode;
                    reset_jump_mode(&state);
                    getyx(win,state.mouse.scr_abs_now_pos.y,state.mouse.scr_abs_now_pos.x);
                    struct pos prompt_pos = jump_prompt_pos(&state);
                    move(prompt_pos.y,prompt_pos.x+1);
                    clrtoeol();
                    addstr("JMP_LINE ");
                    refresh();
                    break;
                }
                if (ch == CTRL('f')) {
                    state.screen_state = file_browse_screen;
                    getyx(win, state.mouse.scr_abs_now_pos.y, state.mouse.scr_abs_now_pos.x);
                    show_file_browse(&state, file_browse_box, dir_name_table, path_name, win);
                    refresh();
                    break;
                }
                else if(ch == CTRL('s')){
                    getyx(win,state.scr.cursor_pos.y,state.scr.cursor_pos.x);
                    save_file(&state);
                    flushinp();
                    if(state.screen_state == ask_make_file_mode){
                        show_make_file_prompt(win, &state,&state.ask_make_file_box,screen_center_y, screen_center_pos);
                    }
                    
                    break;
                }
                if (ch == KEY_MOUSE) {
                    handle_mouse(win, &mouse_event, &state);
                    draw_line(line_start_pos, line_end_pos, win, all_draw_mode);
                    if(state.settings_data->show_status_bar){
                        draw_status_bar_line(&state,*state.status_bar,win);
                        draw_line_status(&state,win);
                    }
                    break;
                }
                if (state.is_cur_show) {
                    if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
                        handle_newline(win, &state);
                        draw_line_status(&state,win);
                    } else if (ch == '\t') {
                        handle_tab(&state);
                    } else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
                        handle_input_allow(win, ch, &state);
                        draw_line_status(&state,win);
                    } else if (input_result == OK && iswprint((wint_t)ch)) {
                        if (ch == 'q') { running = 0; break; }
                        handle_char_input(win, (wchar_t)ch, &state);
                        draw_line_status(&state,win);
                    }
                } else {
                    if (input_result == OK && iswprint((wint_t)ch)) {
                        int x = getcurx(win);

                        if(editor_line_limit(&state) == 0){
                            break;
                        }
                        move_view_to_line(win, &state, state.mouse.now_mouce_line, x,
                                          line_start_pos, line_end_pos);
                        state.is_cur_show = true;
                        curs_set(1);
                        handle_char_input(win, (wchar_t)ch, &state);
                        draw_line_status(&state,win);
                        refresh();
                    }
                }
                if(ch == 'q') { running = 0; break; }
                break;
            case file_browse_screen:
                if (ch == CTRL('f')) {
                    restore_edit_screen(win, &state, line_start_pos, line_end_pos);
                    break;
                }

                if (ch == KEY_UP || ch == 'k') {
                    int next_line;
                    if (state.file_select_line <= 0){
                        next_line = state.dir_num - 1;
                    }
                    else{
                       next_line = state.file_select_line - 1;
                    }
                    file_sellect_line_update(&state,next_line);
                } else if (ch == KEY_DOWN  || ch == 'j') {
                    int next_line;
                    if (state.file_select_line >= state.dir_num - 1){
                        //一番上に戻す
                        next_line = 0;
                    }
                    else{
                        next_line = state.file_select_line + 1;
                    }
                    file_sellect_line_update(&state,next_line);

                } else if (ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' ') {
                    struct file_browse_select_state select_state;
                    load_file(&state,dir_name_table,path_name,&select_state);
                    if(select_state.select_name[0] != '\0'){
                        // ディレクトリ選択時だけここに来る。
                        char now_path_name[PATH_MAX] = {0};
                        size_t path_len = strlen(path_name);
                        size_t select_name_len = strlen(select_state.select_name);

                        if (path_len + 1 + select_name_len + 1 > sizeof(now_path_name)) {
                            editor_error_screen(&state, "path too long");
                            break;
                        }

                        // path_nameはファイルブラウザの現在位置なので、
                        // 選択したディレクトリ名を足して一覧を読み直す。
                        memcpy(now_path_name, path_name, path_len);
                        now_path_name[path_len] = '/';
                        memcpy(now_path_name + path_len + 1, select_state.select_name, select_name_len + 1);
                        memcpy(path_name, now_path_name, path_len + 1 + select_name_len + 1);
                        draw_edit_screen_base(&state, win, line_start_pos, line_end_pos);
                        load_dir_table(&state,dir_name_table,dir_name_table_size,path_name);
                        show_file_browse(&state, file_browse_box, dir_name_table, path_name, win);
                        refresh();
                    }
                    if(state.file_data.now_open_file != NULL){
                        load_screen_size(&state);
                        state.mouse.scr_abs_now_pos = (struct pos){state.write_area.x_start, state.write_area.y_start};
                        restore_edit_screen(win, &state, line_start_pos, line_end_pos);
                        draw_status_bar_path(&state, win);
                        draw_line_status(&state,win);
                        refresh();
                    }
                }
                break;
            case line_jump_mode:
                if(ch >= '0' && ch <='9' &&
                   state.jump_mode_data.jump_line_num_counter < (int)sizeof(state.jump_mode_data.jump_line_num) - 1){
                    state.jump_mode_data.jump_line_num[state.jump_mode_data.jump_line_num_counter++] = (char)ch;
                    state.jump_mode_data.jump_line_num[state.jump_mode_data.jump_line_num_counter] = '\0';
                    addch(ch);
                }
                else if(ch == CTRL('h')){
                    reset_jump_mode(&state);
                    restore_edit_screen(win, &state, line_start_pos, line_end_pos);
                }
                else if(ch == KEY_BACKSPACE && state.jump_mode_data.jump_line_num_counter > 0){

                    state.jump_mode_data.jump_line_num_counter--;
                    state.jump_mode_data.jump_line_num[state.jump_mode_data.jump_line_num_counter] = '\0';

                    struct pos prompt_pos = jump_prompt_pos(&state);
                    int prompt_x = prompt_pos.x + (int)strlen("JMP_LINE ") + 1 +
                        state.jump_mode_data.jump_line_num_counter;

                    mvaddch(prompt_pos.y, prompt_x, ' ');
                    move(prompt_pos.y, prompt_x);
                    refresh();
                }
                else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){

                    struct pos prompt_pos = jump_prompt_pos(&state);
                    mvhline(prompt_pos.y,prompt_pos.x+1,' ',
                            strlen("JMP_LINE ")+JUMP_LINE_NUM_DIGITS);
                    char *end;
                    long n = strtol(state.jump_mode_data.jump_line_num,&end, 10);

                    if(n < 1){
                        n = 1;
                    }
                    else if( n > DEFAULT_LOAD_LINE_SiZE ){
                        n = DEFAULT_LOAD_LINE_SiZE;
                    }

                    move_view_to_line(win, &state, n - 1, state.write_area.x_start,
                                      line_start_pos, line_end_pos);
                    draw_line_status(&state,win);
                    curs_set(1);
                    refresh();
                    reset_jump_mode(&state);
                    state.screen_state = edit_screen;
                }
                break;
            case error_screen:
                if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
                    clear();
                    curs_set(true);
                    state.is_cur_show = true;
                    state.screen_state = edit_screen;
                    draw_edit_screen_base(&state, win, line_start_pos, line_end_pos);
                    refresh();
                }
                break;

            case ask_make_file_mode:
            {
                if(state.make_file_mode_status.is_input_scene){
                    if(input_result == OK && iswprint((wint_t)ch) && state.ask_make_file_box.w > 
                        state.make_file_mode_status.new_file_name_counter){
                        state.make_file_mode_status.new_file_name[state.make_file_mode_status.new_file_name_counter++] = ch;
                        addch(ch);
                    }
                    else if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state.is_cur_show 
                        && state.make_file_mode_status.new_file_name_counter >0){
                        state.make_file_mode_status.new_file_name_counter--;  
                    
                        int x;
                        int y;
                        getyx(win,y,x);
                        mvaddch(y, x - 1,' ');
                        move(y, x - 1);
                    }
                    else if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' '){
                        state.make_file_mode_status.new_file_name[state.make_file_mode_status.new_file_name_counter + 1] = '\0';
                        state.screen_state = edit_screen;
                        memcpy(state.file_data.now_open_path_name,
                            state.make_file_mode_status.new_file_name,
                            sizeof(state.file_data.now_open_path_name));

                        save_file(&state);

                        state.make_file_mode_status.new_file_name_counter = 0;
                        memset(state.make_file_mode_status.new_file_name,
                            0,
                            sizeof(state.make_file_mode_status.new_file_name));
                    }
                }
                else{
                //ファイル名入力モードに入る前
                    if(ch == 'y'){
                        struct pos str_start_pos = (struct pos){state.ask_make_file_box.pos.x,state.ask_make_file_box.pos.y+1};
                        struct box write_file_name_scene_box =  
                            (struct box){state.ask_make_file_box.pos,state.ask_make_file_box.w,state.ask_make_file_box.h + 2};
                        struct box input_new_file_name_wtrite_area = 
                            (struct box){(struct pos){write_file_name_scene_box.pos.x+1,
                            write_file_name_scene_box.pos.y  + write_file_name_scene_box.h - 3},
                            write_file_name_scene_box.w - 2,2};

                        clear_box(write_file_name_scene_box);
                        draw_box(write_file_name_scene_box,win);
                        draw_box(input_new_file_name_wtrite_area,win);
                        ask_new_file_name(str_start_pos,write_file_name_scene_box.w,write_file_name_scene_box.h);
                        state.make_file_mode_status.is_input_scene = true;
                        state.is_cur_show = true;
                        move(input_new_file_name_wtrite_area.pos.y + 1,input_new_file_name_wtrite_area.pos.x + 1);
                        curs_set(true);
                        state.write_file_name_area = input_new_file_name_wtrite_area;
                        continue;
                
                    }
                    else if(ch == 'n'){
                        clear();
                        draw_edit_screen_base(&state,win,line_start_pos,line_end_pos);
                        redraw_edit_screen(win, &state,line_start_pos,line_end_pos);
                        state.screen_state = edit_screen;
                    
                    }
            
                }
                refresh();
                    
                }
        }
    }
    end_process(&state);
    return 0;
}

static void show_make_file_prompt(WINDOW *win, struct editor_state *state,struct box * file_box,
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
// 引数: target_line=画面内に表示したい論理行番号。
// 返り値: scr_start_numへ設定する表示開始行。
static int draw_start_line_for_target(struct editor_state *state,long target_line){
    int line_limit = get_line_limit();
    target_line = ( target_line + state->write_area.h > line_limit ) ? line_limit -  state->write_area.h + 15 : target_line;
    return (target_line > 15) ? (target_line - 15) : 0;
}

// redraw_edit_screen(): 編集画面の固定要素と表示中のファイル内容を再描画する。
// 引数: win=描画先ウィンドウ、state=描画対象のエディタ状態、line_start_pos/line_end_pos=区切り線の端点。
// 返り値: なし。
static void redraw_edit_screen(WINDOW *win, struct editor_state *state,
                               struct pos line_start_pos, struct pos line_end_pos){
    clear();
    draw_edit_screen_base(state, win, line_start_pos, line_end_pos);
    draw_file_data(state);
}

// restore_edit_screen(): ファイルブラウザやジャンプ入力から編集画面へ戻す。
// 引数: win=描画先ウィンドウ、state=画面状態と復帰カーソル位置、line_start_pos/line_end_pos=区切り線の端点。
// 返り値: なし。
static void restore_edit_screen(WINDOW *win, struct editor_state *state,
                                struct pos line_start_pos, struct pos line_end_pos){
    state->screen_state = edit_screen;
    state->is_cur_show = true;
    curs_set(true);
    redraw_edit_screen(win, state, line_start_pos, line_end_pos);
    move(state->mouse.scr_abs_now_pos.y, state->mouse.scr_abs_now_pos.x);
    refresh();
}

// move_view_to_line(): 指定行が見える位置へ表示開始行とカーソルを移動する。
// 引数: win=描画先ウィンドウ、state=表示位置とカーソル行、target_line=移動先行、x=移動後のx座標、line_start_pos/line_end_pos=区切り線の端点。
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

// end_process(): 読み込んだファイル行バッファと編集用バッファを解放し、
// ncursesの画面状態を通常の端末状態へ戻す。
// 引数: state=解放対象のエディタ状態。
// 返り値: なし。
static void end_process(struct editor_state *state){
    clear();
    for(int i=0;i < state->file_data.file_line_n;i++){
        free(state->file_data.file_str_data[i]);
    }
    free(state->file_data.file_str_data);
    free(state->file_data.file_line_start_num);
    free(state->str.line_str_data);
    free(state->str.line);
    endwin();
}
void my_mvaddstr(struct pos pos,char * str){
    mvaddstr(pos.y,pos.x,str);
}
void my_mvaddch(struct pos pos,char str){
    mvaddch(pos.y,pos.x,str);
}
