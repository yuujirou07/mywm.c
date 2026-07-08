#ifndef DEFALT_SETTINGS_H
#define DEFALT_SETTINGS_H

#include<ncurses.h>
#define MAX_LINES    1000
#define MAX_LINE_SIZE 1024
#define LINE_NUMBER_SPACE 4 
#define INDENT_RANGE 8      //インデントの幅
#define JMP_SET_CUR_POS 10  
#define DEFAULT_LOAD_LINE_SiZE 9999//行制限
#define LOAD_BUFFER_LINES 100//行の最大文字
#define SHOW_STATUS_BAR 1 //ステータスバーの表示
#define DEFAULT_DRAW_SPLIT_LINE 1//行と書き込み領域の間を線で区切るか
#define DEFAULT_STATUS_BAR_SIDE top//ステータスバーの位置
#define JUMP_LINE_NUM_DIGITS 4//行指定の書き込みセル数
#define DEFAULT_ASK_MAKE_FILE 1//ファイル変更時に何もファイルを開いていなかった場合ファイルを作るか聞く
#define DEFAULT_PATH_NAME_MAX_SIZE PATH_MAX
#define DEFAULT_FILE_SELECT_SCENE_LIGHTING false
#define DEFAULT_SHOW_START_MENU true

struct editor_settings{
    int max_lines;
    int max_line_size;
    int line_number_space;
    int indent_range;
    int jmp_set_cur_pos;
    int default_load_line_size;
    int load_buffer_lines;
    int bar_side_state;
    bool show_status_bar;
    bool draw_split_line;
    bool ask_make_file;//ファイル変更時に何もファイルを開いていなかった場合ファイルを作るか聞く
    bool show_start_menu;
    bool file_select_scene_lighting;
    
};
#endif 
