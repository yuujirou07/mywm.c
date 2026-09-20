#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cjson/cJSON.h"
#include "json_read.h"
#include "path_util.h"
#include "settings_screen.h"

// settings_items.jsonの"value"に書ける型名。並び順はsettinge_value_typeと対応する。
static const char *value_type_str_list[] =
    {
    "bool",
    "int",
    };

// cmb_value_str_to_enum(): JSON文字列の設定値型をsettinge_value_typeへ変換する。
// 引数: value_type_item=型名を保持するcJSON文字列項目。
// 返り値: 一致する型。未対応文字列ならVALUE_TYPE_UNKNOWN。
// 所有権: cJSON内の文字列を借用し、解放しない。
static settinge_value_type cmb_value_str_to_enum(cJSON *value_type_item){
    char *value_type_str = cJSON_GetStringValue(value_type_item);
    for(size_t i = 0;i < sizeof(value_type_str_list)/sizeof(value_type_str_list[0]);i++){
        if(strcmp(value_type_str,value_type_str_list[i]) == 0)return (settinge_value_type)i;
    }
    return VALUE_TYPE_UNKNOWN;
}

// load_settings_screen_items(): editor_settings/settings_items.jsonを読み込み、設定画面の項目配列へ追加する。
// 引数: settings_screen_data=項目配列と件数を保持する設定画面データ。
// 返り値: 成功時0、ファイル・JSON・必須項目・メモリ確保の失敗時-1。
// 所有権: nameとexplanationを複製し、追加成功後はsettings_screen_dataが所有する。
int load_settings_screen_items(settings_screen_data *settings_screen_data){
    const char *file_name = "editor_settings/settings_items.json";
    char exe_dir_path[PATH_MAX];
    const char *path = NULL;

    if(access(file_name,R_OK) == 0){
        path = file_name;
    }
    else if(editor_path_from_exe_dir(exe_dir_path,sizeof(exe_dir_path),file_name) != NULL &&
            access(exe_dir_path,R_OK) == 0){
        path = exe_dir_path;
    }
    else{
        return -1;
    }

    char *buf = read_file_all(path);
    if(buf == NULL)return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!cJSON_IsArray(root)){
        cJSON_Delete(root);
        return -1;
    }

    cJSON *json_item = NULL;
    cJSON_ArrayForEach(json_item,root){
        cJSON *name = cJSON_GetObjectItemCaseSensitive(json_item,"name");
        cJSON *key = cJSON_GetObjectItemCaseSensitive(json_item,"key");
        cJSON *explanation = cJSON_GetObjectItemCaseSensitive(json_item,"explanation");
        cJSON *value_type = cJSON_GetObjectItemCaseSensitive(json_item,"value");
        if(!cJSON_IsString(name) || !cJSON_IsString(key) ||
            !cJSON_IsString(explanation) || key->valuestring[0] == '\0' ||
            !cJSON_IsString(value_type) ||key->valuestring[1] != '\0'){
            cJSON_Delete(root);
            return -1;
        }



        settings_items_data item = {
            .name = strdup(name->valuestring),
            .key_code = (unsigned char)key->valuestring[0],
            .explanation = strdup(explanation->valuestring),
            .value_type = cmb_value_str_to_enum(value_type),
        };

        if(item.name == NULL || item.explanation == NULL ||
            add_settings_screen_item(settings_screen_data,item) < 0){
            free((char *)item.name);
            free((char *)item.explanation);
            cJSON_Delete(root);
            return -1;
        }
    }

    cJSON_Delete(root);
    return 0;
}
