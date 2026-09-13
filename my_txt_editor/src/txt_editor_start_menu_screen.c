#include "start_menu.h"
#include "txt_editor_screen.h"

bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch){
    (void)ch;
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(ctx->start_menu_screen.plugin == NULL){
        restore_edit_screen(state);
        return true;
    }

    state->is_cur_show = false;
    curs_set(0);
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
        state->is_cur_show = false;
        curs_set(0);

        struct box clear_area;
        int logo_h = ctx->start_menu_screen.ascii_data != NULL
            ? ctx->start_menu_screen.ascii_data->h : 0;
        clear_area.pos = (struct pos){0,logo_h};
        clear_area.w = state->scr.scr_size.x - 1;
        clear_area.h = state->scr.scr_size.y - logo_h;

        request_clear_box(state,clear_area);
        show_file_browse(state, ctx->file_browse_screen.box,
                         ctx->file_browse_screen.dir_name_table,
                         ctx->file_browse_screen.path_name, win);
        refresh();
        return true;
    }
    else if(start_menu_result == new_file){
        editor_set_screen_state(state, edit_screen);
        my_cur_set(state,true);
        clear();
        state->scr.scr_start_num = 0;
        editor_set_cursor(state, 0, 0);
        editor_sync_cursor(state);
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
        return true;
    }
    else if(start_menu_result == settings){
        editor_set_screen_state(state,setting_screen);
        my_cur_set(state,false);

        int y,x;
        getmaxyx(ctx->win,y,x);
        int pos_x = x/5;
        int pos_y = y/10;
        struct pos settings_pos = {pos_x,pos_y};
        struct box settings_box = {settings_pos,x - (pos_x * 2),y - (pos_y * 2)};
        request_draw_box(state,settings_box);
        request_clear_box(state,settings_box);
        return true;
    }

    editor_set_screen_state(state, edit_screen);
    state->is_cur_show = true;
    curs_set(1);
    return true;
}
