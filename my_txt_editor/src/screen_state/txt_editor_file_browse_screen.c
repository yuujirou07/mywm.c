#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wctype.h>
#include "error_log.h"
#include "txt_editor_screen.h"

// handle_file_browse_screen_input(): ファイルブラウザ画面の移動・選択・復帰を処理する。
// 引数: ctx=ファイル一覧・現在パス・描画先を持つcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
bool handle_file_browse_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch){
    struct editor_state *state = ctx->state;
    struct file_browse_screen_context *file_browse_screen = &ctx->file_browse_screen;

    //もしインプットモードなら英数字のキーには反応させない
    bool input_mode = get_file_browse_path_input_mode(&ctx->file_browse_screen);

    if (ch == CTRL('f')) {
        restore_edit_screen(state);
        return true;
    }


    //パスインプットモード判定
    if(ch == 'i' && !input_mode){
        state->is_cur_show = true;
        curs_set(1);
        set_file_browse_path_input_mode(
            file_browse_screen,
            true
            );
        state->file_select_line_data.now_logical_line = 0;


        size_t now_open_path_len = strlen(ctx->file_browse_screen.path_name);
        char tmp_now_open_path[now_open_path_len + 2];
        strcpy(tmp_now_open_path,ctx->file_browse_screen.path_name);
        char joint_str[2] = {'/','\0'};
        strcat(tmp_now_open_path,joint_str);
        struct dir_table now_path = {.path_name = tmp_now_open_path};
        now_open_path_name(&now_path,set);
        load_dir_table(
            state,&ctx->file_browse_screen.dir_name_table,
            &ctx->file_browse_screen.dir_name_table_rows,
            ctx->file_browse_screen.path_name,
            0,
            &ctx->file_browse_screen.dir_num,
            &ctx->file_browse_screen.dir_name_table_num
        );
        state->render_flags |= RENDER_FILE_BROWSE;

        return true;
    }

    if(input_mode == false){
        if(state->settings_data->file_select_scene_lighting){

            if(ch == '\t') {
                if(editor_get_screen_state_log(state,1) == start_menu_screen){
                    editor_set_screen_state(state, start_menu_screen);
                    *ctx->start_menu_screen.open = true;

                    set_file_browse_path_input_mode(file_browse_screen,
                        false);
                }
                else{
                    editor_set_screen_state(state, edit_screen);
                    restore_edit_screen(state);
                    set_file_browse_path_input_mode(file_browse_screen,
                        false);
                }

                return true;
            }

            if(ch == KEY_MOUSE){
                handle_mouse(ctx,ctx->file_browse_screen.dir_name_table_num);
            }


            //親ディレクトリに戻る
            if(ch == 'h'&& !input_mode){

                char *child_dir = NULL;
                if((child_dir = strrchr(ctx->file_browse_screen.path_name,'/'))==NULL){
                    return 1;
                }
                if(strchr(ctx->file_browse_screen.path_name,'/') == child_dir){
                    *(child_dir+1) = '\0';
                }
                else{
                    *(child_dir) = '\0';
                }

                state->file_select_line_data.now_logical_line = 0;
                load_dir_table(state, &ctx->file_browse_screen.dir_name_table,
                    &ctx->file_browse_screen.dir_name_table_rows,
                    ctx->file_browse_screen.path_name,
                    0,
                    &ctx->file_browse_screen.dir_num,
                    &ctx->file_browse_screen.dir_name_table_num);
                state->render_flags |= RENDER_FILE_BROWSE;
            }

        }
        if(ch == KEY_ENTER || ch == '\n' || ch == '\r' || ch == ' ' || (ch == 'l' && input_mode)) {
        // select_state.select_nameが空でなければディレクトリ選択、空ならファイル読み込み完了側を見る。
            struct file_browse_select_state select_state;
            load_file(state, ctx->file_browse_screen.dir_name_table,
                    ctx->file_browse_screen.dir_name_table_num,
                    ctx->file_browse_screen.path_name, &select_state);

            if(select_state.select_name[0] != '\0' && select_state.select_state == folder){
                struct dir_table now_path = {
                    .path_name = ctx->file_browse_screen.path_name,
                };
                now_open_path_name(&now_path,set);
                char now_path_name[PATH_MAX] = {0};
                const char *path_name = ctx->file_browse_screen.path_name;
                const char *separator = strcmp(path_name, "/") == 0 ? "" : "/";
                int path_len = snprintf(now_path_name, sizeof(now_path_name), "%s%s%s",
                                        path_name, separator, select_state.select_name);
                if(path_len < 0 || (size_t)path_len >= sizeof(now_path_name)){
                    editor_error_screen(state, "path too long");
                    return true;
                }
                memcpy(ctx->file_browse_screen.path_name, now_path_name, (size_t)path_len + 1);
                state->file_select_line_data.now_logical_line = 0;
                load_dir_table(state, &ctx->file_browse_screen.dir_name_table,
                    &ctx->file_browse_screen.dir_name_table_rows,
                        ctx->file_browse_screen.path_name,
                        0,
                        &ctx->file_browse_screen.dir_num,
                        &ctx->file_browse_screen.dir_name_table_num);

                state->render_flags |= RENDER_FILE_BROWSE;
                ctx->file_browse_screen.path_input_mode = false;
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
                //ここでディレクトリ移動
                chdir(ctx->file_browse_screen.path_name);
                // 読み込み直後は先頭行の行頭から編集を始める。
                editor_set_cursor(state, 0, 0);
                restore_edit_screen(state);

            }
        }
    }
    else{
        const wchar_t *path = now_open_path_name(NULL,get);
        size_t path_len = wcslen(path);

        if(ch == KEY_BACKSPACE){
            wchar_t  tmp_path[path_len + 1];
            memcpy(tmp_path,path,sizeof(wchar_t) * (path_len+1));
            char mb_path[PATH_MAX];
            if(path_len > 0){
                tmp_path[path_len-1] = L'\0';

                if(wcstombs(mb_path, tmp_path, sizeof(mb_path)) != (size_t)-1){
                    struct dir_table now_path = {.path_name = mb_path};
                    now_open_path_name(&now_path,set);
                    state->render_flags |= RENDER_FILE_BROWSE;
                }
            }
        }
        if(input_result == OK && iswprint(ch)){
            wchar_t tmp_path[path_len + 2];
            memcpy(tmp_path,path,sizeof(wchar_t) * (path_len+1));
            tmp_path[path_len] = ch;
            tmp_path[path_len + 1] = L'\0';
            char mb_path[PATH_MAX];
            if(wcstombs(mb_path, tmp_path, sizeof(mb_path)) != (size_t)-1){
                struct dir_table now_path = {.path_name = mb_path};
                now_open_path_name(&now_path,set);
                state->render_flags |= RENDER_FILE_BROWSE;
            }
        }
        if(ch == '\t' || ch == '\0' || ch == '\r' || ch == '\n' || ch == KEY_ENTER){

            const wchar_t *path = now_open_path_name(NULL,get);
            if(path == NULL)return true;
            char char_old_path[PATH_MAX];
            size_t converted = wcstombs(char_old_path,path,sizeof(char_old_path) - 1);

            enum select_state path_state = get_path_state(char_old_path);
            if(path_state == file){
                now_input_path_open(state,ctx);
                return true;
            }

            int selected_line = state->file_select_line_data.now_line;
            if(selected_line < 0 || selected_line >= ctx->file_browse_screen.dir_name_table_num)return true;

            int candidate_rows = selected_line + 1;
            struct dir_table candidates[candidate_rows];
            int candidate_count = check_dir_mem(candidates,candidate_rows);
            //もし選択行よりメンバが少なかったらバグっているのでエラー画面を出す
            if(candidate_count <= selected_line){
                editor_error_screen(state,"directory error");
                return true;
            }
            struct dir_table *candidate = &candidates[selected_line];
            if(path == NULL){
                error_log("can not get open path name");
                return true;
            }

            if(converted == (size_t)-1){
                return true;
            }
            char_old_path[converted] = '\0';
            char *last_slash = strrchr(char_old_path,'/');
            if(last_slash != NULL){
                *(last_slash+1) = '\0';
            }

            char char_path[PATH_MAX];
            const char *separator = (candidate->d_type == DT_DIR) ? "/" : "";
            int path_size = snprintf(char_path,sizeof(char_path),"%s%s%s",
                char_old_path,candidate->d_name,separator);
            if(path_size >= 0 && (size_t)path_size < sizeof(char_path)){
                struct dir_table now_path = {
                    .path_name = char_path,
                };
                now_open_path_name(&now_path,set);
                if(candidate->d_type != DT_REG)
                    state->render_flags |= RENDER_FILE_BROWSE;
            }
            if(candidate->d_type == DT_REG){
                now_input_path_open(state,ctx);
                return true;
            }
        }


        if(ch == 'q'){
            return false;
        }
        if(ch == KEY_BACKSPACE || (input_result == OK && iswprint(ch)) || ch == '\t' || ch == '\n'){
            int table_rows = ctx->file_browse_screen.dir_name_table_rows;
            struct dir_table table[table_rows];
            int men_num = check_dir_mem(table,table_rows);
            if(men_num >= 0){
                ctx->file_browse_screen.dir_num = men_num;
                ctx->file_browse_screen.dir_name_table_num = men_num;
                state->file_select_line_data.now_logical_line = 0;
                if(men_num > 0 &&
                   (state->file_select_line_data.now_line < 0 ||
                    state->file_select_line_data.now_line >= men_num)){
                    set_file_select_line(state,ctx->file_browse_screen.dir_name_table_num,0);
                }
                for(int i = 0; i < table_rows;i++){
                    if(i < men_num){

                        snprintf(ctx->file_browse_screen.dir_name_table[i].name,
                            sizeof(ctx->file_browse_screen.dir_name_table[i].name),
                            "%s",table[i].d_name);
                        ctx->file_browse_screen.dir_name_table[i].d_type = table[i].d_type;
                    }
                    else{
                        ctx->file_browse_screen.dir_name_table[i].name[0] = '\0';
                        ctx->file_browse_screen.dir_name_table[i].d_type = DT_UNKNOWN;
                    }
                }

                set_clear_box(&state->clear_box_data,*state->file_browser_box);
                state->render_flags |= RENDER_FILE_BROWSE;
            }
        }
    }
    // now_lineは画面内の位置、now_logical_lineは全件テーブルの開始位置として別に動かす。
    if(ch == KEY_UP || (ch == 'k' && !input_mode)){
        int visible_num = ctx->file_browse_screen.dir_num -
            state->file_select_line_data.now_logical_line;
        if(visible_num > state->file_browser_area.h)visible_num = state->file_browser_area.h;
        int next_line = state->file_select_line_data.now_line;
        if(next_line > 0){
            next_line--;
        }
        else if(state->file_select_line_data.now_logical_line > 0){
            state->file_select_line_data.now_logical_line--;
        }
        set_file_select_line(state, visible_num, next_line);
        state->render_flags |= RENDER_FILE_BROWSE;

    } else if(ch == KEY_DOWN || (ch == 'j' && !input_mode)){
        int visible_num = ctx->file_browse_screen.dir_num -
            state->file_select_line_data.now_logical_line;
        if(visible_num > state->file_browser_area.h)visible_num = state->file_browser_area.h;
        int next_line = state->file_select_line_data.now_line;
        if(next_line + 1 < visible_num){
            next_line++;
        }
        else if(state->file_select_line_data.now_logical_line + visible_num <
                ctx->file_browse_screen.dir_num){
            state->file_select_line_data.now_logical_line++;
        }
        set_file_select_line(state, visible_num, next_line);
        state->render_flags |= RENDER_FILE_BROWSE;
    }

    return true;
}
