#include <stdio.h>
#include"raylib.h"

int main(int argc,char *argv[]){
    int screen_x=500;
    int screen_y=500;
    InitWindow(screen_x,screen_y,"hello");
    SetTargetFPS(60);
    while(!WindowShouldClose()){
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Congrats! You created your first window!",
        0, 250, 20, LIGHTGRAY);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}