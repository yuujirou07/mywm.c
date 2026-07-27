#include<stdio.h>
#include"error_log.h"
#include"ascii_art_comb.h"

void get_ascii_data(struct ascii_data *ascii_data,FILE *file){
        // 危険: h++はfgets()の成否より先に実行され、EOFでもhが増える。
        // 上限到達後は範囲外要素をfgets()へ渡してから検査するため、配列外書き込みになり得る。
        while(fgets(ascii_data->ascii_data[ascii_data->h++],ascii_data_w_max,file) != NULL){
                if(ascii_data->h > ascii_data_h_max){
                        error_log_write("ascii art data overflow");
                        return;
                }
        }

        return;
}
