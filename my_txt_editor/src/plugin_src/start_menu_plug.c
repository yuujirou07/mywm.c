#include<stdio.h>
#include<ncurses.h>
#include <string.h>
#include<stdlib.h>
#include"error_log.h"
#include"ascii_art_comb.h"
#include"start_menu.h"
#include"txt_editor.h"

#define option_list_max 8

int draw_start_menu(int screen_max_w,int screen_max_h);
void draw_ascii_logo(struct pos screen_max_pos,struct ascii_data *ascii_data);
void draw_option(struct pos screen_max_pos,struct pos screen_mid_pos,struct ascii_data ascii_data);
void draw_varsion(struct pos screen_mid_pos,struct ascii_data ascii_data);



int draw_start_menu(int screen_max_w,int screen_max_h){
        struct pos screen_max_pos = (struct pos){screen_max_w,screen_max_h};
        struct pos screen_mid_pos = (struct pos){screen_max_w/2,screen_max_h/2};
        struct ascii_data ascii_data = {0};
        draw_ascii_logo(screen_mid_pos,&ascii_data);
        draw_option(screen_max_pos, screen_mid_pos,ascii_data);
        draw_varsion(screen_mid_pos,ascii_data);
        refresh();
        while(1){
                wint_t ch = 0;
                int input_result;
                input_result = get_wch(&ch);
                if(input_result == ERR){
                        continue;
                }

                if(ch == 'q'){
                    clear();
                    refresh();
                    return 1;
                }
                
                
        }
        
        


        return 0;
}

void draw_ascii_logo(struct pos screen_mid_pos,struct ascii_data *ascii_data){

        FILE *ascii_file = fopen("/home/yuujirou07/vscode_proj/mywm_proj/my_txt_editor/my_txt_editor_settings_folder/ascii_art_img.txt","r");
        if(ascii_file == NULL){
                error_log_write("can not open ascii art file\n");
                return;
        }
        get_ascii_data(ascii_data,ascii_file);
        
        int h_counter = 0;
        int str_size = strlen(ascii_data->ascii_data[h_counter]);
        while(h_counter < ascii_data->h ){
                move(h_counter,screen_mid_pos.x-(str_size/2));
                addstr(ascii_data->ascii_data[h_counter++]);
        }

        fclose(ascii_file);
}


void draw_option(struct pos screen_max_pos,struct pos screen_mid_pos,struct ascii_data ascii_data){
        int ascii_art_option_space = 5;
        int option_list_counter = 0;
        int option_short_cut_key = screen_max_pos.x/3;

        struct option_data{
                char *option_name;
                char *short_cut_key;
        };        
        struct option_data option_data[option_list_max];
        option_data[option_list_counter].option_name    = "new file";
        option_data[option_list_counter].short_cut_key  = "[n]";
        option_list_counter++;

        option_data[option_list_counter].option_name    = "select folder";
        option_data[option_list_counter].short_cut_key  = "[f]";
        option_list_counter++;

        option_data[option_list_counter].option_name    = "settings";
        option_data[option_list_counter].short_cut_key  = "[s]";
        option_list_counter++;

        option_data[option_list_counter].option_name    = "quit my txt editor";
        option_data[option_list_counter].short_cut_key  = "[q]";
        option_list_counter++;


        for(int i = 0; i < option_list_counter;i++){
                int option_list_str_len = strlen(option_data[i].option_name);
                int option_pos_y = ascii_data.h + ascii_art_option_space + i;
                int option_pos_x = screen_max_pos.x/3;

                if( option_pos_y > screen_max_pos.y ){
                        //画面を大きくしろの警告を表示
                        return;
                }
                else if(option_list_str_len > screen_max_pos.x){
                        //画面を大きくしろの警告を表示
                        return;
                }

                mvaddstr(option_pos_y,option_pos_x,option_data[i].option_name);
                mvaddstr(option_pos_y,option_pos_x + option_short_cut_key,option_data[i].short_cut_key);
        }


}

void draw_varsion(struct pos screen_mid_pos,struct ascii_data ascii_data){
        char var[16];
        snprintf(var,16,"Var%.1f",my_txt_editor_var);
        int len = strlen(var);
        mvaddstr(ascii_data.h-1,screen_mid_pos.x - (len/2),var);
}

