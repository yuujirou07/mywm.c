#ifndef PTY_MAKE_H
#define PTY_MAKE_H

#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include<vulkan/vulkan.h>

enum cur_allow_mode{
  AP_MODE,
  NORMAL_MODE
};

// raylibのColor型の代替（同じレイアウト・値で定義し、移行時の見た目を変えない）
typedef struct Color {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
} Color;

#define WHITE   (Color){255, 255, 255, 255}
#define BLACK   (Color){0, 0, 0, 255}
#define RED     (Color){230, 41, 55, 255}
#define GREEN   (Color){0, 228, 48, 255}
#define YELLOW  (Color){253, 249, 0, 255}
#define BLUE    (Color){0, 121, 241, 255}
#define MAGENTA (Color){255, 0, 255, 255}
#define SKYBLUE (Color){102, 191, 255, 255}

struct pos {
  int w;
  int h;
};

struct cur_mgr {
  char *cur_font;
  int load_cur_font_n;
};

struct cursor {
  enum cur_allow_mode allow_mode;

    char *shape;
    int color;
    struct {
      bool blinking;
      double speed_ms;
      bool now_right;
    } lighting;
    struct {
      int w;
      int h;
    } cur_pos;

    bool now_writing;
    double writing_st_time;
    double writing_end_time;


};

struct csi_data {
  int pal_count;
  char last_chr; 
};

enum last_chr_mode {
  line_down,
  str_end,
  none
};

enum parse_state {
  GROUND, //通常モード（届いた文字をそのまま画面に描画する)
  SQE_START,
};

enum mode_state {
  CSI_MODE, //引数を収集中	数字やセミコロンをメモリに溜める。
  OSC_MODE, //終端文字を受信	溜めた引数を使って、実際の命令（色変更など）を実行する。
  IDK
};

enum pal_p {
  st,
  ed
};

enum osc_state {
  OSC_EXPECT_ST,
  NORMAL
};

enum visiavle_chr {
  YES,
  NO,
  BS_ST1,
};

enum now_str_ovf{
  Y,
  N
};

enum repeat_key{
  ALPHABET,
  OTHER
};

struct line_info {
    bool is_wrapped; // true なら「上の行から自動で溢れてきた行」
};

// 1文字（セル）のデータ構造
struct term_cell {
    int character;  // 表示する文字（例: 'A'）
    Color fg_color;  // 前景色（文字色）
    Color bg_color;  // 背景色
    bool is_bold;    // 太字かどうか
    bool is_real_chr;
};

// ターミナルの「現在の状態」を保持する構造体
struct term_state {
    Color current_fg; // 今から打ち込まれる文字の色
    Color current_bg; // 今から打ち込まれる文字の背景色
    bool current_bold;
};

struct return_binary {
  int *char_binary;
  int char_binary_counter;
};

struct clientinfo {
    int fd;
    int n;
    int state;
    char buf[1024];
};

struct term_context {
  struct term_cell *term_cell;
  struct term_cell *alt_term_cell;
  struct cursor *cur;
  struct cursor *save_cur;
  struct line_info *lines;
  struct pos term_size;
  struct pos temp_cur_pos;
  struct pos home_pos;

  //DECSTBM - DEC Set Top and Bottom Margins
  // コマンドで使う構造体
  struct margin{
    int top_margin;
    int bottom_margin;
    bool decstbm_state;
  } fixrd_cur_scr_range;

  int *palms;
  int *palms_counter;
  int *term_cell_alloc_size;
  int total_cells;
  int master_fd;
  GLFWwindow *window; // クリップボード操作(glfwSetClipboardString等)に使用
  int cell_w;
  int cell_h;
  float display_scale;
  int render_scale;
  bool paste_mode;
  bool insert_mode;
  bool kbd_insert_mode;
  char *abs_path_name;


  struct {//bash_str_parse関数で使用する変数
    enum parse_state state;
    enum mode_state mode;
    enum osc_state osc_state;

    Color now_fg_color;
    Color now_bg_color;
    bool now_is_bold;
    bool now_is_reverse;

    int osc_pal_chr_counter;
    int val;

    bool is_private;
    bool has_val;
    bool g0_special_graphics;
    bool g1_special_graphics;
    bool use_g1_charset;

    char osc_pal_chr[513];
  }bash_parser_required_memb;
};

struct setting_data{
  double key_repeat_interval;//キーリーピートのタイミング
  double cursor_blink_restart_timeout_seconds;
};


Color conbert_num_to_color(char *color_str,int mode);
Color xterm_256color(int n);

struct return_binary *char_conbert_binary_arry(char *osc_pal_chr);
struct csi_data csi_mode_pal_parse(char *buff, int *i, int size);

enum parse_state buff_state_check(char buff, enum parse_state now_state);

void cur_allow_write(enum cur_allow_mode mode, int master_fd, int key_code);
void window_resized_update_memb(GLFWwindow *window, struct pos *screen_pixel, struct pos *term_size, struct term_context *ctx);
void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell, int term_cell_alloc_size);
void unicode_utf8_encoder(char *utf8,int unicode, int *len);
void erase_chr(struct term_context *ctx,int n);
void char_arry_insert_chr(struct term_context *ctx,int n);
void char_array_alignment(struct term_context *ctx,int n);
void change_bg_color(struct term_context *ctx,Color c_col);
void change_fg_color(struct term_context *ctx,Color c_col);
void conbert_chr_to_binary_table(struct return_binary *char_conbert_binary, char buff);
void osc_mode(char *buff, struct term_context *ctx, char *osc_pal_chr);
void ls_chr_parse(struct term_context *ctx, char buff, Color *now_fg_color, Color *now_bg_color, bool is_private);
void bs_st1(struct pos *pos, char *buff, int cols, int *buff_counter, unsigned int *bash_line_total_ciunt);
void cur_font_set(struct cursor *cur, struct cur_mgr *cur_mgr, int n);
void cur_set_default(struct cur_mgr *cur_mgr);
void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx);
void csi_mode_parse(char *buff, int *i, int size);
void load_cur_font(struct cur_mgr *cur_mgr);
void load_settings(struct setting_data *data);
void set_default_settings(struct setting_data *data);
void scroll_region_up(struct term_context *ctx);
void scroll_region_down(struct term_context *ctx);
void esc_single_dispatch(struct term_context *ctx, char c);

char *base64_decoder(char *osc_pal_chr);
char ** split_line(int cols, char *buff_str);

int init_cur_mgr(struct cur_mgr *cur_mgr);
int check_key();

bool ctl_c_sig_check(int *counter,int master_fd);
#endif
