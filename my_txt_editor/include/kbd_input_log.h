#ifndef KBD_INPUT_LOG_H
#define KBD_INPUT_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include "editor_types.h"


#define KEY_LOG_ALLOCATE_NUM 32

struct editor_state;

typedef struct{
    wchar_t key;
    int input_result;
    screen_state screen_state;
}key_data;

typedef struct{
    key_data *key_log;
    size_t key_count;
    size_t key_allocate_num;
}key_log_data;


typedef struct{
    key_log_data Key_log_data;
}key_bord_data;

int KL_add(struct editor_state *state,wchar_t ch,int result,screen_state screen_state);
int KL_write_file();
int KL_GetScreenLog(screen_state screen_state,struct editor_state *state,key_data **data,size_t *n);

int KL_open_key_log_file(const char *path);
#endif
