#include <ncurses.h>

int main()
{
    int x, y;

    initscr();
    while (getch() != 'q') {
        printw("(%3d %3d)\n", LINES, COLS);
        refresh();
    }
    endwin();
    return 0;
}