#include<stdio.h>
#include<ncurses.h>
#include <string.h>
#include <wctype.h>
#include<time.h>
#include"error_log.h"
#include"ascii_art_comb.h"
#include"start_menu.h"
#include"txt_editor.h"


#define option_list_max 8

struct option_data{
        char *option_name;
        char short_cut_key;
        int x;
        int y;
        int w;
};   


void option_fn(char *key,int *return_numk);
int draw_option(struct pos screen_max_pos,struct pos *option_start_pos,struct ascii_data ascii_data,struct option_data *option_data,int size);
void draw_ascii_logo(struct pos screen_max_pos,struct ascii_data *ascii_data);
void draw_varsion(struct pos screen_mid_pos,struct ascii_data ascii_data);
static void write_startup_time_log(const struct timespec *start_time, const char *log_path);


int draw_start_menu(int screen_max_w,int screen_max_h,struct ascii_data *ascii_data_ptr,
                    const struct timespec *startup_start_time,
                    const char *startup_log_path){
        static bool is_first_start_menu = 1;
        struct pos screen_max_pos   = (struct pos){screen_max_w,screen_max_h};
        struct pos screen_mid_pos   = (struct pos){screen_max_w/2,screen_max_h/2};
        struct pos option_start_pos = {0};
        struct ascii_data ascii_data = {0};
        struct option_data option_data[option_list_max];

        draw_ascii_logo(screen_mid_pos,&ascii_data);
        int option_count = draw_option(screen_max_pos,&option_start_pos,ascii_data,option_data,option_list_max);
        draw_varsion(screen_mid_pos,ascii_data);


        for(int i = 0;i < option_count;i++){
                mvchgat(option_data[i].y,option_data[i].x,
                        option_data[i].w,A_BOLD,1,NULL);
        }
        refresh();
        if(is_first_start_menu){
                write_startup_time_log(startup_start_time,startup_log_path);
                is_first_start_menu = 0;
        }
        
        while(1){

                wint_t ch = 0;
                int input_result;
                input_result = get_wch(&ch);
                if(input_result == ERR){
                        continue;
                }

                int return_num = 0;
                for(int i = 0;i < option_list_max;i++){
                        if(ch != (wint_t)option_data[i].short_cut_key){continue;}
                        option_fn(&option_data[i].short_cut_key,&return_num);
                        if(ascii_data_ptr != NULL){
                                *ascii_data_ptr = ascii_data;
                        }
                        return return_num;
                }
                refresh();
        }
        return 0;
}

static void write_startup_time_log(const struct timespec *start_time, const char *log_path){
        if(start_time == NULL || log_path == NULL){
                return;
        }

        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        double msec = (end_time.tv_sec - start_time->tv_sec) * 1000.0 +
                      (end_time.tv_nsec - start_time->tv_nsec) / 1000000.0;

        FILE *startup_timer_log_file = fopen(log_path, "w");
        if(startup_timer_log_file == NULL){
                error_log_write("can not count start up time :(");
                return;
        }

        fprintf(startup_timer_log_file,"%f\n",msec);
        fclose(startup_timer_log_file);
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


int draw_option(struct pos screen_max_pos,struct pos *option_start_pos,struct ascii_data ascii_data,struct option_data *option_data,int size){

        int ascii_art_option_space = 5;
        int option_list_counter = 0;
        int option_short_cut_key = screen_max_pos.x/3;
     
        if(option_list_counter < size){
                option_data[option_list_counter].option_name    = "new file";
                option_data[option_list_counter].short_cut_key  = 'n';
                option_list_counter++;
        }

        if(option_list_counter < size){
                option_data[option_list_counter].option_name    = "select folder";
                option_data[option_list_counter].short_cut_key  = 'f';
                option_list_counter++;
        }

        if(option_list_counter < size){
                option_data[option_list_counter].option_name    = "quit my txt editor";
                option_data[option_list_counter].short_cut_key  = 'q';
                option_list_counter++;
        }

        int option_pos_y = ascii_data.h + ascii_art_option_space;
        int option_pos_x = screen_max_pos.x/3;

        *option_start_pos = (struct pos){option_pos_x,option_pos_y};
        char skt[4];
        skt[0]= '[';
        //skt[1] in loop
        skt[2]= ']';
        skt[3]= '\0';

        for(int i = 0; i < option_list_counter;i++){
                skt[1] = option_data[i].short_cut_key;
                int short_cut_key_len = strlen(skt);
                int option_draw_len = option_short_cut_key + short_cut_key_len;
                int option_y = option_pos_y + (i * 2);

                if( option_y > screen_max_pos.y ){
                        //画面を大きくしろの警告を表示
                        return 0;
                }
                else if(option_pos_x + option_draw_len > screen_max_pos.x){
                        //画面を大きくしろの警告を表示
                        return 0;
                }

                option_data[i].x = option_pos_x;
                option_data[i].y = option_y;
                option_data[i].w = option_draw_len;

                mvaddstr(option_y,option_pos_x,option_data[i].option_name);
                mvaddstr(option_y,option_pos_x + option_short_cut_key,skt);
        }

        return option_list_counter;
}

void draw_varsion(struct pos screen_mid_pos,struct ascii_data ascii_data){
        char var[16];
        snprintf(var,16,"Var%.1f",my_txt_editor_var);
        int len = strlen(var);
        mvaddstr(ascii_data.h-1,screen_mid_pos.x - (len/2),var);
}

void option_fn(char *key,int *return_num){
        switch(*key){
                case 'q':{
                        *return_num = quit;
                        break;
                }
                case 'n':{
                        *return_num = new_file;
                        break;
                }
                case 'f':{
                        *return_num = select_folder;
                        break;
                }
                default:{
                        *return_num = none;
                        break;
                }
        }
        return;
}
