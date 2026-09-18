#include "error_log.h"
#include <stddef.h>
#include<stdio.h>
#include <string.h>
static FILE *file = NULL;

// set_error_log_file(): エラーログの出力先を初回だけ開く。
// 引数: file_path=作成または上書きするログファイルのパス。
// 返り値: なし。既に開いている場合またはfopen()失敗時は状態を変更しない。
// 所有権: 開いたFILEはこのモジュールが保持し、close_error_log_file()が閉じる。
void set_error_log_file(char *file_path){
        if(file != NULL){
                return;
        }

        file = fopen(file_path,"w");
        if(file == NULL){
                //開けなかったことを通知する
                return;
        }
}

// close_error_log_file(): このモジュールが保持するログファイルを閉じる。
// 引数: なし。
// 返り値: なし。ログファイルが未設定なら何もしない。
void close_error_log_file(){
        if(file != NULL)fclose(file);
}

// error_log_write(): 設定済みログへ最大ERROR_MSG_SIZE_MAXバイトを書き込む。
// 引数: error_comment=NUL終端された出力文字列。
// 返り値: なし。ログファイルが未設定なら何もしない。
void error_log_write(char *error_comment){
        if(file == NULL)return; 

        size_t len = strlen(error_comment);
        len = (len > ERROR_MSG_SIZE_MAX)?ERROR_MSG_SIZE_MAX:len;

        // 危険: LSP本文など長さが外部入力に依存する文字列と同じ大きさのVLAを作る。
        // 大きなログ1件だけでスタックオーバーフローする可能性がある。
        char error_log[len + 1];
        memmove(error_log, error_comment,sizeof(char ) * len);
        error_log[len] = '\0';
        fputs(error_log,file);
        return;
}
