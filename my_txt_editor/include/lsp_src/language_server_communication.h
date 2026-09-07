#ifndef LANGUAGE_SERVER_COMMUNICATION_H
#define LANGUAGE_SERVER_COMMUNICATION_H

#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define MSG_BUFF_SIZE_MAX 2048
#define MAX_ID_STRAGE_SIZE 512
#define initialize_id_num 1
#define NONE -1

struct editor_state;

typedef enum{
    lsp_method_completion,
    lsp_method_none,
}lsp_method;

typedef struct{
    int line;
    int character;
}lsp_pos;

typedef struct{
    int id;
    lsp_method lsp_method;
    char *uri;
    lsp_pos pos;
}lsp_send_msg_data;

struct lsp_send_receve_id_data{
    int used_id_history[MAX_ID_STRAGE_SIZE];
    int id_storage_counter;
};

struct txt_update_data{
    char path_name[PATH_MAX];
    int file_update_counter;
};

struct lsp_process {
    pid_t pid;
    int to_server_fd;
    int from_server_fd;
    bool initialized;
    struct txt_update_data update_data;
    struct lsp_send_receve_id_data id_data;
    char from_server_msg_buff[MSG_BUFF_SIZE_MAX];
    char to_server_msg_buff[MSG_BUFF_SIZE_MAX];
    char lsp_language_id[32];
};



void lsp_process_init(struct lsp_process *lsp);
int lsp_start_server(struct lsp_process *lsp, const char *command, char *const argv[]);
void lsp_close_server(struct lsp_process *lsp);
int lsp_path_to_file_uri(char *uri, size_t uri_size, const char *path);
int lsp_send(int fd, const char *json);
int lsp_send_initialize(int fd, int id, pid_t process_id, const char *root_uri);
char *lsp_read_message(int fd);
void lsp_handle_message(struct lsp_process *lsp, char *msg);
void initialize_id(struct lsp_send_receve_id_data *id_data);
void set_lsp_use_language(struct lsp_process *lsp,char *language);
int lsp_send_did_open(int fd, const char *uri,
                      const char *language_id, const char *text);
int lsp_send_did_change(int fd, const char *uri,
                        int version, const char *text);

int lsp_make_msg(lsp_send_msg_data msg_data, char **msg);
int lsp_send_completion(int lsp_fd,struct editor_state *state);

#endif
