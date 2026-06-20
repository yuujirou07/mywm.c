#ifndef MY_CUI_LIB_H
#define MY_CUI_LIB_H

// ===== 定数 (ANSIカラーオフセット) =====

#define MY_BLACK   0
#define MY_RED     1
#define MY_GREEN   2
#define MY_YELLOW  3
#define MY_BLUE    4
#define MY_MAGENTA 5
#define MY_CYAN    6
#define MY_WHITE   7

// ===== 型定義 =====

struct rgb {
    int r;
    int g;
    int b;
};

enum eight_bit_rgb {
    black,
    red,
    green,
    yellow,
    blue,
    magenta,
    cyan,
    white
};

// ===== バッファ管理 =====

int  term_init(void);
void screen_push(void);

// ===== カーソル制御 =====

void move_cursor(int x, int y);
void reset_cursor(void);
void cursor_up(int count);
void cursor_down(int count);
void cursor_move_right(int count);
void cursor_move_left(int count);
void cursor_down_line_start(int count);
void cursor_up_line_start(int count);
void cursor_line_move(int count);
void rq_cursor_pos(void);
void save_cursor_pos(void);
void remove_cursor_pos(void);

// ===== 画面・行消去 =====

void erase_cursor_to_end_of_screen(void);
void erase_cursor_to_start_of_screen(void);
void erase_screen(void);
void erase_scroll_buff(void);
void erase_cursor_pos_to_line_end(void);
void erase_cursor_pos_to_line_start(void);
void erase_cursor_line(void);

// ===== 色・書式設定 =====

void change_str_rgb_foreground_color_start(enum eight_bit_rgb eight_bit_rgb);
void change_str_rgb_foreground_color_end(void);
void change_str_rgb_background_color_start(enum eight_bit_rgb eight_bit_rgb);
void change_str_rgb_background_color_end(void);
void change_str_rgb_true_foreground_color_start(struct rgb rgb);
void change_str_rgb_true_foreground_color_end(void);
void change_str_rgb_true_background_color_start(struct rgb rgb);
void change_str_rgb_true_background_color_end(void);

// ===== 文字出力 =====

void myprint(char *str);
void myprint_at(int x, int y, const char *str);

// ===== ターミナルモード =====

void hidden_cursor(void);
void show_cursor(void);
void change_buff_screen(void);
void back_forward_screen(void);
void report_focus_event(void);
void not_report_focus_event(void);
void on_bracket_paste_mode(void);
void off_bracket_paste_mode(void);
void set_echo_mode(int enable);

// ===== OSCコマンド =====

void change_window_name(char *w_name);
void change_win_tab_name(char *w_t_name);

// ===== ターミナルサイズ取得 =====

void get_terminal_size(int *width, int *height);

#endif /* MY_CUI_LIB_H */
