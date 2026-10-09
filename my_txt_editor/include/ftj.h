
#ifndef FTJ_H
#define FTJ_H

#include <dirent.h>
#include <limits.h>

struct table;

/* 木が所有する子エントリ。配列や個別要素は利用者が直接freeしない。 */
struct table{
    struct table *c_table;//child data 
    struct dirent s_data;
    char absolute_path[PATH_MAX]; /* NUL終端の絶対パス。 */
    int c_table_num; /* c_tableの要素数。0は空または未読み込み。 */
};

/* 起点の所有ハンドル。s_dataは現実装で初期化されないため参照しない。 */
struct root_node{
    struct dirent s_data;//self data
    struct table *c_table;//child data
    int c_table_num; /* c_tableの要素数。0は空または未読み込み。 */
};  


/* 指定ディレクトリ直下を読み込み、子の読み込みは遅延する。
 * 引数: path = 非NULLのNUL終端ディレクトリパス。
 * 戻り値: 所有root_node。解決・open・確保失敗ならNULL。空ディレクトリも成功。close_treeで解放する。
 */
struct root_node *create_tree(const char *path);
/* 1エントリのディレクトリ直下を読み込み、エントリに所有させる。
 * 引数: table_ent = 木の中のエントリ。absolute_pathに有効なディレクトリパスが必要。
 * 戻り値: 成功または読み込み済みで0、NULL・空パス・読み込み失敗で-1。
 */
int load_child_dir(struct table *table_ent);
/* 読み込み済みの子を再帰解放し、root_nodeも解放する。
 * 引数: tree = create_treeの戻り値。NULL可。呼び出し後は使用不可。
 */
void close_tree(struct root_node *tree);

#endif
