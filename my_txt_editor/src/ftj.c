#include <dirent.h>
#include <stdio.h>
#include<stdlib.h>
#include <string.h>
#include"ftj.h"




int check_child_num(DIR *dir);
static int is_dot_entry(const char *name);

// is_dot_entry(): ディレクトリ自身を指す"."と親を指す".."かどうかを返す。
// どちらも対象ディレクトリ内の実要素ではないため一覧から除外する。
// 引数: name=readdirが返したエントリ名。
// 返り値: "."または".."なら1、それ以外は0。
static int is_dot_entry(const char *name){
    return strcmp(name,".") == 0 || strcmp(name,"..") == 0;
}

// create_tree(): pathで指定したディレクトリ直下の要素を読み込む。
// 引数: path=読み込む起点ディレクトリ。"."と".."は対象外。
// 返り値: 読み込んだ木。pathがNULL、またはopendirに失敗したらNULL。
struct root_node *create_tree(const char *path){
    if(path == NULL)return NULL;
    char root_path[PATH_MAX];
    if(realpath(path,root_path) == NULL)return NULL;
    DIR *Root_DIR = opendir(root_path);
    
    if(Root_DIR == NULL)return NULL;

    struct root_node *Root_Node = malloc(sizeof(struct root_node));
    if(Root_Node == NULL){
        closedir(Root_DIR);
        return NULL;
    }
    Root_Node->c_table = NULL;
    Root_Node->c_table_num = 0;

    int child_num = check_child_num(Root_DIR);
    if(child_num <= 0){
        closedir(Root_DIR);
        return Root_Node;
    }

    Root_Node->c_table = malloc(sizeof(struct table) * child_num);
    if(Root_Node->c_table == NULL){
        free(Root_Node);
        closedir(Root_DIR);
        return NULL;
    }
    

    int load_num = 0;
    while(load_num < child_num){
        struct dirent *root_dirent = readdir(Root_DIR);
        if(root_dirent == NULL)break;

        if(is_dot_entry(root_dirent->d_name))continue;

        struct table *child = &Root_Node->c_table[load_num];
        const char *separator = strcmp(root_path,"/") == 0 ? "" : "/";
        int path_len = snprintf(child->absolute_path,sizeof(child->absolute_path),"%s%s%s",
            root_path,separator,root_dirent->d_name);
        if(path_len < 0 || (size_t)path_len >= sizeof(child->absolute_path))continue;

        child->s_data = *root_dirent;
        child->c_table = NULL;
        child->c_table_num = 0;
        load_num++;
    }
    Root_Node->c_table_num = load_num;
    
    closedir(Root_DIR);
    return Root_Node;
}

// check_child_num(): dir内の子の数を数える。"."と".."は子として数えない
// (読み込みループ側でも同じ条件で読み飛ばすため、数と実際に読む数を揃える)。
// 引数: dir=数えるディレクトリ。
// 返り値: "."と".."を除いた子の数。
int check_child_num(DIR *dir){
    rewinddir(dir);
    int c_count = 0;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL){
        if(is_dot_entry(entry->d_name))continue;
        c_count++;
    }
    rewinddir(dir);
    return c_count;
}

int load_child_dir(struct table *table_ent){
    if(table_ent == NULL || table_ent->absolute_path[0] == '\0')return -1;
    if(table_ent->c_table != NULL)return 0;

    struct root_node *child_root = create_tree(table_ent->absolute_path);
    if(child_root == NULL)return -1;

    table_ent->c_table = child_root->c_table;
    table_ent->c_table_num = child_root->c_table_num;
    free(child_root);
    return 0;
}



void close_table(struct table *table,int table_num){
    if(table == NULL)return;
    for(int i = 0;i < table_num;i++){
        close_table(table[i].c_table,table[i].c_table_num);
        table[i].c_table = NULL;
        table[i].c_table_num = 0;
    }
    free(table);
}







void close_tree(struct root_node *tree){
    if(tree == NULL)return;
    close_table(tree->c_table,tree->c_table_num);
    tree->c_table = NULL;
    tree->c_table_num = 0;
    free(tree);
}
