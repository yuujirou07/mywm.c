#!/bin/bash

set -e

MAIN_SRC="src/main.c src/txt_editor_state.c src/txt_editor_func.c src/txt_editor_draw.c src/txt_editor_file.c src/json_read.c src/error_log.c src/language_server_communication.c"
PLUGIN_SRC="src/plugin_src/start_menu_plug.c src/plugin_src/ascii_art_comb.c src/error_log.c"
FLAGS="-Wall -Wextra -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I./include"
OUT="main"
PLUGIN_OUT="so_file/start_menu_plug.so"

mkdir -p so_file

gcc $FLAGS -shared -fPIC $PLUGIN_SRC -lncursesw -o $PLUGIN_OUT
gcc $FLAGS $MAIN_SRC -lcjson -lncursesw -ldl -o $OUT
echo "Build OK: $OUT"
echo "Build OK: $PLUGIN_OUT"
