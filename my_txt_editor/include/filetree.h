#ifndef FILETREE_H
#define FILETREE_H

#include <stdbool.h>
#include"editor_types.h"
#include"ftj.h"

#define FILETREE_DEFAULT_SIZE_W 10



typedef struct{
    struct box file_tree_search_box;
    struct box file_tree_box;
    struct root_node *root_node;
    int now_search_layer_num;
    bool is_show; // ファイルツリーを表示中ならtrue。編集領域を右へ寄せる幅の計算に使う。
}file_tree_data;


int get_file_tree_data(file_tree_data *file_tree,char *path);

int set_filetree_box(file_tree_data *filetree_data,struct box filetree_box);


#endif