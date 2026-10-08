#ifndef FTJ_H
#define FTJ_H

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>

struct table {
    struct {
        char d_name[NAME_MAX + 1];
        unsigned char d_type;
    } s_data;
    char absolute_path[PATH_MAX];
    struct table *c_table;
    int c_table_num;
    bool children_loaded;
};

struct root_node {
    struct table *c_table;
    int c_table_num;
};

struct root_node *create_tree(const char *path);
int load_child_dir(struct table *table);
void close_tree(struct root_node *root);

#endif
