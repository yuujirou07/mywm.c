#include <ncurses.h>
#include <stdint.h>
#include<stdio.h>
#include <string.h>
#include"error_log.h"
#include"ascii_art_comb.h"

// get_ascii_data(): ASCIIアートをfileから読み込み、画面幅に収まる場合だけascii_dataへ保持する。
// 引数: ascii_data=読み込み先、file=読み込み済みFILE、screen_width=表示可能な最大幅。
// 返り値: なし。引数不正、幅超過、行数超過時は途中で終了する。
// 所有権: fileの所有権は呼び出し側にあり、この関数は閉じない。
void get_ascii_data(struct ascii_data *ascii_data,FILE *file,int screen_width){
        if(ascii_data == NULL || file == NULL)return;
        // 危険: h++はfgets()の成否より先に実行され、EOFでもhが増える。
        // 上限到達後は範囲外要素をfgets()へ渡してから検査するため、配列外書き込みになり得る。
        char *ascii_art_w = 0;
        while((ascii_art_w = fgets(ascii_data->ascii_data[ascii_data->h],ascii_data_w_max,file)) != NULL){
                //もしロゴの幅より画面サイズが小さかったら表示しない
                if((int)strlen(ascii_art_w) > screen_width){
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
