#include "txt_editor_screen.h"

// handle_error_screen_input(): エラー画面でEnterが押されたら遷移元に応じて画面へ戻す。
// 引数: ctx=編集画面復帰に必要なcontext、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue。
bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch){
    struct editor_state *state = ctx->state;

    if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
        clear();
        enum now_screen_state previous_state = editor_get_screen_state_log(state,1);
        // start menu由来のfile browserで発生したエラーでは、2つ前が戻り先になる。
        enum now_screen_state previous_previous_state =
            editor_get_screen_state_log(state,2);

        if(previous_state == start_menu_screen ||
           (previous_state == file_browse_screen &&
            previous_previous_state == start_menu_screen)){
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
