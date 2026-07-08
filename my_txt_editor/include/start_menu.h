#ifndef START_MENU_H
#define START_MENU_H
#include <time.h>
#include "ascii_art_comb.h"

int draw_start_menu(int screen_max_w,int screen_max_h,struct ascii_data *ascii_data_ptr,
                    const struct timespec *startup_start_time,
                    const char *startup_log_path);

#endif
