#ifndef LANGUAGE_SERVER_COMMUNICATION_H
#define LANGUAGE_SERVER_COMMUNICATION_H

#include <stddef.h>
#include <sys/types.h>

struct lsp_process {
    pid_t pid;
    int to_server_fd;
    int from_server_fd;
};

void lsp_process_init(struct lsp_process *lsp);
int lsp_start_server(struct lsp_process *lsp, const char *command, char *const argv[]);
void lsp_close_server(struct lsp_process *lsp);
int lsp_path_to_file_uri(char *uri, size_t uri_size, const char *path);
int lsp_send(int fd, const char *json);
int lsp_send_initialize(int fd, int id, pid_t process_id, const char *root_uri);
char *lsp_read_message(int fd);


#endif
