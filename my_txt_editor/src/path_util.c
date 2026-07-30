#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "path_util.h"

// editor_path_from_exe_dir(): 詳細はpath_util.hのコメントを参照。
// /proc/self/exeは、プラグイン(.so)の中から呼んでも本体の実行ファイルを指すため、
// 本体・プラグインのどちらから呼んでも同じディレクトリが基準になる。
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
