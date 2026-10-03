

#include"public_data/c_settings/c_settings_setting.h"
#include <stdlib.h>
#include <wchar.h>


static void func(const MY_TXT_EDITOR_API *api){
    int oo = api - api;
    exit(oo);
}


void ST_INIT(const MY_TXT_EDITOR_API *api){
    api->key(api->userdata,L'o',L'o',func);
}