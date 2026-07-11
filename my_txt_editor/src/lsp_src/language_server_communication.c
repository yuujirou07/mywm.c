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
#include<cjson/cJSON.h>
#include "lsp_src/language_server_communication.h"

#define LSP_INVALID_FD (-1)
#define LSP_HEADER_MAX 8192

/*
 * 指定した長さのデータをfdへすべて書き込む。
 * write()がシグナルで中断された場合は再試行する。
 *
 * 引数:
 *   fd   : 書き込み先のファイルディスクリプタ。
 *   data : 送信するデータの先頭アドレス。
 *   len  : 送信するデータのバイト数。
 *
 * 返り値:
 *   0  : lenバイトすべてを送信できた。
 *   -1 : 書き込み失敗、または接続先が閉じられた。
 */
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

/*
 * 指定した長さのデータをfdからすべて読み込む。
 * read()がシグナルで中断された場合は再試行する。
 *
 * 引数:
 *   fd   : 読み込み元のファイルディスクリプタ。
 *   data : 受信データの格納先。
 *   len  : 読み込むデータのバイト数。
 *
 * 返り値:
 *   0  : lenバイトすべてを読み込めた。
 *   -1 : 読み込み失敗、または接続先が閉じられた。
 */
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

/*
 * LSPメッセージのヘッダからContent-Lengthの値を取り出す。
 *
 * 引数:
 *   header : \r\n\r\nで終わるLSPメッセージヘッダ文字列。
 *
 * 返り値:
 *   0以上 : JSON本文のバイト数。0は空の本文を表す。
 *   -1     : Content-Lengthが無い、または値が不正。
 */
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

/*
 * file URIにそのまま含められる文字かを判定する。
 *
 * 引数:
 *   ch : 判定する1バイト文字。
 *
 * 返り値:
 *   0以外 : パーセントエンコードせずURIにコピーできる。
 *   0     : パーセントエンコードが必要。
 */
static int lsp_is_uri_safe(unsigned char ch)
{
    return isalnum(ch) || ch == '/' || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

/*
 * LSPプロセス管理構造体を未接続状態へ初期化する。
 *
 * 引数:
 *   lsp : 初期化するLSPプロセス管理構造体。NULLは何もしない。
 *
 * 返り値:
 *   なし。
 */
void lsp_process_init(struct lsp_process *lsp)
{
    if(lsp == NULL){
        return;
    }

    lsp->pid = -1;
    lsp->to_server_fd = LSP_INVALID_FD;
    lsp->from_server_fd = LSP_INVALID_FD;
    lsp->initialized = false;
    lsp->update_data.file_update_counter = 0;
    lsp->update_data.path_name[0] = '\0';
    lsp->lsp_language_id[0] = '\0';
}

/*
 * Language Serverを子プロセスとして起動し、標準入出力と親プロセスの
 * pipeを接続する。成功後はto_server_fdへ送信し、from_server_fdから受信する。
 *
 * 引数:
 *   lsp     : 起動結果のpidとpipeのfdを格納する構造体。
 *   command : execvp()で実行するLanguage Serverのコマンド名またはパス。
 *   argv    : commandへ渡すNULL終端の引数配列。NULLならcommandだけを渡す。
 *
 * 返り値:
 *   0  : 子プロセスの起動とpipeの接続に成功した。
 *   -1 : 引数不正、pipe()、またはfork()に失敗した。
 */
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

/*
 * LSPとの通信fdを閉じ、子プロセスを回収できていれば管理構造体を未接続状態に戻す。
 *
 * 引数:
 *   lsp : 閉じるLSPプロセス管理構造体。NULLは何もしない。
 *
 * 返り値:
 *   なし。
 */
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

/*
 * ローカルファイルパスをLSPで使用するfile URIへ変換する。
 * URIに直接使えない文字はパーセントエンコードする。
 *
 * 引数:
 *   uri      : 変換したURIの出力先バッファ。
 *   uri_size : uriのバイト数。終端NULの領域を含める。
 *   path     : 変換元のNUL終端ファイルパス。
 *
 * 返り値:
 *   0  : 変換に成功した。
 *   -1 : 引数不正、またはuriのバッファ容量不足。uriは空文字列にする。
 */
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

/*
 * JSON-RPC本文にContent-Lengthヘッダを付け、LSPへ1メッセージ送信する。
 *
 * 引数:
 *   fd   : LSPの標準入力へ接続された書き込み用ファイルディスクリプタ。
 *   json : 送信するNUL終端JSON-RPC本文。ヘッダは含めない。
 *
 * 返り値:
 *   0  : ヘッダとJSON本文をすべて送信できた。
 *   -1 : 引数不正、ヘッダ作成失敗、または書き込み失敗。
 */
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

/*
 * LSPの初期化要求initializeをJSON-RPCメッセージとして送信する。
 *
 * 引数:
 *   fd         : LSPの標準入力へ接続された書き込み用ファイルディスクリプタ。
 *   id         : この要求を識別するJSON-RPCのid。応答のidと対応する。
 *   process_id : エディタプロセスのpid。LSPへ親プロセスとして通知する。
 *   root_uri   : プロジェクトルートを表すfile URI。
 *
 * 返り値:
 *   0  : initialize要求を送信できた。
 *   -1 : 引数不正、メモリ確保、JSON作成、または送信に失敗した。
 */
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

/*
 * LSPからContent-Lengthヘッダ付きのJSON-RPCメッセージを1件受信する。
 * 戻り値の文字列は呼び出し側がfree()する。
 *
 * 引数:
 *   fd : LSPの標準出力へ接続された読み込み用ファイルディスクリプタ。
 *
 * 返り値:
 *   NULL以外 : NUL終端されたJSON本文。呼び出し側がfree()する。
 *   NULL     : 受信、ヘッダ解析、またはメモリ確保に失敗した。
 */
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

void initialize_id(struct lsp_send_receve_id_data *id_data){
    int size = sizeof(id_data->used_id_history);
    int arry_size = size/sizeof(int);
    for(int i = 0; i < arry_size ;i++){
        id_data->used_id_history[i] = i + 1;
    }
    id_data->id_strage_counter = 0;
}

int check_id(char *msg){
    cJSON *root = cJSON_Parse(msg);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
 
    int int_id = NONE;
    if(cJSON_IsNumber(id) && id->valueint >= 0 ){
        int_id = id->valueint;
    }
    return int_id;
}

void set_lsp_use_language(struct lsp_process *lsp,char *language){
    if(language == NULL)return;
    
    int language_name_size = sizeof(lsp->lsp_language_id);
    int language_name = strlen(language);

    if(language_name_size < language_name){
        return;
    }

    snprintf(lsp->lsp_language_id, sizeof(lsp->lsp_language_id),
         "%s", language);
}

int lsp_send_did_open(int fd, const char *uri,
                      const char *language_id, const char *text)
{
        
    int result = -1;
    cJSON *root = cJSON_CreateObject();
    cJSON *params;
    cJSON *document;
    char *json;

    if(root == NULL || uri == NULL || language_id == NULL || text == NULL){
        cJSON_Delete(root);
        return -1;
    }

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "textDocument/didOpen");

    params = cJSON_AddObjectToObject(root, "params");
    document = cJSON_AddObjectToObject(params, "textDocument");
    cJSON_AddStringToObject(document, "uri", uri);
    cJSON_AddStringToObject(document, "languageId", language_id);
    cJSON_AddNumberToObject(document, "version", 1);
    cJSON_AddStringToObject(document, "text", text);

    json = cJSON_PrintUnformatted(root);
    if(json != NULL){
        result = lsp_send(fd, json);
        free(json);
    }

    cJSON_Delete(root);
    return result;
}


