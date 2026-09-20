
#include <ncurses.h>
#include<stdlib.h>
#include "editor_types.h"
#include "error_log.h"
#include "filetree.h"
#include "ftj.h"
#include "txt_editor.h"



int get_root_file_tree_data(file_tree_data *file_tree,char *path){
    file_tree->root_node = create_tree(path);
    if(file_tree->root_node == NULL){
        error_log("can not create file tree");
        return -1;
    }
    int mem_num = file_tree->root_node->c_table_num;
    file_tree->open_check_data = NULL;
    file_tree->open_count_num = 0;
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
void show_filetree(struct editor_input_context *ctx){
    struct editor_state *state = ctx->state;

    struct box tree_box = (struct box){
        .pos = {0,0},
        .w = state->scr.scr_size.x / FILETREE_DEFAULT_SIZE_W,
        .h = state->scr.scr_size.y,
    };
    set_filetree_box(&state->file_tree_data,tree_box);
    state->file_tree_data.is_show = true;

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
}



bool handle_filetree_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result){
    (void)input_result;

    if(ch == CTRL('n')){
        // ツリーを出したキーと同じキーで閉じる。
        hide_filetree(ctx);
        return true;
    }
    if(ch == 'q')return false;
    if(ch == KEY_MOUSE){
        handle_mouse(ctx,0);
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
