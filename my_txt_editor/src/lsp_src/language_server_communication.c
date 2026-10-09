#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#include<cjson/cJSON.h>
#include "lsp_src/language_server_communication.h"
#include "txt_editor.h"
#include "error_log.h"

#define LSP_INVALID_FD (-1) // 言語サーバーとのパイプが未接続であることを表す値。
#define LSP_HEADER_MAX 8192 // 受信するLSPヘッダーの最大バイト数。
#define LSP_CONTENT_MAX ((size_t)32 * 1024 * 1024) // fdへdataのlenバイトを全て書く。短い書込みは続行し、EINTRは再試行する。
// 返り値: 完了0、書込失敗・0バイト書込みは-1。blocking fdでは完了まで待機する。
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

// fdからlenバイトをdataへ読む。短い読込みは続行し、EINTRは再試行する。
// 返り値: 完了0、読込失敗・途中EOFは-1。成功時はdata[len]に'\0'を書くため、len+1バイトの領域が必要。
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
    data[len] = '\0';
    return 0;
}

// NUL終端のheaderからContent-Lengthを探し、上限32MiB以内の本文バイト数を返す。
// 返り値: 未検出・数値変換失敗・負数・上限超過はSIZE_MAX。現実装では数値後の余分な文字を検査しない。
static size_t lsp_parse_content_length(const char *header)
{
    const char *line = header;

    while(*line != '\0'){
        const char *line_end = strstr(line, "\r\n");
        size_t line_len = line_end != NULL ? (size_t)(line_end - line) : strlen(line);

        if(line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0){
            const char *value = line + 15;
            char *end = NULL;
            unsigned long long length;

            while(*value == ' ' || *value == '\t'){
                value++;
            }
            errno = 0;
            length = strtoull(value, &end, 10);
            if(errno != 0 || end == value || *value == '-' ||
               length > (unsigned long long)LSP_CONTENT_MAX){
                return SIZE_MAX;
            }
            return (size_t)length;
        }

        if(line_end == NULL){
            break;
        }
        line = line_end + 2;
    }

    return SIZE_MAX;
}

// 1バイトのchが英数字または/・-・.・_・~なら、URIへ直接コピー可能と判定する。
// 返り値: 直接使用可能なら非0、パーセントエンコードが必要なら0。英数字判定はロケールに従う。
static int lsp_is_uri_safe(unsigned char ch)
{
    return isalnum(ch) || ch == '/' || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

// lspのpid・通信fd・初期化状態・文書情報を未接続の値にする。既存の接続は閉じない。
// 返り値: なし。NULLは何もしない。接続済みの構造体は事前に終了処理を行う。
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

// commandをargv（NULLならcommandだけ）でfork/execし、pidと標準入出力のパイプを未接続のlspへ保存する。
// 返り値: 親側の接続成功0、引数・pipe・fork失敗-1。0でも子のexec成功は保証せず、終了時はlsp_close_serverを呼ぶ。
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

// lspの通信fdを閉じ、waitpid(WNOHANG)で子を一度だけ回収確認してpidを-1にする。
// 返り値: なし。NULLは何もしない。終了待ち・強制終了はせず、未終了の子でもpidを破棄する。
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
        // 危険: WNOHANGで未終了だった場合も直後にpidを捨てる。
        // 後からwait/killできず、子プロセスやゾンビを回収できなくなる可能性がある。
        waitpid(lsp->pid, NULL, WNOHANG);
        lsp->pid = -1;
    }
}

// NUL終端のpathにfile://を付けてエンコードし、uri_sizeバイトのuriへ格納する。絶対パス化はしない。
// 返り値: 成功0、引数不正・容量不足-1。容量不足ではuriを空にし、引数不正時は書き換えない。
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

// NUL終端のjsonにContent-Lengthヘッダーを付けて、LSPの書込fdへ全バイト送信する。
// 返り値: 成功0、NULL・ヘッダー作成・送信失敗-1。jsonの所有権は移動しない。
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

