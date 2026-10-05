
#include <ncurses.h>
#include<stdlib.h>
#include "editor_types.h"
#include "error_log.h"
#include "filetree.h"
#include "ftj.h"
#include "txt_editor.h"
#include "txt_editor_screen.h"



int get_root_file_tree_data(file_tree_data *file_tree,char *path){
    file_tree->root_node = create_tree(path);
    if(file_tree->root_node == NULL){
        error_log("can not create file tree");
        return -1;
    }
    int mem_num = file_tree->root_node->c_table_num;
    file_tree->open_check_data = NULL;
    file_tree->open_count_num = 0;
    file_tree->is_grabed = false;
    if(mem_num == 0)return 0;

    file_tree->open_check_data = malloc(sizeof(ft_path_open_check_data) * mem_num);
    if(file_tree->open_check_data == NULL){
        close_tree(file_tree->root_node);
        file_tree->root_node = NULL;
        return -1;
    }
    file_tree->open_count_num = mem_num;

    for(int i = 0;i < mem_num;i++){
        file_tree->open_check_data[i].table_ptr = &file_tree->root_node->c_table[i];
        file_tree->open_check_data[i].is_open = false;
        file_tree->open_check_data[i].indent_num = 0;
        file_tree->open_check_data[i].screen_y = -1;
    }

    return 0;
}

ft_path_open_check_data *get_filetree_item_data(file_tree_data *file_tree,
                                                struct table *table_ptr){
    for(int i = 0;i < file_tree->open_count_num;i++){
        if(file_tree->open_check_data[i].table_ptr == table_ptr){
            return &file_tree->open_check_data[i];
        }
    }

    int new_count = file_tree->open_count_num + 1;
    ft_path_open_check_data *new_data = realloc(file_tree->open_check_data,
        sizeof(ft_path_open_check_data) * new_count);
    if(new_data == NULL)return NULL;

    file_tree->open_check_data = new_data;
    file_tree->open_count_num = new_count;
    new_data[new_count - 1].table_ptr = table_ptr;
    new_data[new_count - 1].is_open = false;
    new_data[new_count - 1].indent_num = 0;
    new_data[new_count - 1].screen_y = -1;
    return &new_data[new_count - 1];
}

// show_filetree(): 画面左端にファイルツリーの枠を作り、編集領域をその幅だけ右へ寄せる。
// 引数: ctx=画面サイズ・編集領域・ファイルツリーを持つ入力context。
// 返り値: なし。
void show_filetree(struct editor_input_context *ctx,struct box ft_box){
    struct editor_state *state = ctx->state;

    set_filetree_box(&state->file_tree_data,ft_box);
    state->file_tree_data.is_show = true;

    if(state->settings_data->show_status_bar){
        if(state->settings_data->bar_side_state == top ||
            state->settings_data->bar_side_state == bottom){
            int x = getmaxx(stdscr);
            struct box tmp_status_bar_box =
                {(struct pos){ft_box.pos.x + ft_box.w,state->status_bar->pos.y},
                x - (ft_box.pos.x + ft_box.w),
                state->status_bar->h};
            clear_status_bar_outline(state);
            set_status_bar_size(ctx,tmp_status_bar_box);
            state->render_flags |= RENDER_STATUS_BAR_LINE;
        }
    }
    // 編集領域と区切り線を新しい左端へ合わせ、新しい位置で描き直す。
    editor_apply_write_area(state);
    editor_sync_split_line(ctx);
    editor_sync_cursor(state);
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// hide_filetree(): ファイルツリーを閉じ、編集領域を元の位置へ戻す。
// 引数: ctx=画面サイズ・編集領域・ファイルツリーを持つ入力context。
// 返り値: なし。
void hide_filetree(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;

    state->file_tree_data.is_show = false;
    state->file_tree_data.ft_box = (struct box){(struct pos){0,0},0,0};
    editor_apply_write_area(state);
    editor_sync_split_line(ctx);

    // ツリーの枠が残らないよう画面全体を消してから編集画面へ戻す。
    restore_edit_screen(state);

    //ステータスバーのサイズを戻す
    if(state->settings_data->show_status_bar){
        if(state->settings_data->bar_side_state == top || 
            state->settings_data->bar_side_state == bottom){
            int status_bar_y = (state->settings_data->bar_side_state == top)?1:getmaxy(ctx->win) - 1;
            state->status_bar->pos = (struct pos){0,status_bar_y};
            state->status_bar->w = getmaxx(ctx->win);
            state->status_bar->h = 3;
        }
        state->render_flags |= RENDER_STATUS_BAR_LINE;
    }
}



bool handle_filetree_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result){
    (void)input_result;
    struct editor_state *state = ctx->state;
    if(state->is_cur_show)my_cur_set(state,false);
    
    if(ch == CTRL('n')){
        // ツリーを出したキーと同じキーで閉じる。
        hide_filetree(ctx);
        return true;
    }
    else if(ch == CTRL('h')){
        editor_set_screen_state(state,line_jump_mode);
        reset_jump_mode(state);
        state->render_flags |= RENDER_LINE_JUMP;
    }
    if(ch == 'q')return false;
    if(ch == KEY_MOUSE){
        handle_mouse(ctx,0);

        if(state->file_tree_data.is_grabed){
            MEVENT me = *ctx->mouse_event;
            int ft_w = me.x - state->file_tree_data.ft_box.pos.x;

            if(ft_w >= 3 && ft_w < getmaxx(ctx->win)){
                change_file_tree_width(ctx,ft_w);
            }
        }
    }

    if(ch == '>' || ch == '<'){
        int size_fiff = (ch == '<')?-1:1;
        int ft_w = ctx->state->file_tree_data.ft_box.w + size_fiff;
        change_file_tree_width(ctx,ft_w);
    }

    return true;
}


int set_filetree_box(file_tree_data *filetree_data,struct box filetree_box){
    filetree_data->ft_box = filetree_box;
    return 0;
}



int set_tree_item(struct box filetree_box,int layer,int pos_y,struct table *tree_table){
    int x = filetree_box.pos.x + layer;
    int y = filetree_box.pos.y + pos_y;

    for(int i = 0; i < tree_table->c_table_num;i++){
        mvaddstr(y,x,tree_table->c_table[i].s_data.d_name);
    }
    return 0;
}


int change_file_tree_width(struct editor_input_context *ctx,int width){
    int x = getmaxx(stdscr);
    
    file_tree_data *tmp_ft_data = &ctx->state->file_tree_data;
    if(tmp_ft_data->ft_box.w <= 0 || x <= tmp_ft_data->ft_box.w){
        return -1;
    }
    tmp_ft_data->ft_box.w = width;
    show_filetree(ctx,tmp_ft_data->ft_box);
    return 0;
}
