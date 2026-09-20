
#include "ftj.h"
#include "txt_editor.h"



int get_file_tree_data(file_tree_data *file_tree,char *path){
    file_tree->root_node = create_tree(path);
    if(file_tree->root_node == NULL)return -1;
    return 0;
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


    return true;
}


int set_filetree_box(file_tree_data *filetree_data,struct box filetree_box){
    filetree_data->ft_box = filetree_box;
    return 0;
}
