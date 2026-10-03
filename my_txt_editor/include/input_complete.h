

#ifndef EDIT_INPUT_COMPLETE_H
#define EDIT_INPUT_COMPLETE_H

#include<editor_types.h>
#include<stdbool.h>

struct editor_input_context;

typedef struct{
    char **world;
    int world_num;
    int world_allocate_num;
}complete_world_data;


typedef struct{
    bool show;
    struct pos size;
    complete_world_data word_data;
    language lang;
}edit_input_complete_data;


int init_edit_complete_data(struct editor_input_context *ctx);


#endif