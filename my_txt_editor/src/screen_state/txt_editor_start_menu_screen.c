#include "start_menu.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"

// handle_start_menu_input(): start menu pluginを実行し、選択結果に対応する画面へ遷移する。
// 引数: ctx=pluginと各遷移先の状態を持つcontext、ch=dispatcherとの共通形式用で未使用。
// 返り値: 入力ループを続けるならtrue、pluginが終了を要求したらfalse。
bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch){
    (void)ch;
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(ctx->start_menu_screen.plugin == NULL){
        restore_edit_screen(state);
        return true;
    }

    my_cur_set(state,false);
    clear();
    int start_menu_result = ctx->start_menu_screen.plugin(
        state->scr.scr_size.x,state->scr.scr_size.y,
        ctx->start_menu_screen.ascii_data,
        ctx->start_menu_screen.startup_start_time,
        ctx->start_menu_screen.startup_log_path);
    flushinp();
    *ctx->start_menu_screen.open = false;

    if(start_menu_result == resize_request){
        handle_resize(win, ctx);
        return true;
    }
    else if(start_menu_result == quit){
        return false;
    }
    else if(start_menu_result == select_folder){
        editor_set_screen_state(state, file_browse_screen);
        my_cur_set(state,false);

        struct box clear_area;
        int logo_h = ctx->start_menu_screen.ascii_data != NULL
            ? ctx->start_menu_screen.ascii_data->h : 0;
        clear_area.pos = (struct pos){0,logo_h};
        clear_area.w = state->scr.scr_size.x - 1;
        clear_area.h = state->scr.scr_size.y - logo_h;

        request_clear_box(state,clear_area);
        show_file_browse(state);
        refresh();
        return true;
    }
    else if(start_menu_result == new_file){
        editor_set_screen_state(state, edit_screen);
        my_cur_set(state,true);
        clear();
        state->scr.scr_start_num = 0;

        state->file_data.now_open_path_name[0] = '\0';
        editor_set_cursor(state, 0, 0);
        editor_sync_cursor(state);
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
        return true;
    }
    else if(start_menu_result == settings){
        editor_set_screen_state(state,setting_screen);
        my_cur_set(state,false);

        //項目数と項目名の長さに合わせて枠を決め、画面の中央へ置く。
        set_settings_screen_box(state);
        struct box settings_box = state->settings_screen_data.box;
        // 枠はdraw_settings_screen()が描く。ここで要求するとRENDER_SETTINGSより
        // 後に枠が描き直され、枠上辺のタイトルが消える。
        request_clear_box(state,settings_box);
        state->render_flags |= RENDER_SETTINGS;
        return true;
    }

    editor_set_screen_state(state, edit_screen);
    my_cur_set(state,true);
    return true;
}
