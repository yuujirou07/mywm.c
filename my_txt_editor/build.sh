#!/bin/bash

set -e

SRC="src/main.c src/txt_editor_func.c src/my_cui_lib_linux.c"
FLAGS="-D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I./include"
OUT="main"

gcc $FLAGS $SRC -lncursesw -o $OUT
echo "Build OK: $OUT"
