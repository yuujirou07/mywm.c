#include<stdio.h>
#include<ncurses.h>
#include<stdlib.h>
#include"error_log.h"
#include"ascii_art_comb.h"
#include"start_menu.h"


void draw_start_menu(int screen_w,int screen_h){
        int scr_mid_pos_x = screen_w/2;
        int scr_mid_pos_y = screen_h/2;

        FILE *ascii_file = fopen("/home/yuujirou07/vscode_proj/mywm_proj/my_txt_editor/my_txt_editor_settings_folder/ascii_art_img.txt","r");
        if(ascii_file == NULL){
                error_log_write("can not open ascii art file\n");
                return;
        }
        struct ascii_data ascii_data = {0};
        get_ascii_data(&ascii_data,ascii_file);
        
        int h_counter = 0;
        while(h_counter < ascii_data.h ){
                move(h_counter,0);
                addstr(ascii_data.ascii_data[h_counter++]);
        }
        fclose(ascii_file);

        return;
}




