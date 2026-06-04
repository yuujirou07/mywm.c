#include<ncurses.h>
#include"txt_editor.h"

//行数表示
void draw_line_numbers(struct scr_data *scr_data ,struct write_possible_area *area){
    for(int i = 1;i<=scr_data->scr_size.y;i++)
    {       
        char num_str[6];
        int size = snprintf(num_str,6,"%d",i);
        num_str[size] = '\0';
        mvaddstr(i-1,4-size,num_str);
    }
    refresh();
    //カーソルと行番号との間に1マス開ける
    area->x_start = 5;
    area->y_start = 0;

    area->x_end -= 4;
    area->y_end = scr_data->scr_size.y;
}

