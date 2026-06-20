#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "my_cui_lib.h"

static int convert_eight_bit_rgb_color(enum eight_bit_rgb eight_bit_rgb);

static char   big[4096];
static size_t pos = 0;

int term_init(){
    pos = 0;
    return 0;
}

static int convert_eight_bit_rgb_color(enum eight_bit_rgb eight_bit_rgb);

// ===== カーソル制御シーケンス (CSI) =====

void move_cursor(int x, int y){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%d;%dH", y, x);
}

void reset_cursor(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[H");
}

void cursor_up(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dA", count);
}

void cursor_down(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dB", count);
}

void cursor_move_right(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dC", count);
}

void cursor_move_left(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dD", count);
}

void cursor_down_line_start(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dE", count);
}

void cursor_up_line_start(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dF", count);
}

void cursor_line_move(int count){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dG", count);
}

void rq_cursor_pos(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[6n");
}

void save_cursor_pos(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[s");
}

void remove_cursor_pos(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[u");
}

// ===== 画面・行の消去機能 =====

void erase_cursor_to_end_of_screen(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[J");
}

void erase_cursor_to_start_of_screen(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[1J");
}

void erase_screen(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[2J");
}

void erase_scroll_buff(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[3J");
}

void erase_cursor_pos_to_line_end(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[K");
}

void erase_cursor_pos_to_line_start(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[1K");
}

void erase_cursor_line(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[2K");
}

// ===== 文字の書式設定と色 (SGR: Select Graphic Rendition) =====

void change_str_rgb_foreground_color_start(enum eight_bit_rgb eight_bit_rgb){
    int rgb_color = convert_eight_bit_rgb_color(eight_bit_rgb);
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dm", rgb_color);
}

void change_str_rgb_foreground_color_end(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[0m");
}

void change_str_rgb_background_color_start(enum eight_bit_rgb eight_bit_rgb){
    int rgb_color = convert_eight_bit_rgb_color(eight_bit_rgb);
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%dm", rgb_color + 10);
}

void change_str_rgb_background_color_end(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[49m");
}

void change_str_rgb_true_foreground_color_start(struct rgb rgb){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[38;2;%d;%d;%dm", rgb.r, rgb.g, rgb.b);
}

void change_str_rgb_true_foreground_color_end(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[39m");
}

void change_str_rgb_true_background_color_start(struct rgb rgb){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[48;2;%d;%d;%dm", rgb.r, rgb.g, rgb.b);
}

void change_str_rgb_true_background_color_end(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[49m");
}

// ===== 文字出力 =====

void myprint(char *str){
    pos += snprintf(big + pos, sizeof(big) - pos, "%s", str);
}

void myprint_at(int x, int y, const char *str){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[%d;%dH", y, x);
    pos += snprintf(big + pos, sizeof(big) - pos, "%s", str);
}

// ===== ターミナルモードの変更 =====

void hidden_cursor(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?25l");
}

void show_cursor(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?25h");
}

void change_buff_screen(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?47h");
}

void back_forward_screen(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?47l");
}

void report_focus_event(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?1004h");
}

void not_report_focus_event(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?1004l");
}

void on_bracket_paste_mode(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?2004h");
}

void off_bracket_paste_mode(){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b[?2004l");
}

// ===== オペレーティングシステムコマンド (OSC) =====

void change_window_name(char *w_name){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b]0;%sST", w_name);
}

void change_win_tab_name(char *w_t_name){
    pos += snprintf(big + pos, sizeof(big) - pos, "\x1b]2;%sST", w_t_name);
}

// ===== エコーモード (POSIX termios) =====

void set_echo_mode(int enable){
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    if (enable) {
        t.c_lflag |= ECHO;
    } else {
        t.c_lflag &= ~ECHO;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

// ===== フラッシュ =====

void screen_push(){
    fwrite(big, 1, pos, stdout);
    fflush(stdout);
    term_init();
}

// ===== 内部ヘルパー =====

static int convert_eight_bit_rgb_color(enum eight_bit_rgb eight_bit_rgb){
    int color = 30;
    switch(eight_bit_rgb){
        case red:     color += MY_RED;     break;
        case green:   color += MY_GREEN;   break;
        case blue:    color += MY_BLUE;    break;
        case black:   color += MY_BLACK;   break;
        case yellow:  color += MY_YELLOW;  break;
        case magenta: color += MY_MAGENTA; break;
        case cyan:    color += MY_CYAN;    break;
        case white:   color += MY_WHITE;   break;
    }
    return color;
}

void get_terminal_size(int *width, int *height){
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width  = ws.ws_col;
        *height = ws.ws_row;
    } else {
        *width  = 80;
        *height = 24;
    }
}
