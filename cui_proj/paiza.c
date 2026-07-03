#include <stdio.h>
#include<string.h>
int main(void){
    int w;
    int h;
    int sp_num;
    scanf("%d",&w);
    scanf("%d",&h);
    
    scanf("%d",&sp_num);
    
    struct sp_pos{
        int x;
        int y;
    };
    struct sp_pos sp_data[sp_num];
    
    for(int i=0;i<sp_num;i++){
        scanf("%d",&sp_data[i].x);
        scanf("%d",&sp_data[i].y);
    }
    
    int total = w * h;
    int cells[h][w];
    memset(cells,0,sizeof(cells));

    for(int i = 0;i < sp_num;i++){
        int sp_x = sp_data[i].x;
        int sp_y = sp_data[i].y;

        if(sp_x - 1 > 0){
                cells[sp_y][sp_x-1]++;
        }

        if(sp_x + 1 < w){
                cells[sp_y][sp_x+1]++;
        }


        if(sp_y - 1 > 0){
                if(sp_x - 1 > 0){
                        cells[sp_y - 1][sp_x-1]++;
                }

                if(sp_x + 1 < w){
                        cells[sp_y - 1][sp_x+1]++;
                }
                 
                cells[sp_y - 1][sp_x]++;
                
        }

        if(sp_y + 1 < w){
                if(sp_x - 1 > 0){
                        cells[sp_y - 1][sp_x-1]++;
                }

                if(sp_x + 1 < w){
                        cells[sp_y - 1][sp_x+1]++;
                }
               
                cells[sp_y - 1][sp_x]++;
        }
    }



    int result = 0;
    for(int i = 0;i<h;i++){
         for(int b = 0;b<w;b++){
                if(cells[i][b] > 0){
                        result++;
                }
        }
    }

    printf("%d",result);
    return 0;
}