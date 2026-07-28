#include <ncurses.h>
#include <stdint.h>
#include<stdio.h>
#include <string.h>
#include"error_log.h"
#include"ascii_art_comb.h"

void get_ascii_data(struct ascii_data *ascii_data,FILE *file,int screen_width){
        if(ascii_data == NULL || file == NULL)return;
        // 危険: h++はfgets()の成否より先に実行され、EOFでもhが増える。
        // 上限到達後は範囲外要素をfgets()へ渡してから検査するため、配列外書き込みになり得る。
        char *ascii_art_w = 0;
        while((ascii_art_w = fgets(ascii_data->ascii_data[ascii_data->h],ascii_data_w_max,file)) != NULL){
                //もしロゴの幅より画面サイズが小さかったら表示しない
                if(strlen(ascii_art_w) > screen_width){
                        memset(ascii_data->ascii_data,'\n',sizeof(ascii_data->ascii_data));
                        return;
                }
                if(ascii_data->h > ascii_data_h_max){
                        error_log_write("ascii art data overflow");
                        return;
                }
                ascii_data->h++;
        }

        return;
}
