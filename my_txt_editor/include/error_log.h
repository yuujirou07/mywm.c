#ifndef ERROR_LOG_H
#define ERROR_LOG_H

#define ERROR_MSG_SIZE_MAX 1024 // 1件の整形済みエラーメッセージへ確保するバイト数。

void set_error_log_file(char *file_path);
void close_error_log_file();
void error_log_write(char *error_comment);

// 式名、ソースファイル、行番号を整形して現在のエラーログへ書き込む。
#define error_log(msg) char fin_msg[ERROR_MSG_SIZE_MAX];\
snprintf(fin_msg,sizeof(fin_msg),"ERROR:# %s FILE %s  LINE %d\n",#msg,__FILE__,__LINE__);\
error_log_write(fin_msg);\


#endif
