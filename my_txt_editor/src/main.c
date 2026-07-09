#include <stdio.h>
#include <ncurses.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>   // SYS_getdents64
#include <sys/types.h>     // uint64_t など
#include <dirent.h>
#include<dlfcn.h>
#include<time.h>
#include <wctype.h>
#include<dirent.h>
#include <libgen.h>
#include <limits.h>
#include<unistd.h>
#include "ascii_art_comb.h"
#include "language_server_communication.h"
#include "txt_editor.h"
#include"error_log.h"


static void end_process(struct editor_state *state);


// main(): ncursesを初期化し、エディタ画面・ファイルブラウザ・エラー画面の
// 入力ループを切り替えながら各処理関数へイベントを振り分ける。
// 引数: なし。
// 返り値: 正常終了なら0、ncurses初期化やメモリ確保に失敗したら1。
int main(int argc, char *argv[])
{

    char startuptime_log_file_path_name[PATH_MAX] = {0};
    struct timespec startup_start_time;
    clock_gettime(CLOCK_MONOTONIC, &startup_start_time);

    bool startup_timer = 0;
    set_error_log_file("my_editor_error_log.txt");

    if(argc > startuptime_log_file_argument_num){
        char *result = strstr(argv[startuptime_log_file_argument_num],"startuptime_log.log");

        if(result != NULL){
            int st_up_ag_num = startuptime_log_file_argument_num;
            int startuptime_log_file_name_size = strlen(argv[st_up_ag_num]);
            if((int)sizeof(startuptime_log_file_path_name) > startuptime_log_file_name_size){
                memcpy(startuptime_log_file_path_name,argv[st_up_ag_num],startuptime_log_file_name_size);
                startuptime_log_file_path_name[startuptime_log_file_name_size] = '\0';
                startup_timer = 1;
            }
            else{
                error_log_write("path to long");
            }
        }
    }

    struct editor_settings settings_data = {0};
    struct editor_state state = {0};
    struct ascii_data ascii_data = {0};
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
    keypad(win, TRUE); 
    curs_set(0);
    
    start_color();
    if (has_colors()) {
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        attrset(COLOR_PAIR(1));
    }
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_BLACK, COLOR_RED);

    state.scr.cursor_pos.x = 0;
    state.scr.cursor_pos.y = 0;
    state.scr.scr_start_num = 0;

    state.mouse.now_mouce_line = 0;
    state.mouse.scr_abs_now_pos = (struct pos){0,0};

    getmaxyx(win, state.scr.scr_size.y, state.scr.scr_size.x);
    clear();

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
    scrollok(win, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);  
    
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
    state.status_bar   = &status_bar;
    state.write_area.w = state.write_area.x_end - state.write_area.x_start;
    state.write_area.h = state.write_area.y_end - state.write_area.y_start;

    state.file_browser_area.pos.x = file_browse_box.pos.x + 1;
    state.file_browser_area.pos.y = file_browse_box.pos.y + 1;
    state.file_browser_area.h = file_browse_box.h - 2; //底辺から1引く
    state.file_browser_area.w = file_browse_box.w - 2;//同上 

    state.make_file_mode_status.is_input_scene        = false;
    state.make_file_mode_status.new_file_name_counter = 0;
    
    memset(&state.write_file_name_area,0,sizeof(struct box));

    state.file_select_line  = 0;
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

    state.screen_state              = state.settings_data->show_start_menu ? start_menu_screen : edit_screen;
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
    
    bkgd(COLOR_PAIR(1));
    move(state.write_area.y_start, state.write_area.x_start);
    refresh();


    void *handle = NULL;
    //スタートメニュー表示判定

    Start_Menu start_menu = NULL;
    bool open_start_menu = state.settings_data->show_start_menu;
    if(state.settings_data->show_start_menu){
        handle = dlopen("so_file/start_menu_plug.so", RTLD_NOW);
        if(handle == NULL){
            handle = dlopen("/home/yuujirou07/vscode_proj/mywm_proj/my_txt_editor/so_file/start_menu_plug.so", RTLD_NOW);
        }
        if(handle == NULL){
            error_log_write("sry can not open so file :(\n");
            end_process(&state);
            return 1;
        }
        start_menu = (Start_Menu)dlsym(handle,"draw_start_menu");
        if(start_menu == NULL){
            error_log_write("dlsym failed\n");
            dlclose(handle);
            end_process(&state);
            return 1;
        }
    }


    ///LSP
    int to_child[2];    // 親からclangdへ送る用
    int from_child[2];  // clangdから親へ返す用
    int read_result  = pipe(to_child);
    int write_result = pipe(from_child);

    if(read_result == 0 && write_result == 0){
        pid_t pid;
        pid = fork();
        if (pid == -1) {
            perror("fork");
            return 1;
        }
        if(pid == 0){
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            dup2(from_child[1], STDERR_FILENO);

            close(to_child[0]);
            close(to_child[1]);
            close(from_child[0]);
            close(from_child[1]);

            char *argv[] = {
                "clangd",
                NULL
            };

            execvp("clangd", argv);
            // execvpに失敗したときだけここに来る
            perror("execvp");
            exit(1);
        }
        else if(pid > 0){
            close(to_child[0]);     // 親はここから読まない
            close(from_child[1]);   // 親はここへ書かない

            const char *json =
                "{\"jsonrpc\":\"2.0\","
                "\"id\":1,"
                "\"method\":\"initialize\","
                "\"params\":{"
                    "\"processId\":1234,"
                    "\"rootUri\":\"file:///home/yuujirou07/my_txt_editor\","
                    "\"capabilities\":{}"
                "}}";
            lsp_send(to_child[1], json);
        }
    }
    else if(read_result == -1 || write_result == -1){
        error_log_write("cant make pipe :(");
    }

    



    int running = true;
    while (running) {
        struct editor_input_context input_context = {
            .win = win,
            .mouse_event = &mouse_event,
            .state = &state,
            .file_browse_box = file_browse_box,
            .dir_name_table = dir_name_table,
            .dir_name_table_size = dir_name_table_size,
            .path_name = path_name,
            .line_start_pos = line_start_pos,
            .line_end_pos = line_end_pos,
            .screen_center_y = screen_center_y,
            .screen_center_pos = screen_center_pos,
            .open_start_menu = &open_start_menu,
            .has_start_menu = (start_menu != NULL),
            .start_menu = start_menu,
            .ascii_data = &ascii_data,
            .startup_start_time = startup_timer ? &startup_start_time : NULL,
            .startup_log_path = startup_timer ? startuptime_log_file_path_name : NULL,
        };

        if(open_start_menu && start_menu != NULL){
            state.screen_state = start_menu_screen;
        }
        if(state.screen_state == start_menu_screen){
            running = editor_handle_screen_input(&input_context, OK, 0);
            continue;
        }

        wint_t ch = 0;
        int input_result;

        input_result = get_wch(&ch);

        if (input_result == ERR)
            continue;

        if (input_result == KEY_CODE_YES && ch == KEY_RESIZE) {
            handle_resize(win, &state,&line_start_pos,&line_end_pos);
            continue;
        }
        running = editor_handle_screen_input(&input_context, input_result, ch);
        continue;

    }
    if(handle != NULL)
        dlclose(handle);
    end_process(&state);
    return 0;
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

    close_error_log_file();
    endwin();
}
void my_mvaddstr(struct pos pos,char * str){
    mvaddstr(pos.y,pos.x,str);
}
void my_mvaddch(struct pos pos,char str){
    mvaddch(pos.y,pos.x,str);
}
