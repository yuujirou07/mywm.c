#!/bin/bash
set -eu
cd "$(dirname "$0")"
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
cat > "$tmp_dir/test.c" <<'EOF'
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "txt_editor.h"

int main(int argc, char **argv){
    assert(argc == 2);
    assert(setlocale(LC_ALL,"C.UTF-8") != NULL);
    assert(chdir(argv[1]) == 0);
    assert(mkdir("browse",0700) == 0);
    assert(mkdir("browse/item.c",0700) == 0);
    FILE *file = fopen("item.c","w");
    assert(file != NULL);
    fclose(file);
    ext_icon mapping = {.icon_code = "C", .file_ext = ".c"};
    struct editor_settings test_settings = {0};
    test_settings.icon_data = (icon_data){.icon_lib = &mapping, .icon_lib_num = 1};
    assert(strcmp(get_file_ext_code("browse/item.c",&test_settings.icon_data),"\U0001F4C2") == 0);
    assert(strcmp(get_file_ext_code("item.c",&test_settings.icon_data),"C") == 0);
    assert(strcmp(get_file_ext_code("missing.c",&test_settings.icon_data),"C") == 0);
    assert(strcmp(get_file_ext_code("missing",&test_settings.icon_data),"\uef4c") == 0);
    FILE *output = tmpfile();
    FILE *input = tmpfile();
    assert(output != NULL && input != NULL);
    SCREEN *screen = newterm("xterm",output,input);
    assert(screen != NULL);
    struct editor_state *state = calloc(1,sizeof(*state));
    assert(state != NULL);
    state->settings_data = &test_settings;
    state->file_browse.area = (struct box){.pos = {0,0}, .w = 20, .h = 1};
    strcpy(state->file_browse.path_name,"browse");
    struct dir_entry entry = {.name = "item.c", .d_type = DT_DIR};
    draw_box_inside_dir(state,&entry);
    cchar_t cell;
    wchar_t chars[CCHARW_MAX];
    attr_t attrs;
    short pair;
    assert(mvwin_wch(stdscr,0,1,&cell) != ERR);
    assert(getcchar(&cell,chars,&attrs,&pair,NULL) != ERR);
    assert(chars[0] == L'\U0001F4C2');
    free(state);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("file browser icon: OK");
    return 0;
}
EOF
gcc -Wall -Wextra -Werror -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -I./include -I./include/lsp_src "$tmp_dir/test.c" \
    src/txt_editor_draw.c src/settings_parse_src/editor_icon_parse.c \
    -Wl,--gc-sections -lncursesw -o "$tmp_dir/test"
"$tmp_dir/test" "$tmp_dir"
