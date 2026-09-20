#ifndef FILETREE_H
#define FILETREE_H

#include <stdbool.h>
#include"editor_types.h"
#include"ftj.h"

#define FILETREE_DEFAULT_SIZE_W 10


typedef enum{
    FT_TOP,
    FT_BOTTOM,
    FT_LEFT,
    FT_RIGHT,
}filetree_side;

struct ft_path_click_data;

typedef struct{
    struct dirent s_data;
    struct box click_box;
    struct ft_path_click_data *ft_path_click_data;
}ft_path_click_data;

typedef struct{
    struct box ft_search_box;
    struct box ft_box;
    struct root_node *root_node;
    ft_path_click_data *ft_path_data;
    int ft_path_click_data_num;
    bool is_show; // ファイルツリーを表示中ならtrue。編集領域を右へ寄せる幅の計算に使う。
    filetree_side ft_side;//ファイルツリーを表示する辺


}file_tree_data;


int get_root_file_tree_data(file_tree_data *file_tree,char *path);

int set_filetree_box(file_tree_data *filetree_data,struct box filetree_box);


#endif
