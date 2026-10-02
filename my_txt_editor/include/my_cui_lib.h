#ifndef MY_CUI_LIB_H
#define MY_CUI_LIB_H

// ===== 定数 (ANSIカラーオフセット) =====

#define MY_BLACK   0 // ANSI基本色の黒。
#define MY_RED     1 // ANSI基本色の赤。
#define MY_GREEN   2 // ANSI基本色の緑。
#define MY_YELLOW  3 // ANSI基本色の黄。
#define MY_BLUE    4 // ANSI基本色の青。
#define MY_MAGENTA 5 // ANSI基本色のマゼンタ。
#define MY_CYAN    6 // ANSI基本色のシアン。
#define MY_WHITE   7 // ANSI基本色の白。

// ===== 型定義 =====

// 0〜255の各成分で表す24bit RGB色。
struct rgb {
    int r; // 赤成分。
    int g; // 緑成分。
    int b; // 青成分。
};

// ANSIの8基本色。列挙値は対応する色番号と一致する。
enum eight_bit_rgb {
    black, // 黒。
    red, // 赤。
    green, // 緑。
    yellow, // 黄。
    blue, // 青。
    magenta, // マゼンタ。
    cyan, // シアン。
    white // 白。
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
