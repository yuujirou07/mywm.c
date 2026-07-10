#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#include "language_server_communication.h"

#define LSP_INVALID_FD (-1)
#define LSP_HEADER_MAX 8192

static int lsp_write_all(int fd, const char *data, size_t len)
{
    size_t written = 0;

    while(written < len){
        ssize_t result = write(fd, data + written, len - written);
        if(result == -1){
            if(errno == EINTR){
                continue;
            }
            return -1;
        }
        if(result == 0){
            return -1;
        }
        written += (size_t)result;
    }

    return 0;
}

static int lsp_read_all(int fd, char *data, size_t len)
{
    size_t read_size = 0;

    while(read_size < len){
        ssize_t result = read(fd, data + read_size, len - read_size);
        if(result == -1){
            if(errno == EINTR){
                continue;
            }
            return -1;
        }
        if(result == 0){
            return -1;
        }
        read_size += (size_t)result;
    }

    return 0;
}

static int lsp_parse_content_length(const char *header)
{
    const char *line = header;

    while(*line != '\0'){
        const char *line_end = strstr(line, "\r\n");
        size_t line_len = line_end != NULL ? (size_t)(line_end - line) : strlen(line);

        if(line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0){
            const char *value = line + 15;
            char *end = NULL;
            long length;

            while(*value == ' ' || *value == '\t'){
                value++;
            }
            errno = 0;
            length = strtol(value, &end, 10);
            if(errno != 0 || end == value || length < 0 || length > INT_MAX){
                return -1;
            }
            return (int)length;
        }

        if(line_end == NULL){
            break;
        }
        line = line_end + 2;
    }

    return -1;
}

static int lsp_is_uri_safe(unsigned char ch)
{
    return isalnum(ch) || ch == '/' || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

void lsp_process_init(struct lsp_process *lsp)
{
    if(lsp == NULL){
        return;
    }

    lsp->pid = -1;
    lsp->to_server_fd = LSP_INVALID_FD;
    lsp->from_server_fd = LSP_INVALID_FD;
}

int lsp_start_server(struct lsp_process *lsp, const char *command, char *const argv[])
{
    int to_server[2] = {LSP_INVALID_FD, LSP_INVALID_FD};
    int from_server[2] = {LSP_INVALID_FD, LSP_INVALID_FD};
    char *default_argv[2];
    pid_t pid;

    if(lsp == NULL || command == NULL){
        return -1;
    }

    lsp_process_init(lsp);

    default_argv[0] = (char *)command;
    default_argv[1] = NULL;
    if(argv == NULL){
        argv = default_argv;
    }

    if(pipe(to_server) == -1){
        return -1;
    }
    if(pipe(from_server) == -1){
        close(to_server[0]);
        close(to_server[1]);
        return -1;
    }

    pid = fork();
    if(pid == -1){
        close(to_server[0]);
        close(to_server[1]);
        close(from_server[0]);
        close(from_server[1]);
        return -1;
    }

    if(pid == 0){
        int null_fd;

        if(dup2(to_server[0], STDIN_FILENO) == -1 ||
           dup2(from_server[1], STDOUT_FILENO) == -1){
            _exit(1);
        }

        null_fd = open("/dev/null", O_WRONLY);
        if(null_fd != -1){
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }

        close(to_server[0]);
        close(to_server[1]);
        close(from_server[0]);
        close(from_server[1]);

        execvp(command, argv);
        _exit(127);
    }

    close(to_server[0]);
    close(from_server[1]);

    lsp->pid = pid;
    lsp->to_server_fd = to_server[1];
    lsp->from_server_fd = from_server[0];

    return 0;
}

void lsp_close_server(struct lsp_process *lsp)
{
    if(lsp == NULL){
        return;
    }

    if(lsp->to_server_fd != LSP_INVALID_FD){
        close(lsp->to_server_fd);
        lsp->to_server_fd = LSP_INVALID_FD;
    }
    if(lsp->from_server_fd != LSP_INVALID_FD){
        close(lsp->from_server_fd);
        lsp->from_server_fd = LSP_INVALID_FD;
    }
    if(lsp->pid > 0){
        waitpid(lsp->pid, NULL, WNOHANG);
        lsp->pid = -1;
    }
}

int lsp_path_to_file_uri(char *uri, size_t uri_size, const char *path)
{
    static const char prefix[] = "file://";
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    if(uri == NULL || uri_size == 0 || path == NULL){
        return -1;
    }

    for(size_t i = 0; prefix[i] != '\0'; i++){
        if(pos + 1 >= uri_size){
            uri[0] = '\0';
            return -1;
        }
        uri[pos++] = prefix[i];
    }

    for(size_t i = 0; path[i] != '\0'; i++){
        unsigned char ch = (unsigned char)path[i];

        if(lsp_is_uri_safe(ch)){
            if(pos + 1 >= uri_size){
                uri[0] = '\0';
                return -1;
            }
            uri[pos++] = (char)ch;
        }
        else{
            if(pos + 3 >= uri_size){
                uri[0] = '\0';
                return -1;
            }
            uri[pos++] = '%';
            uri[pos++] = hex[ch >> 4];
            uri[pos++] = hex[ch & 0x0f];
        }
    }

    uri[pos] = '\0';
    return 0;
}

int lsp_send(int fd, const char *json)
{
    char header[128];
    size_t json_len;
    int header_len;

    if(json == NULL){
        return -1;
    }

    json_len = strlen(json);
    header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
    if(header_len < 0 || (size_t)header_len >= sizeof(header)){
        return -1;
    }

    if(lsp_write_all(fd, header, (size_t)header_len) == -1){
        return -1;
    }
    return lsp_write_all(fd, json, json_len);
}

int lsp_send_initialize(int fd, int id, pid_t process_id, const char *root_uri)
{
    const char *json_format =
        "{\"jsonrpc\":\"2.0\","
        "\"id\":%d,"
        "\"method\":\"initialize\","
        "\"params\":{"
            "\"processId\":%ld,"
            "\"rootUri\":\"%s\","
            "\"capabilities\":{}"
        "}}";
    int json_len;
    char *json;
    int result;

    if(root_uri == NULL){
        return -1;
    }

    json_len = snprintf(NULL, 0, json_format, id, (long)process_id, root_uri);
    if(json_len < 0){
        return -1;
    }

    json = malloc((size_t)json_len + 1);
    if(json == NULL){
        return -1;
    }

    snprintf(json, (size_t)json_len + 1, json_format, id, (long)process_id, root_uri);
    result = lsp_send(fd, json);
    free(json);

    return result;
}

char *lsp_read_message(int fd)
{
    char header[LSP_HEADER_MAX + 1];
    size_t header_len = 0;
    int content_length;
    char *json;

    while(header_len < LSP_HEADER_MAX){
        if(lsp_read_all(fd, &header[header_len], 1) == -1){
            return NULL;
        }
        header_len++;
        header[header_len] = '\0';

        if(header_len >= 4 && strcmp(&header[header_len - 4], "\r\n\r\n") == 0){
            break;
        }
    }

    if(header_len >= LSP_HEADER_MAX){
        return NULL;
    }

    content_length = lsp_parse_content_length(header);
    if(content_length < 0){
        return NULL;
    }

    json = malloc((size_t)content_length + 1);
    if(json == NULL){
        return NULL;
    }

    if(lsp_read_all(fd, json, (size_t)content_length) == -1){
        free(json);
        return NULL;
    }

    json[content_length] = '\0';
    return json;
}
