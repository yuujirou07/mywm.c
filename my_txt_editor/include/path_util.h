#ifndef PATH_UTIL_H
#define PATH_UTIL_H

#include <stddef.h>

// editor_path_from_exe_dir(): 実行ファイルのあるディレクトリを基準にパスを組み立てる。
// カレントディレクトリがどこであっても、プラグイン・設定・データファイルを
// 実行ファイルの隣から見つけられるようにする。本体とプラグインの両方が使う。
// 引数: buf=書き込み先、buf_size=bufのバイト数、relative=実行ファイルからの相対パス。
// 返り値: 組み立てたbuf。実行ファイルの位置が取れない、または長すぎるならNULL。
char *editor_path_from_exe_dir(char *buf, size_t buf_size, const char *relative);

#endif
