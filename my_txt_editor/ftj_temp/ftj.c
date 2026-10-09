#include "ftj.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int table_name_compare(const void *left, const void *right){
    const struct table *a = left;
    const struct table *b = right;

    if(a->s_data.d_type == DT_DIR && b->s_data.d_type != DT_DIR)return -1;
    if(a->s_data.d_type != DT_DIR && b->s_data.d_type == DT_DIR)return 1;
    return strcasecmp(a->s_data.d_name,b->s_data.d_name);
}

static void close_table_children(struct table *table){
    if(table == NULL)return;
    for(int i = 0;i < table->c_table_num;i++){
        close_table_children(&table->c_table[i]);
    }
    free(table->c_table);
    table->c_table = NULL;
    table->c_table_num = 0;
}

static int scan_directory(const char *path,struct table **entries,int *entry_count){
    DIR *dir = opendir(path);
    if(dir == NULL)return -1;

    struct table *table = NULL;
    int count = 0;
    struct dirent *entry;
    errno = 0;

    while((entry = readdir(dir)) != NULL){
        if(strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0){
            continue;
        }

        if(count == INT_MAX / (int)sizeof(*table)){
            errno = EOVERFLOW;
            goto fail;
        }

        struct table *grown = realloc(table,(size_t)(count + 1) * sizeof(*table));
        if(grown == NULL)goto fail;
        table = grown;
        struct table *item = &table[count];
        memset(item,0,sizeof(*item));

        int path_len = snprintf(item->absolute_path,sizeof(item->absolute_path),
                                "%s/%s",path,entry->d_name);
        if(path_len < 0 || (size_t)path_len >= sizeof(item->absolute_path)){
            errno = ENAMETOOLONG;
            goto fail;
        }

        size_t name_len = strlen(entry->d_name);
        if(name_len >= sizeof(item->s_data.d_name)){
            errno = ENAMETOOLONG;
            goto fail;
        }
        memcpy(item->s_data.d_name,entry->d_name,name_len + 1);

        unsigned char type = entry->d_type;
        if(type == DT_UNKNOWN){
            struct stat info;
            if(lstat(item->absolute_path,&info) != 0)goto fail;
            if(S_ISDIR(info.st_mode))type = DT_DIR;
            else if(S_ISREG(info.st_mode))type = DT_REG;
            else if(S_ISLNK(info.st_mode))type = DT_LNK;
            else type = DT_UNKNOWN;
        }
        item->s_data.d_type = type;
        count++;
        errno = 0;
    }

    if(errno != 0)goto fail;
    if(closedir(dir) != 0){
        free(table);
        return -1;
    }

    if(count > 1)qsort(table,(size_t)count,sizeof(*table),table_name_compare);
    *entries = table;
    *entry_count = count;
    return 0;

fail: {
        int saved_errno = errno;
        closedir(dir);
        free(table);
        errno = saved_errno;
        return -1;
    }
}

struct root_node *create_tree(const char *path){
    if(path == NULL){
        errno = EINVAL;
        return NULL;
    }

    struct root_node *root = calloc(1,sizeof(*root));
    if(root == NULL)return NULL;

    if(scan_directory(path,&root->c_table,&root->c_table_num) != 0){
        int saved_errno = errno;
        free(root);
        errno = saved_errno;
        return NULL;
    }
    return root;
}

int load_child_dir(struct table *table){
    if(table == NULL || table->s_data.d_type != DT_DIR){
        errno = EINVAL;
        return -1;
    }
    if(table->children_loaded)return 0;

    struct table *children = NULL;
    int child_count = 0;
    if(scan_directory(table->absolute_path,&children,&child_count) != 0){
        return -1;
    }

    table->c_table = children;
    table->c_table_num = child_count;
    table->children_loaded = true;
    return 0;
}

void close_tree(struct root_node *root){
    if(root == NULL)return;
    for(int i = 0;i < root->c_table_num;i++){
        close_table_children(&root->c_table[i]);
    }
    free(root->c_table);
    free(root);
}
