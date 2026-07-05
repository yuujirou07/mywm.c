#include<stdio.h>
#include"error_log.h"
#include"ascii_art_comb.h"

void get_ascii_data(struct ascii_data *ascii_data,FILE *file){
        while(fgets(ascii_data->ascii_data[ascii_data->h++],ascii_data_w_max,file) != NULL){
                if(ascii_data->h > ascii_data_h_max){
                        error_log_write("ascii art data overflow");
                        return;
                }
        }

        return;
}