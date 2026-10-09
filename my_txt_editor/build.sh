#!/bin/bash

set -e

MAIN_SRC="src/main.c src/screen_state/txt_editor_state.c src/txt_editor_func.c src/txt_editor_draw.c src/txt_editor_file.c src/txt_editor_syntax.c src/json_read.c src/error_log.c src/lsp_src/language_server_communication.c src/path_util.c src/settings_parse_src/editor_settings_parse.c src/settings_parse_src/settings_items_parse.c src/settings_parse_src/editor_icon_parse.c"
MAIN_SRC="$MAIN_SRC src/screen_state/txt_editor_edit_screen.c src/screen_state/txt_editor_file_browse_screen.c src/screen_state/txt_editor_line_jump_screen.c src/screen_state/txt_editor_error_screen.c src/screen_state/txt_editor_make_file_screen.c src/screen_state/txt_editor_start_menu_screen.c src/screen_state/txt_editor_settings_screen.c src/screen_state/txt_editor_filetree_screen.c"
MAIN_SRC="$MAIN_SRC src/plugin_src/edit_input_complete.c"
MAIN_SRC="$MAIN_SRC src/public_settings_src/c_settings_src.c/c_settings_settings.c"
# Temporary in-repo substitute until the external ftj dependency is available.
FTJ_SRC="ftj_temp/ftj.c"
PLUGIN_SRC="src/plugin_src/start_menu_plug.c src/plugin_src/ascii_art_comb.c src/error_log.c src/path_util.c"
SETTINGS_SRC="editor_settings/src/settings.c"
FLAGS="-Wall -O0 -fsanitize=address,undefined -Wextra -Werror -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I./include -I./include/lsp_src -I./ftj_temp"
OUT="main"
PLUGIN_OUT="so_file/start_menu_plug.so"
SETTINGS_OUT="so_file/settings.so"

mkdir -p so_file

gcc $FLAGS -shared -fPIC $PLUGIN_SRC -lncursesw -o $PLUGIN_OUT
gcc $FLAGS -shared -fPIC $SETTINGS_SRC -o $SETTINGS_OUT
gcc $FLAGS $MAIN_SRC $FTJ_SRC -lcjson -lncursesw -ldl -o $OUT
echo "Build OK: $OUT"
echo "Build OK: $PLUGIN_OUT"
echo "Build OK: $SETTINGS_OUT"
