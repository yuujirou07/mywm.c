// gcc -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -Iinclude -Iinclude/lsp_src test_settings.c src/settings_parse_src/editor_settings_parse.c src/json_read.c src/path_util.c -lcjson -o /tmp/test_settings && /tmp/test_settings
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "txt_editor.h"

int main(void){
    char original_dir[PATH_MAX];
    char temp_dir[] = "/tmp/my_txt_editor_settings_XXXXXX";
    assert(getcwd(original_dir, sizeof(original_dir)) != NULL);
    assert(mkdtemp(temp_dir) != NULL);
    assert(chdir(temp_dir) == 0);
    assert(mkdir("editor_settings", 0700) == 0);

    const char *cases[] = {
        "{\"display\":{\"show_status_bar\":true,\"status_bar_side\":\"bottom\","
        "\"line_number_space\":6,\"draw_split_line\":true,\"show_start_menu\":true,\"use_icon\":true},"
        "\"editor\":{\"indent_range\":2,\"jmp_set_cur_pos\":7,\"built_in_syntax\":true},"
        "\"buffer\":{\"max_lines\":123,\"max_line_size\":456,\"default_load_line_size\":789,\"load_buffer_lines\":12},"
        "\"auto_complete\":{\"enabled\":true,\"window\":{\"show\":true,\"width\":20,\"height\":8}},"
        "\"lsp\":{\"launch_startup_editor\":true,\"epoll_timeout_ms\":25}}",
        "{\"show_status_bar\":true,\"status_bar_side\":\"bottom\",\"line_number_space\":6,"
        "\"draw_split_line\":true,\"show_start_menu\":true,\"use_icon\":true,\"indent_range\":2,"
        "\"jmp_set_cur_pos\":7,\"built_in_syntax\":true,\"auto_complete\":true,"
        "\"max_lines\":123,\"max_line_size\":456,\"default_load_line_size\":789,\"load_buffer_lines\":12,"
        "\"lsp\":{\"launch_startup_editor\":true,\"epoll_timeout_ms\":25}}",
        "{}",
        "{\"display\":false,\"editor\":[],\"buffer\":null,\"lsp\":42}",
        "{\"display\":{\"show_status_bar\":\"true\",\"status_bar_side\":42},"
        "\"editor\":{\"auto_complete\":1},\"buffer\":{\"max_lines\":false}}",
        "{\"use_icon\":true,\"display\":{\"use_icon\":false}}"
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++){
        FILE *fp = fopen("editor_settings/my_txt_editor_settings.json", "w");
        assert(fp != NULL);
        assert(fputs(cases[i], fp) >= 0);
        assert(fclose(fp) == 0);
        struct editor_settings data = {
            .line_number_space = 4, .max_line_size = MAX_LINE_SIZE,
            .default_load_line_size = DEFAULT_LOAD_LINE_SIZE,
            .load_buffer_lines = LOAD_BUFFER_LINES, .indent_range = INDENT_RANGE,
            .bar_side_state = top
        };
        load_custom_editor_settings(&data);
        if(i < 2){
            assert(data.show_status_bar && data.bar_side_state == bottom);
            assert(data.line_number_space == 6 && data.draw_split_line);
            assert(data.show_start_menu && data.use_icon);
            assert(data.indent_range == 2 && data.jmp_set_cur_pos == 7);
            assert(data.built_in_syntax);
            assert(data.auto_complete_settings_data.auto_complete_enabled);
            if(i == 0){
                assert(data.auto_complete_settings_data.auto_complete_window_enable);
                assert(data.auto_complete_settings_data.auto_complete_window_size.x == 20);
                assert(data.auto_complete_settings_data.auto_complete_window_size.y == 8);
            }
            assert(data.max_lines == 123 && data.max_line_size == 456);
            assert(data.default_load_line_size == 789 && data.load_buffer_lines == 12);
            assert(data.lsp.lsp_launch_startup_editor && data.lsp.lsp_epoll_timeout_ms == 25);
        }
        else{
            assert(!data.show_status_bar && data.bar_side_state == top);
            assert(data.line_number_space == 4 && !data.draw_split_line);
            assert(!data.show_start_menu && !data.use_icon);
            assert(data.indent_range == INDENT_RANGE && data.jmp_set_cur_pos == 0);
            assert(!data.built_in_syntax);
            assert(!data.auto_complete_settings_data.auto_complete_enabled);
            assert(data.max_lines == 0 && data.max_line_size == MAX_LINE_SIZE);
            assert(data.default_load_line_size == DEFAULT_LOAD_LINE_SIZE);
            assert(data.load_buffer_lines == LOAD_BUFFER_LINES);
            assert(!data.lsp.lsp_launch_startup_editor && data.lsp.lsp_epoll_timeout_ms == 0);
        }
    }
    assert(unlink("editor_settings/my_txt_editor_settings.json") == 0);
    assert(rmdir("editor_settings") == 0);
    assert(chdir(original_dir) == 0);
    assert(rmdir(temp_dir) == 0);
    puts("settings: OK");
}
