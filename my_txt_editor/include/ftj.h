
#ifndef FTJ_H
#define FTJ_H

#include <dirent.h>
#include <limits.h>

struct table;

// ディレクトリツリー内の1項目。子配列を再帰的に所有する。
struct table{
    struct table *c_table; // 子項目の動的配列。子が無ければNULL。
    struct dirent s_data; // この項目自身の名前と種別。
    char absolute_path[PATH_MAX]; // この項目を指すNUL終端の絶対パス。
    int c_table_num; // c_tableに格納済みの子項目数。
};

// create_tree()が返すディレクトリツリーの根。close_tree()が全体を解放する。
struct root_node{
    struct dirent s_data; // ルートディレクトリ自身の情報。
    struct table *c_table; // 直下の項目を格納する動的配列。
    int c_table_num; // c_tableに格納済みの子項目数。
};  


struct root_node *create_tree(const char *path);
int load_child_dir(struct table *table_ent);
void close_tree(struct root_node *tree);

#endif
