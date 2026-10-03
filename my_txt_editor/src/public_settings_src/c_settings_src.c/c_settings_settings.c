

#include <dlfcn.h>
#include <limits.h>
#include<stdio.h>
#include<stdlib.h>
#include <sys/stat.h>
#include <wchar.h>


#include "c_settings.h"
#include "error_log.h"
#include "path_util.h"
#include "public_data/c_settings/c_settings_setting.h"
#include"txt_editor.h"

static void api_save_file(void *userdata);

static void api_key_mapping(void *userdata,wchar_t key1,wchar_t key2,EDITOR_ACTION action);
void connect_api_mem_data(MY_TXT_EDITOR_API *const api,struct editor_input_context *ctx){
    api->userdata = ctx; 
    api->save_file = api_save_file;
    api->key = api_key_mapping;
    return;
}

static void api_save_file(void *userdata){
    struct editor_input_context *tmp_ctx = userdata;
    save_file((struct editor_state *)tmp_ctx->state);
}



static void api_key_mapping(void *userdata,wchar_t key1,wchar_t key2,EDITOR_ACTION action){
    struct editor_input_context *ctx = (struct editor_input_context *)userdata;
    //メモリの拡張処理
    if(ctx->key_mapp_list.key_map_allocate_num <= ctx->key_mapp_list.key_map_num){
        settings_key_mapps *tmp_key_mapps = 
            realloc(ctx->key_mapp_list.key_mapp_list,
                sizeof(settings_key_mapps) * (ctx->key_mapp_list.key_map_num * 2)); 
            if(tmp_key_mapps == NULL){
                error_log(can not realloc);
                return;
            }
            ctx->key_mapp_list.key_mapp_list = tmp_key_mapps;
            ctx->key_mapp_list.key_map_allocate_num = ctx->key_mapp_list.key_map_num * 2;
    }
    ctx->key_mapp_list.key_mapp_list[ctx->key_mapp_list.key_map_num].ch[0] = key1;
    ctx->key_mapp_list.key_mapp_list[ctx->key_mapp_list.key_map_num].ch[1] = key2;
    ctx->key_mapp_list.key_mapp_list[ctx->key_mapp_list.key_map_num].action_func = action;
    ctx->key_mapp_list.key_map_num++;
    return;
}


void init_settings_src(MY_TXT_EDITOR_API *const api,struct editor_input_context *ctx){
    connect_api_mem_data(api,ctx);
    ST_INIT(api);
}

void check_key_mapps_entry(struct editor_input_context *ctx,wchar_t chr[2],MY_TXT_EDITOR_API *api){
    for(int i = 0;i < ctx->key_mapp_list.key_map_num;i++){
        if(ctx->key_mapp_list.key_mapp_list[i].ch[0] == chr[0] &&
            ctx->key_mapp_list.key_mapp_list[i].ch[1] == chr[1]){

            ctx->key_mapp_list.key_mapp_list[i].action_func(api);
        }
    }
    return;
}

void load_current_settings_obj_c_file(struct editor_input_context *ctx){
    (void)ctx;
    char obj_path[PATH_MAX];
    if(editor_path_from_exe_dir(obj_path, sizeof(obj_path), "editor_settings/C_build/main") == NULL){
        error_log_write("can not found settings obj file\n");
        return;
    }

    struct stat st;
    if(stat(obj_path, &st) != 0 || !S_ISREG(st.st_mode)){
        error_log_write("can not found settings obj file\n");
        return;
    }

    ctx->dl_data.now_loading_dynamic_lib[ctx->dl_data.now_loading_lib_num] = 
        dlopen(obj_path,RTLD_LAZY | RTLD_GLOBAL);
    
    return;
}
