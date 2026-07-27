#include<stdio.h>
#include <string.h>
static FILE *file = NULL;

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

void close_error_log_file(){
        if(file != NULL){
                fclose(file);
        }

}


void error_log_write(char *error_comment){
        if(file == NULL){
               return; 
        }
        int len = strlen(error_comment);
        // 危険: LSP本文など長さが外部入力に依存する文字列と同じ大きさのVLAを作る。
        // 大きなログ1件だけでスタックオーバーフローする可能性がある。
        char error_log[len + 1];
        memmove(error_log, error_comment,sizeof(char ) * len);
        error_log[len] = '\0';
        fputs(error_log,file);
        return;
}
