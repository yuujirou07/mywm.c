#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "path_util.h"

// 実行ファイル位置を/proc/self/exeで取得し、relativeを連結してbuf_sizeバイトのbufへ書く。
// 返り値: 成功時buf、引数不正・位置取得失敗・長さ超過はNULL。bufは呼び出し側所有で、プラグイン内でも本体が基準。
char *editor_path_from_exe_dir(char *buf, size_t buf_size, const char *relative){
    if(buf == NULL || buf_size == 0 || relative == NULL){
        return NULL;
    }

    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if(len <= 0){
        return NULL;
    }
    exe_path[len] = '\0';

    // 実行ファイル名を落としてディレクトリ部分だけにする
    char *slash = strrchr(exe_path, '/');
    if(slash == NULL){
        return NULL;
    }
    *slash = '\0';

    int written = snprintf(buf, buf_size, "%s/%s", exe_path, relative);
    if(written < 0 || (size_t)written >= buf_size){
        return NULL;
    }
    return buf;
}