// fdへ要求ID=id、エディタPID=process_id、ルートURI=root_uriのinitialize要求を送る。
// 返り値: 成功0、引数・JSON生成・送信失敗-1。root_uriはJSON内へそのまま埋め込めるエンコード済みURIとする。
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

// fdからContent-Length付きメッセージを1件読み、NUL終端のJSON本文を確保する。
// 返り値: 呼び出し側がfreeする本文、受信・解析・確保失敗はNULL。blocking fdでは全本文を受け取るまで待つ。
char *lsp_read_message(int fd){

    char header[LSP_HEADER_MAX + 1];
    size_t header_len = 0;
    size_t content_length;
    char *json;

    // 危険: epollが保証するのは「1バイト以上読める」ことだけで、完全なメッセージではない。
    // blocking fdでヘッダや本文の残りを待つため、分割受信するとUI全体が停止する。
    while(header_len < LSP_HEADER_MAX){
        if(lsp_read_all(fd, &header[header_len], 1) == -1){
            return NULL;
        }
        header_len++;

        if(header_len >= 4 && strcmp(&header[header_len - 4], "\r\n\r\n") == 0){
            break;
        }
    }

    if(header_len >= LSP_HEADER_MAX){
        return NULL;
    }

    content_length = lsp_parse_content_length(header);
    if(content_length == SIZE_MAX){
        return NULL;
    }

    json = malloc(content_length + 1);
    if(json == NULL){
        return NULL;
    }

    if(lsp_read_all(fd, json, content_length) == -1){
        free(json);
        return NULL;
    }

    return json;
}

// 受信済みのLSPメッセージ1件を種類別に振り分ける。initialize応答にはinitialized通知を返し、診断通知はエラーログへ書き出す。
// 引数: lsp=送信先fdとinitialized状態を持つLSPプロセス、msg='\0'終端のJSON文字列。 返り値: なし。
void lsp_handle_message(struct lsp_process *lsp, char *msg){
    cJSON *root;
    cJSON *jsonrpc;
    cJSON *method;
    cJSON *id;
    cJSON *result;
    cJSON *error;

    if(lsp == NULL || msg == NULL){
        return;
    }

    root = cJSON_Parse(msg);
    if(root == NULL){
        return;
    }

    jsonrpc = cJSON_GetObjectItemCaseSensitive(root, "jsonrpc");
    method = cJSON_GetObjectItemCaseSensitive(root, "method");
    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    error = cJSON_GetObjectItemCaseSensitive(root, "error");

    if(!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0){
        cJSON_Delete(root);
        return;
    }

    if(cJSON_IsString(method)){
        if(cJSON_IsNumber(id)){
            error_log_write("unsupported LSP request\n");
        }
        else if(strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0){
            error_log_write(msg);
            error_log_write("\n");
        }
    }
    else if(cJSON_IsNumber(id) && id->valueint == initialize_id_num && !lsp->initialized){
        if(error != NULL){
            error_log_write(msg);
            error_log_write("\n");
        }
        else if(result != NULL && lsp_send(lsp->to_server_fd,
            "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}") == 0){
            lsp->initialized = true;
        }
    }

    cJSON_Delete(root);
}

// LSP要求ID履歴を1始まりの連番で初期化する。
// 引数: id_data=ID配列と現在位置を持つ送受信状態。 返り値: なし。id_dataがNULLの場合の動作は未定義。
void initialize_id(struct lsp_send_receve_id_data *id_data){
    int size = sizeof(id_data->used_id_history);
    int arry_size = size/sizeof(int);
    for(int i = 0; i < arry_size ;i++){
        id_data->used_id_history[i] = i + 1;
    }
    id_data->id_storage_counter = 0;
}

// LSPへ通知する言語IDをプロセス状態へコピーする。
// 引数: lsp=設定先、language=NUL終端された言語ID。 返り値: なし。languageがNULLまたは格納先より長い場合は変更しない。
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

