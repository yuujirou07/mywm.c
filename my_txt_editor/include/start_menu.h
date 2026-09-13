#ifndef START_MENU_H
#define START_MENU_H
#include <time.h>
#include "ascii_art_comb.h"

#define my_txt_editor_var 0.0
#define new_file 0
#define quit 1
#define select_folder 3
#define settings 2
#define none 4
#define resize_request 5
#define option_list_max 8

int draw_start_menu(int screen_max_w,int screen_max_h,struct ascii_data *ascii_data_ptr,
                    const struct timespec *startup_start_time,
                    const char *startup_log_path);

#endif
