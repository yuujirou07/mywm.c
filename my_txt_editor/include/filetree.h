#ifndef FILETREE_H
#define FILETREE_H

#include <stdbool.h>
#include"editor_types.h"

#define FILETREE_DEFAULT_SIZE_W 10


typedef enum{
    FT_TOP,
    FT_BOTTOM,
    FT_LEFT,
    FT_RIGHT,
}filetree_side;

struct ft_path_click_data;
struct editor_input_context;

typedef struct{
    struct table *table_ptr;
    bool is_open;
    int indent_num;
    int screen_y;
}ft_path_open_check_data;

typedef struct{
    struct box ft_search_box;
    struct box ft_box;
    struct root_node *root_node;
    ft_path_open_check_data *open_check_data;
    int open_count_num;
    bool is_show; // ファイルツリーを表示中ならtrue。編集領域を右へ寄せる幅の計算に使う。
    bool is_grabed;//ファイルツリーの枠線を掴みサイズを変更しているか
    filetree_side ft_side;//ファイルツリーを表示する辺
}file_tree_data;


int get_root_file_tree_data(file_tree_data *file_tree,char *path);

ft_path_open_check_data *get_filetree_item_data(file_tree_data *file_tree,
                                                struct table *table_ptr);

int set_filetree_box(file_tree_data *filetree_data,struct box filetree_box);

int change_file_tree_width(struct editor_input_context *ctx,int width);
void show_filetree(struct editor_input_context *ctx,struct box ft_box);
void hide_filetree(struct editor_input_context *ctx);
int filetree_mouse_event(struct editor_input_context *ctx);


#endif