// 文書を開いたことと全文をLSPサーバへ通知する。
// 引数: fd=書き込みfd、uri=文書URI、language_id=言語ID、text=UTF-8の全文。 返り値: 送信成功時0、引数不正・JSON生成・送信失敗時-1。
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


// 文書の新しい版と全文をLSPサーバへ通知する。
// 引数: fd=書き込みfd、uri=文書URI、version=1以上の版番号、text=UTF-8の全文。 返り値: 送信成功時0、引数不正・JSON生成・送信失敗時-1。
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

// 要求データからLSP用JSON文字列を生成する。
// 引数: msg_data=要求ID・method・URI・位置、msg=生成文字列の返却先。 返り値: 成功時0、引数不正またはJSON生成失敗時-1。失敗時の*msgはNULL。 所有権: 成功時の*msgは呼び出し側がfree()する。
int lsp_make_msg(lsp_send_msg_data msg_data, char **msg){
    cJSON *root;
    cJSON *params;
    cJSON *text_document;
    cJSON *position;
    char *json;

    if(msg == NULL){
        return -1;
    }
    *msg = NULL;

    if(msg_data.id <= 0 || msg_data.uri == NULL ||
       msg_data.pos.line < 0 || msg_data.pos.character < 0){
        return -1;
    }

    root = cJSON_CreateObject();
    if(root == NULL){
        return -1;
    }

    if(cJSON_AddStringToObject(root, "jsonrpc", "2.0") == NULL ||
       cJSON_AddNumberToObject(root, "id", msg_data.id) == NULL){
        cJSON_Delete(root);
        return -1;
    }

    switch(msg_data.lsp_method){
        case lsp_method_completion:
            if(cJSON_AddStringToObject(root, "method", "textDocument/completion") == NULL){
                cJSON_Delete(root);
                return -1;
            }
            break;
        case lsp_method_none:
        default:
            cJSON_Delete(root);
            return -1;
    }

    params = cJSON_AddObjectToObject(root, "params");
    if(params == NULL){
        cJSON_Delete(root);
        return -1;
    }

    text_document = cJSON_AddObjectToObject(params, "textDocument");
    if(text_document == NULL ||
       cJSON_AddStringToObject(text_document, "uri", msg_data.uri) == NULL){
        cJSON_Delete(root);
        return -1;
    }

    position = cJSON_AddObjectToObject(params, "position");
    if(position == NULL ||
       cJSON_AddNumberToObject(position, "line", msg_data.pos.line) == NULL ||
       cJSON_AddNumberToObject(position, "character", msg_data.pos.character) == NULL){
        cJSON_Delete(root);
        return -1;
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(json == NULL){
        return -1;
    }

    *msg = json;
    return 0;
}

// 現在のファイルパスとカーソル位置で補完要求を生成して送信する。
// 引数: lsp_fd=LSPサーバへの書き込みfd、state=ファイルパスとカーソル位置。 返り値: 送信成功時0、JSON生成または送信失敗時-1。 所有権: 生成したJSON文字列は送信後にこの関数が解放する。
int lsp_send_completion(int lsp_fd,struct editor_state *state){
    lsp_send_msg_data msg_data = {0};
    msg_data.id = 3;
    msg_data.lsp_method = lsp_method_completion;
    msg_data.pos.line = state->cursor.file_pos.y;
    msg_data.pos.character = state->cursor.file_pos.x;
    char uri[512];
    lsp_path_to_file_uri(uri,sizeof(uri),state->file_data.now_open_path_name);
    msg_data.uri = uri;

    char *lsp_msg_ptr = NULL;
    if(lsp_make_msg(msg_data,&lsp_msg_ptr) == -1){
        return -1;
    }

    int result = lsp_send(lsp_fd,lsp_msg_ptr);
    free(lsp_msg_ptr);
    return result;
}
