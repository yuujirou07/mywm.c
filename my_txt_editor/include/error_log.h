#ifndef ERROR_LOG_H
#define ERROR_LOG_H

void set_error_log_file(char *file_path);
void close_error_log_file();
void error_log_write(char *error_comment);



#endif