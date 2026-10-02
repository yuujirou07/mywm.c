#ifndef FILETREE_H
#define FILETREE_H

#include <stdbool.h>
#include"editor_types.h"

#define FILETREE_DEFAULT_SIZE_W 10 // ファイルツリーの既定幅（端末セル数）。


// ファイルツリーを接続する画面端。
typedef enum{
    FT_TOP, // 画面上端。
    FT_BOTTOM, // 画面下端。
    FT_LEFT, // 画面左端。
    FT_RIGHT, // 画面右端。
}filetree_side;

struct ft_path_click_data;
struct editor_input_context;

// ツリー内の1項目について、開閉状態と直近の描画位置を保持する。
typedef struct{
    struct table *table_ptr; // root_node配下の項目への借用ポインタ。
    bool is_open; // ディレクトリの子要素を展開中ならtrue。
    int indent_num; // ルート直下を0とするツリー階層。
    int screen_y; // 直近の描画先となる画面y座標。非表示なら-1。
}ft_path_open_check_data;

// ファイルツリー画面の形状、ツリー本体、項目ごとの表示状態。
typedef struct{
    struct box ft_search_box; // 検索欄として予約する画面上の矩形。
    struct box ft_box; // ファイルツリー全体の外枠。
    struct root_node *root_node; // create_tree()で確保し、close_tree()で解放するツリーの根。
    ft_path_open_check_data *open_check_data; // この構造体が所有する項目状態の動的配列。
    int open_count_num; // open_check_dataに格納済みの有効要素数。
    bool is_show; // ファイルツリーを表示中ならtrue。編集領域を右へ寄せる幅の計算に使う。
    bool is_grabed; // 枠線をマウスで掴み、幅を変更中ならtrue。
    filetree_side ft_side; // ファイルツリーを接続する画面端。
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
