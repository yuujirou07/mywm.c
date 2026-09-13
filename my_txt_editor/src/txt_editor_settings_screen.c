#include "txt_editor_screen.h"

bool handle_settings_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result){
    struct editor_state *state = ctx->state;
    if(ch == 'q')return false;
    if(input_result == ERR)editor_error_screen(ctx->state,"key input error");
    if(ch == '\t'){
        enum now_screen_state old_state = editor_get_screen_state_log(state,1);
        if(old_state == screen_state_log_error)editor_error_screen(state,"editor screen error");
        else editor_set_screen_state(state,old_state);
    }
    return true;
}
