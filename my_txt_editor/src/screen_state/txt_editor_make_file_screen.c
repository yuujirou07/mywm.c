#include <string.h>
#include <wctype.h>
#include "txt_editor_screen.h"

// handle_ask_make_file_mode_input(): 未保存ファイル作成確認と新規ファイル名入力を処理する。
// 引数: ctx=確認ダイアログと編集画面復帰に必要なcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch){
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
            my_cur_set(state,true);
        }

    }
    return true;
}

// show_make_file_prompt(): 保存先が無いときにファイル作成確認の小画面を描く。
// 引数: win=描画先、state=画面状態、file_box=作成した確認枠の保存先、screen_center_y/screen_center_pos=配置基準。
// 返り値: なし。
void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                                  int screen_center_y, struct pos screen_center_pos){
    (void)win;
    char comment_str[] = "The file cannot be found; would you like to create it?";
    int comment_str_len = strlen(comment_str);
    int box_h = (comment_str_len / state->scr.scr_size.x + 1) + 3;
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
