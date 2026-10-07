#include <stdio.h>
#include<stdlib.h>
#include "json_read.h"

// pathのファイル全体を読み込み、NUL終端の文字列を確保する。
// 返り値: 成功時は呼び出し側がfreeする文字列、オープン・読込・確保失敗時はNULL。
char *read_file_all(const char *path){
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        perror("fopen");
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if (read_size != (size_t)size) {
        free(buf);
        return NULL;
    }

    buf[size] = '\0';
    return buf;
}
