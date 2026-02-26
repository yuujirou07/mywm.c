#include <stdio.h>
#include <raylib.h>



int main(void){
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800,600,"test");
     Texture2D tex = LoadTexture("/usr/share/pixmaps/com.visualstudio.code.oss.png");   // PNG読み込み

while(!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(BLACK);

    DrawTextureEx(
    tex,
    (Vector2){100,100},   // 表示位置
    0.0f,                 // 回転
    0.5f,                 // ←倍率 0.5 = 半分
    WHITE
);
    EndDrawing();
}
UnloadTexture(tex);
CloseWindow();
}