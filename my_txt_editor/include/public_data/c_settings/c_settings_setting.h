

#ifndef C_SETTINGS_SETTING_H
#define C_SETTINGS_SETTING_H



#include <wchar.h>


typedef struct MY_TXT_EDITOR_API MY_TXT_EDITOR_API;

typedef void (*EDITOR_ACTION)(const MY_TXT_EDITOR_API *);
typedef void (*KEY_SET)(void *userdata,wchar_t key1,wchar_t key2,EDITOR_ACTION action);

struct MY_TXT_EDITOR_API{
    void * userdata;
    void *screen_state;
    void (* save_file)(void *userdata);
    KEY_SET key;

};


void ST_INIT(const MY_TXT_EDITOR_API *api);

void ST_on_key(wchar_t ch,const MY_TXT_EDITOR_API *const api);



#endif