
#ifndef FTJ_H
#define FTJ_H

#include <dirent.h>
#include <limits.h>

struct table;

struct table{
    struct table *c_table;//child data 
    struct dirent s_data;
    char absolute_path[PATH_MAX];
    int c_table_num;
};

struct root_node{
    struct dirent s_data;//self data
    struct table *c_table;//child data
    int c_table_num;
};  


struct root_node *create_tree(const char *path);
int load_child_dir(struct table *table_ent);
void close_tree(struct root_node *tree);

#endif
