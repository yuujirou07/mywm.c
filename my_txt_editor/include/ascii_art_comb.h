#ifndef ASCII_ART_COMB_H
#define ASCII_ART_COMB_H

#include<stdio.h>
#define ascii_data_h_max 256
#define ascii_data_w_max 256

struct ascii_data{
        int w;
        int h;
        char ascii_data[ascii_data_h_max ][ascii_data_w_max + 1];
};

void get_ascii_data(struct ascii_data *ascii_data,FILE *file);


#endif 