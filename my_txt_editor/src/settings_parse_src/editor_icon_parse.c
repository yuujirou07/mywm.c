


#include <cjson/cJSON.h>
#include <dirent.h>
#include <stddef.h>
#include<stdio.h>
#include<sys/stat.h>
#include<string.h>
#include<stdlib.h>
#include"error_log.h"
#include"txt_editor.h"
#include"json_read.h"
#include "txt_editor_icon.h"


// clear_icon_lib(): 対応表を解放し、未読み込み状態へ戻す。
// 引数: icon_data_ptr=対象の対応表。
// 返り値: なし。
static void clear_icon_lib(icon_data *icon_data_ptr){
    free(icon_data_ptr->icon_lib);
    icon_data_ptr->icon_lib = NULL;
    icon_data_ptr->icon_lib_num = 0;
    icon_data_ptr->icon_lib_allocate_num = 0;
}

// load_icon_data(): アイコン設定JSONを読み込み、拡張子とアイコン文字の対応表を作る。
// JSONは配列で、各要素は{"file_ext":".h","icon_code":"H"}の形を取る。
// 引数: path=読み込むJSONのパス、icon_data_ptr=対応表の格納先。
// 返り値: 成功時0、引数不正・ファイル不正・JSON不正・メモリ確保失敗時-1。
// 所有権: icon_libをmallocで確保してicon_data_ptrが保持する。
//         既に読み込み済みの対応表があれば捨てて作り直す。
//         失敗時は対応表を解放し、件数も0へ戻して未読み込み状態にする。
int load_icon_data(char *path,icon_data *icon_data_ptr){
    if(path == NULL)return -1;
    struct stat icon_stat;
    if(stat(path,&icon_stat) != 0){
        error_log("can not read icon file");
        return -1;
    }
    if(!S_ISREG(icon_stat.st_mode)){
        error_log("can not read file");
        return -1;
    }

    // 拡張子はパス末尾のドット以降だけを見る。
    // strstrで探すとディレクトリ名の途中の".json"を拾ってしまう。
    const char *ext_ptr = strrchr(path,'.');
    if(ext_ptr == NULL || strcmp(ext_ptr,SETTINGS_FILE_EXT) != 0){
        error_log("this file extension is not support");
        return -1;
    }

    char *json_buff = read_file_all(path);
    if(json_buff == NULL){
        error_log("can not read file");
        return -1;
    }

    cJSON *json_data = cJSON_Parse(json_buff);
    free(json_buff);
    if(json_data == NULL){
        error_log("icon json parse error");
        return -1;
    }
    if(!cJSON_IsArray(json_data)){
        cJSON_Delete(json_data);
        error_log("icon json is not array");
        return -1;
    }

    // 再読み込みに備えて前回の対応表を捨て、空の状態から作り直す。
    clear_icon_lib(icon_data_ptr);

    icon_data_ptr->icon_lib_allocate_num = 16;
    icon_data_ptr->icon_lib =
        malloc(sizeof(ext_icon) * icon_data_ptr->icon_lib_allocate_num);
    if(icon_data_ptr->icon_lib == NULL){
        icon_data_ptr->icon_lib_allocate_num = 0;
        cJSON_Delete(json_data);
        error_log("icon lib malloc error");
        return -1;
    }

    cJSON *data = NULL;
    cJSON_ArrayForEach(data,json_data){
        cJSON *ext_item = cJSON_GetObjectItemCaseSensitive(data,"file_ext");
        cJSON *icon_item = cJSON_GetObjectItemCaseSensitive(data,"ext_code");
        if(!cJSON_IsString(ext_item) || !cJSON_IsString(icon_item)){
            clear_icon_lib(icon_data_ptr);
            cJSON_Delete(json_data);
            error_log("icon json item error");
            return -1;
        }
        if(strlen(ext_item->valuestring) >= ICON_FILE_EXT_SIZE ||
            strlen(icon_item->valuestring) >= ICON_CODE_MAX_SIZE){
            clear_icon_lib(icon_data_ptr);
            cJSON_Delete(json_data);
            error_log("icon json item is so long");
            return -1;
        }

        //確保済みの件数を超えたら倍へ広げる。
        if(icon_data_ptr->icon_lib_num >= icon_data_ptr->icon_lib_allocate_num){
            int new_allocate_num = icon_data_ptr->icon_lib_allocate_num * 2;
            ext_icon *tmp_icon_lib = realloc(icon_data_ptr->icon_lib,
                sizeof(ext_icon) * new_allocate_num);
            if(tmp_icon_lib == NULL){
                clear_icon_lib(icon_data_ptr);
                cJSON_Delete(json_data);
                error_log("icon lib realloc error");
                return -1;
            }
            icon_data_ptr->icon_lib = tmp_icon_lib;
            icon_data_ptr->icon_lib_allocate_num = new_allocate_num;
        }

        ext_icon *lib =
            &icon_data_ptr->icon_lib[icon_data_ptr->icon_lib_num];
        //strlen+1で終端の'\0'までコピーする。
        memcpy(lib->file_ext,ext_item->valuestring,
            strlen(ext_item->valuestring) + 1);
        memcpy(lib->icon_code,icon_item->valuestring,
            strlen(icon_item->valuestring) + 1);
        icon_data_ptr->icon_lib_num++;
    }

    cJSON_Delete(json_data);
    return 0;
}


const char *get_file_ext_code(char *file_ext,icon_data *icon_data_ptr){

    struct stat state;
    stat(file_ext,&state);
    if(S_ISDIR(state.st_mode))return "\U0001F4C2";
    
    const char *return_code = "\u2753";
    for(int i = 0;i < icon_data_ptr->icon_lib_num;i++){
        char *ext_dot_ptr = strrchr(file_ext,'.');
        if(ext_dot_ptr == NULL){
            return return_code;
        }
        ext_icon *data = &icon_data_ptr->icon_lib[i];
        if(strcmp(ext_dot_ptr,data->file_ext) == 0){
            return data->icon_code;
        }
    }
    return return_code;
}

int destroy_icon_data(icon_data *icon_data_ptr){
    if(icon_data_ptr == NULL || icon_data_ptr->icon_lib_allocate_num <= 0)return -1;
    free(icon_data_ptr->icon_lib);   
    return 0;
}

