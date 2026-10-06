#!/bin/bash
set -eu
project_dir=$(cd -- "$(dirname -- "$0")" && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
mkdir "$test_dir/editor_settings"
cat > "$test_dir/test.c" <<'EOF'
#include <assert.h>
#include <stdio.h>
#include "txt_editor.h"

int main(void){
    struct {
        const char *json;
        bool enabled;
        int size;
    } cases[] = {
        {"{}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":{\"enabled\":true,\"buffer_size\":1}}", true, 1},
        {"{\"key_log\":{\"enabled\":false,\"buffer_size\":65535}}", false, 65535},
        {"{\"key_log\":{\"enabled\":1,\"buffer_size\":0}}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":{\"enabled\":\"true\",\"buffer_size\":-1}}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":{\"buffer_size\":65536}}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":{\"buffer_size\":1.5}}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":{\"buffer_size\":\"32\"}}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE},
        {"{\"key_log\":true}", DEFAULT_USE_KEY_LOG, DEFAULT_KEY_LOG_BUFFER_SIZE}
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++){
        FILE *fp = fopen("editor_settings/my_txt_editor_settings.json", "w");
        assert(fp != NULL);
        assert(fputs(cases[i].json, fp) >= 0);
        assert(fclose(fp) == 0);
        struct editor_settings settings_data = {0};
        load_default_editor_settings(&settings_data);
        assert(settings_data.key_log_settings.use_key_log == DEFAULT_USE_KEY_LOG);
        assert(settings_data.key_log_settings.key_log_buffer_size == DEFAULT_KEY_LOG_BUFFER_SIZE);
        load_custom_editor_settings(&settings_data);
        assert(settings_data.key_log_settings.use_key_log == cases[i].enabled);
        assert(settings_data.key_log_settings.key_log_buffer_size == cases[i].size);
    }
    puts("key_log settings: OK");
    return 0;
}
EOF
gcc -Wall -Wextra -Werror -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -I"$project_dir/include" -I"$project_dir/include/lsp_src" \
    "$test_dir/test.c" "$project_dir/src/txt_editor_file.c" \
    "$project_dir/src/settings_parse_src/editor_settings_parse.c" \
    "$project_dir/src/json_read.c" "$project_dir/src/path_util.c" \
    -Wl,--gc-sections -lcjson -lncursesw -o "$test_dir/test"
cd "$test_dir"
./test
