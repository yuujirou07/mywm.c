#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

// 呼び出し側が定める座標系上の2次元位置。
struct pos {
    int x; // 横方向の位置。画面座標では左端が0。
    int y; // 縦方向の位置。画面座標では上端が0。
};

// 左上位置と、そこから右・下へ広がる矩形の大きさ。
struct box {
    struct pos pos; // 矩形の左上座標。
    int w; // 横幅。枠付きUIでは枠線を含む端末セル数。
    int h; // 高さ。枠付きUIでは枠線を含む端末行数。
};


// 値を保存する操作と取得する操作の指定。
enum flags{
    set, // 値を保存する。
    get, // 保存済みの値を取得する。
};



#endif
