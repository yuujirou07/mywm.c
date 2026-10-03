#ifndef TXT_EDITOR_ICON_H
#define TXT_EDITOR_ICON_H

#define ICON_CODE_MAX_SIZE 32 // アイコン文字列へ確保する最大バイト数。
#define ICON_FILE_EXT_SIZE 64 // 拡張子文字列へ確保する最大バイト数。


#define UnknownName L"\u1783" // 対応する拡張子が無い場合に表示する既定アイコン。

// 拡張子1つとアイコン1文字の対応。
typedef struct{
    char icon_code[ICON_CODE_MAX_SIZE]; // UTF-8のNUL終端アイコン文字列。
    char file_ext[ICON_FILE_EXT_SIZE]; // 先頭のピリオドを含む拡張子のNUL終端文字列。
}ext_icon;

// 設定JSONから読み込んだ拡張子別アイコンの動的配列。
typedef struct{
   ext_icon *icon_lib; // この構造体が所有し、destroy_icon_data()で解放する配列。
   int icon_lib_num;   // 配列に格納済みの有効要素数。
   int icon_lib_allocate_num; // 配列へ確保済みの要素数。
}icon_data;

int load_icon_data(char *path,icon_data *icon_data_ptr);
const char *get_file_ext_code(char *file_ext,icon_data *icon_data_ptr);

int destroy_icon_data(icon_data *icon_data_ptr);
#endif
