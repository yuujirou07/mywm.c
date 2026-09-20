#ifndef TXT_EDITOR_ICON_H
#define TXT_EDITOR_ICON_H

#define ICON_CODE_MAX_SIZE 32
#define ICON_FILE_EXT_SIZE 64


#define UnknownName L"\u1783"

// 拡張子1つとアイコン1文字の対応。
typedef struct{
    char icon_code[ICON_CODE_MAX_SIZE];
    char file_ext[ICON_FILE_EXT_SIZE];
}ext_icon;




typedef struct{
   ext_icon *icon_lib;
   int icon_lib_num;
   int icon_lib_allocate_num;
}icon_data;

int load_icon_data(char *path,icon_data *icon_data_ptr);
const char *get_file_ext_code(char *file_ext,icon_data *icon_data_ptr);

int destroy_icon_data(icon_data *icon_data_ptr);
#endif