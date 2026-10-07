

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
static void *load_current_settings_obj_c_file(struct editor_input_context *ctx);

static void api_key_mapping(void *userdata,wchar_t key1,wchar_t key2,EDITOR_ACTION action);
// apiへ借用したctxと、保存・キーマップ登録用の関数ポインタを接続する。
// 返り値: なし。apiとctxは非NULLで、API利用中はctxを有効に保つ。
void connect_api_mem_data(MY_TXT_EDITOR_API *const api,struct editor_input_context *ctx){
    api->userdata  = ctx; 
    api->save_file = api_save_file;
    api->key = api_key_mapping;

    return;
}

// userdataをeditor_input_contextとして扱い、そのstateの本文を保存する。
// 返り値: なし。userdataは有効なctxであること。保存結果の画面遷移はsave_fileに従う。
static void api_save_file(void *userdata){
    struct editor_input_context *tmp_ctx = userdata;
    save_file((struct editor_state *)tmp_ctx->state);
}



// userdataのctxへkey1・key2の組とactionを登録する。キーマップ配列は事前に確保しておく。
// 返り値: なし。上限到達・再確保失敗はログを残して追加しない。actionは呼出し可能な関数を渡す。
static void api_key_mapping(void *userdata,wchar_t key1,wchar_t key2,EDITOR_ACTION action){
    struct editor_input_context *ctx = (struct editor_input_context *)userdata;

    //キーマップ最大登録数ガード
    if(ctx->key_mapp_list.key_map_num >= KEY_MAPPING_MAX_NUM){
        error_log("over flow key map tabele");
        return;
    }

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


// apiへctxを接続し、実行ファイル隣のso_file/settings.soからST_INITを呼んで設定を登録する。
// 返り値: なし。読込失敗は登録せず戻り、成功したハンドルはctxが保持して終了時にdlcloseする。
void init_settings_src(MY_TXT_EDITOR_API *const api,struct editor_input_context *ctx){
    connect_api_mem_data(api,ctx);
    if(ctx->key_mapp_list.key_mapp_list == NULL){
        return;
    }
    void *handle = load_current_settings_obj_c_file(ctx);
    if(handle == NULL){
        return;
    }
    dlerror();
    void (*settings_init)(const MY_TXT_EDITOR_API *) = dlsym(handle,"ST_INIT");
    char *error = dlerror();
    if(error != NULL || settings_init == NULL){
        error_log_write("can not find ST_INIT\n");
        dlclose(handle);
        return;
    }
    ctx->dl_data.now_loading_dynamic_lib[ctx->dl_data.now_loading_lib_num++] = handle;
    settings_init(api);
}

// ctxの登録一覧でchrの2文字と一致する全項目を探し、それぞれの処理へapiを渡して実行する。
// 返り値: なし。chrは2要素以上で、登録関数・ctx・apiは有効であること。不一致は何もしない。
void check_key_mapps_entry(struct editor_input_context *ctx,wchar_t chr[2],MY_TXT_EDITOR_API *api){
    for(int i = 0;i < ctx->key_mapp_list.key_map_num;i++){
        if(ctx->key_mapp_list.key_mapp_list[i].ch[0] == chr[0] &&
            ctx->key_mapp_list.key_mapp_list[i].ch[1] == chr[1]){

            ctx->key_mapp_list.key_mapp_list[i].action_func(api);
        }
    }
    return;
}

// ctxのハンドル格納余地を確認し、実行ファイル隣のso_file/settings.soをdlopenする。
// 返り値: 読込ハンドル、格納余地不足・パス不正・読込失敗はNULL。成功したハンドルは呼び出し側がdlcloseする。
static void *load_current_settings_obj_c_file(struct editor_input_context *ctx){
    if(ctx->dl_data.now_loading_dynamic_lib == NULL ||
        ctx->dl_data.now_loading_lib_num >= ctx->dl_data.now_loading_lib_allocate_num){
        error_log_write("can not store settings library\n");
        return NULL;
    }
    char obj_path[PATH_MAX];
    if(editor_path_from_exe_dir(obj_path, sizeof(obj_path), "so_file/settings.so") == NULL){
        error_log_write("can not found settings obj file\n");
        return NULL;
    }

    struct stat st;
    if(stat(obj_path, &st) != 0 || !S_ISREG(st.st_mode)){
        error_log_write("can not found settings obj file\n");
        return NULL;
    }

    void *handle = dlopen(obj_path,RTLD_NOW | RTLD_LOCAL);
    if(handle == NULL){
        error_log_write("can not load settings library\n");
    }
    return handle;
}



// ctxのキーマップ件数を参照するが、現実装ではイベント処理を行わない。
// 返り値: 常に0。ctxは非NULLとする。
int check_user_settings_event(struct editor_input_context *ctx){
    if(ctx->key_mapp_list.key_map_num > 0){

    }

    return 0;
}


// key_mapp_dataの先頭キーを0へ書き換える。現実装では登録アクションを呼ばない。
// 返り値: 常に0。key_mapp_dataは非NULLとする。
int call_user_key_mapping_event(settings_key_mapps *key_mapp_data){
    *key_mapp_data->ch = 0;
    return 0;
}