int lsp_is_publish_diagnostics(const char *msg)
{
    int result = 0;
    cJSON *root = cJSON_Parse(msg);
    if(root == NULL){
        return 0;
    }

    cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if(cJSON_IsString(method) &&
       strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0){
        result = 1;
    }

    cJSON_Delete(root);
    return result;
}


int lsp_send_did_change(int fd, const char *uri, int version, const char *text)
{
    int result = -1;
    cJSON *root;
    cJSON *params;
    cJSON *document;
    cJSON *changes;
    cJSON *change;
    char *json;

    if(uri == NULL || text == NULL || version < 1){
        return -1;
    }

    root = cJSON_CreateObject();
    if(root == NULL){
        return -1;
    }

    if(cJSON_AddStringToObject(root, "jsonrpc", "2.0") == NULL ||
       cJSON_AddStringToObject(root, "method", "textDocument/didChange") == NULL){
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_AddObjectToObject(root, "params");
    if(params == NULL){
        cJSON_Delete(root);
        return -1;
    }

    document = cJSON_AddObjectToObject(params, "textDocument");
    if(document == NULL ||
       cJSON_AddStringToObject(document, "uri", uri) == NULL ||
       cJSON_AddNumberToObject(document, "version", version) == NULL){
        cJSON_Delete(root);
        return -1;
    }

    changes = cJSON_AddArrayToObject(params, "contentChanges");
    change = cJSON_CreateObject();
    if(changes == NULL || change == NULL){
        cJSON_Delete(change);
        cJSON_Delete(root);
        return -1;
    }
    if(!cJSON_AddItemToArray(changes, change)){
        cJSON_Delete(change);
        cJSON_Delete(root);
        return -1;
    }
    if(cJSON_AddStringToObject(change, "text", text) == NULL){
        cJSON_Delete(root);
        return -1;
    }

    json = cJSON_PrintUnformatted(root);
    if(json != NULL){
        result = lsp_send(fd, json);
        free(json);
    }

    cJSON_Delete(root);
    return result;
}
