#include <stdlib.h>
#include <string.h>
#include "txt_editor_syntax.h"
#include "txt_editor_screen.h"

// handle_line_jump_mode_input(): 行ジャンプ入力中の数字入力・確定・キャンセルを処理する。
// 引数: ctx=ジャンプ後の再描画に必要なcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
bool handle_line_jump_mode_input(struct editor_input_context *ctx, wint_t ch){
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
        my_cur_set(state,true);
        reset_jump_mode(state);
        editor_set_screen_state(state, edit_screen);
        if(state->settings_data->built_in_syntax){
            syntax *syntax = now_usint_syntax_ptr_ctl(NULL,get);
            if(syntax != NULL){
                set_syntax_data(syntax,ctx);
            }
        }
    }
    return true;
}

// reset_jump_mode(): 行ジャンプ番号と入力文字数を初期状態へ戻す。
// 引数: state=行ジャンプ入力状態を持つエディタ状態。
// 返り値: なし。
void reset_jump_mode(struct editor_state *state){
    memset(state->jump_mode_data.jump_line_num, 0, sizeof(state->jump_mode_data.jump_line_num));
    state->jump_mode_data.jump_line_num_counter = 0;
}
