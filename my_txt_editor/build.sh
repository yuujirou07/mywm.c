#!/bin/bash

set -e

SRC="src/main.c src/txt_editor_func.c src/txt_editor_draw.c src/txt_editor_file.c src/json_read.c"
FLAGS="-Wall -Wextra -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I./include -lcjson"
OUT="main"

gcc $FLAGS $SRC -lncursesw -o $OUT
echo "Build OK: $OUT"
