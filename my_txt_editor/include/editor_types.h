#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

struct pos {
    int x;
    int y;
};

struct box {
    struct pos pos;
    int w;
    int h;
};


enum flags{
    set,
    get,
};



#endif
