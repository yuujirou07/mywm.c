#include "raylib.h"
#include <bits/types/siginfo_t.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define ESC_PAL_MAX 32
#define cur_font_load_max 32

struct pos {
  int w;
  int h;
};

struct cur_mgr {
  char *cur_font;
  int load_cur_font_n;
};

struct cursor {
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
    Font font;
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

// 1文字（セル）のデータ構造
struct term_cell {
    char character;  // 表示する文字（例: 'A'）
    Color fg_color;  // 前景色（文字色）
    Color bg_color;  // 背景色
    bool is_bold;    // 太字かどうか
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

// ★ 新しく追加：関数の引数をまとめる構造体
struct term_context {
    struct term_cell *term_cell;
    struct term_cell *alt_term_cell;
    struct cursor *cur;
    struct cursor *save_cur;
    struct pos term_size;
    struct pos temp_cur_pos;
    int *palms;
    int *palms_counter;
    bool paste_mode;
    char *abs_path_name;
    int total_cells;
    bool insert_mode;
};
void erase_chr(struct term_context *ctx,int n);
void char_arry_insert_chr(struct term_context *ctx,int n);
void char_array_alignment(struct term_context *ctx,int n);
Color conbert_num_to_color(char *color_str,int mode);
void change_bg_color(struct term_context *ctx,Color c_col);
void change_fg_color(struct term_context *ctx,Color c_col);
void conbert_chr_to_binary_table(struct return_binary *char_conbert_binary, char buff);
struct return_binary *char_conbert_binary_arry(char *osc_pal_chr);
char *base64_decoder(char *osc_pal_chr);
void osc_mode(char *buff, struct term_context *ctx, char *osc_pal_chr);
void ls_chr_parse(struct term_context *ctx, char buff, Color *now_fg_color, Color *now_bg_color, bool is_private);
struct csi_data csi_mode_pal_parse(char *buff, int *i, int size);
enum mode_state get_mode(char *buff, int *i, int size);
void csi_mode_parse(char *buff, int *i, int size);
enum visiavle_chr check_visible_chr(char buff);
void bs_st1(struct pos *pos, char *buff, int cols, int *buff_counter, unsigned int *bash_line_total_ciunt);
void cur_font_set(struct cursor *cur, struct cur_mgr *cur_mgr, int n);
void cur_set_default(struct cur_mgr *cur_mgr);
void cur_mgr_free(struct cur_mgr *cur_mgr);
int init_cur_mgr(struct cur_mgr *cur_mgr);
void load_cur_font(struct cur_mgr *cur_mgr);
bool IsAnyKeyReleased(void);
void draw_cursor(struct cursor *cur, double *current_time);
void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx);
char ** split_line(int cols, char *buff_str);
char *mymemcpy(char *start, char*end, enum last_chr_mode mode);
char input_bash(char *n);
int check_key();
enum parse_state buff_state_check(char buff, enum parse_state now_state);


int main(void) {
  int master_fd, slave_fd;
  char slavename[256];
  int scr_h = 500;
  int scr_w = 500;
  int str_start_pos_x = 3; //文字の表示開始座標X
  InitWindow(scr_w, scr_h, "bash");
  SetTargetFPS(120);
  if (!IsWindowReady()) {
    printf("window error");
    return 0;
  }
  SetTargetFPS(60);
  Font myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf", 256, NULL, 0);
  SetTextureFilter(myfont.texture, TEXTURE_FILTER_POINT);
  int cols = (int)(scr_w - str_start_pos_x) / 8;
  int rows = scr_h / 16;
  struct pos term_size=(struct pos){cols,rows};
  int total = cols * rows;
  
  // 元のターミナルの設定をコピーし、安全なウィンドウサイズを指定する
  struct termios term;
  struct termios *term_ptr = NULL;
  struct winsize ws;
  ws.ws_col = cols; // 横幅 (壁の位置)
  ws.ws_row = rows; // 縦幅

  if (openpty(&master_fd, &slave_fd, slavename, term_ptr, &ws) == -1) {
    printf("pty error");
    exit(EXIT_FAILURE);
  }
  if (tcgetattr(STDIN_FILENO, &term) != -1) {
      term_ptr = &term;
      term.c_oflag |= OPOST;   // ポストプロセスを有効化
      term.c_oflag |= ONLCR;   // \n を \r\n に変換す
  }
  tcsetattr(slave_fd, TCSANOW, &term); // 設定を即時反映
  pid_t pid_id = fork();
  if (pid_id == -1) {
    perror("forkに失敗しました");
        exit(EXIT_FAILURE);
  }
  else if (pid_id == 0) {
    close(master_fd);
    setsid();
    ioctl(slave_fd, TIOCSCTTY, 0);
    //ファイルディスクリプタのすり替え
    dup2(slave_fd, STDIN_FILENO);  // 標準入力 (0) を slave_fd に
    dup2(slave_fd, STDOUT_FILENO); // 標準出力 (1) を slave_fd に
    dup2(slave_fd, STDERR_FILENO); // 標準エラー (2) を slave_fd に
    // 繋ぎ変えが終わったら、元の slave_fd は不要なので閉じる
    close(slave_fd);
    setenv("TERM", "xterm-256color", 1);
    execlp("bash", "bash", "-i", NULL);

    perror("bashの起動に失敗しました");
    exit(EXIT_FAILURE);
  }
  else {
    close(slave_fd); // 親プロセス（マスター側）では slave_fd は不要なので即座に閉じる
  }
  
  //readを非ブロッキングにする
  fcntl(master_fd, F_SETFL, O_NONBLOCK);
  
  // 変数の初期化
  struct cursor cur;
  struct cursor save_cur;
  struct cur_mgr cur_mg;
  struct term_cell *main_term_cell = calloc(total, sizeof(struct term_cell));
  struct term_cell *alt_term_cell = NULL;
  Vector2 str_pos;
  int n = 0;
  int palms[16];
  int palms_counter = 0;
  double current_time = 0;
  char *read_buf = malloc(total);
  char *abs_path_name = NULL;
  bool paste_mode = false;
  
  str_pos.x = str_start_pos_x;
  str_pos.y = 0;
  cur.shape = malloc(2);
  cur.lighting.blinking = true;
  cur.lighting.speed_ms = 500;
  cur.font = myfont;
  cur.lighting.now_right = 0;
  cur.cur_pos.w = 0;
  cur.cur_pos.h = 0;
  
  int result = init_cur_mgr(&cur_mg);
  if (result == 1) {
    perror("can not init cur_mgr");
    return 0;
  }
  load_cur_font(&cur_mg);
  cur_font_set(&cur, &cur_mg, 1);

  struct term_context ctx;
  ctx.term_cell = main_term_cell;
  ctx.alt_term_cell = alt_term_cell;
  ctx.cur = &cur;
  ctx.save_cur = &save_cur;
  ctx.term_size = term_size;
  ctx.palms = palms;
  ctx.palms_counter = &palms_counter;
  ctx.paste_mode = paste_mode;
  ctx.abs_path_name = abs_path_name;
  ctx.total_cells = total;

  while (!WindowShouldClose()) {
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
      const char* clip_bord_chr = GetClipboardText();
      if (clip_bord_chr != NULL && strlen(clip_bord_chr) > 0) {   
        size_t len = strlen(clip_bord_chr);
        for (size_t i = 0; i < len; i++) {
          unsigned char c = (unsigned char)clip_bord_chr[i];
          if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
            write(master_fd, &c, 1);
          } else {
            break; 
          }
        }
      }
      while (GetCharPressed() > 0) {} //ショートカットキーのバッファがたまっているので捨てる
    }
    else {
      while ((n = GetCharPressed()) > 0) {
        if (n < 32 || n == 127) continue; 
        else if (IsAnyKeyReleased()) break;
        char c = n;
        write(master_fd, &c, 1);
      }
    }
    if (IsKeyPressed(KEY_ENTER)) {
      char enter_key = 13;
      write(master_fd, &enter_key, 1);
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
      char c = 0x7f; 
      write(master_fd, &c, 1);
    }
    if (IsKeyPressed(KEY_RIGHT)){
      //右のセルが空白ならカーソルをブロックする
      if(ctx.cur->cur_pos.w<ctx.term_size.w){
        if(ctx.cur->cur_pos.w<ctx.temp_cur_pos.w+1){
          write(master_fd,"\x1b[C",strlen("\x1b[C"));
        }
      }
    }
    if(IsKeyPressed(KEY_LEFT)){
      write(master_fd,"\x1b[D",strlen("\x1b[D"));
    }
    
    while (1) {
      ssize_t buf_size = read(master_fd, read_buf, total - 1);
      if (buf_size > 0) { 
        bash_str_parse(read_buf, buf_size, &ctx);
      }
      else break;
    }

    BeginDrawing();
    ClearBackground(BLACK);
    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        int idx = i * cols + j;
        char c = ctx.term_cell[idx].character;
        if (c == 0) c = ' '; // 未設定のセルはスペースとして描画
        char char_str[2] = { c, '\0' };
        Color fg = ctx.term_cell[idx].fg_color.a == 0 ? WHITE : ctx.term_cell[idx].fg_color;
        Color bg = ctx.term_cell[idx].bg_color.a == 0 ? BLACK : ctx.term_cell[idx].bg_color;
        if (bg.r != 0 || bg.g != 0 || bg.b != 0) {
           DrawRectangle(j * 8, i * 16, 8, 16, bg);
        }
        DrawTextEx(myfont, char_str, (Vector2){3 + j * 8, i * 16}, 16, 0, fg);
      }
    }
    draw_cursor(ctx.cur, &current_time);
    EndDrawing();
    fflush(stdout);
  }
  close(master_fd);
  CloseWindow();
}

// ★ 変更：構造体を受け取るように修正
void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx) {
    static enum parse_state state = GROUND;
    static enum mode_state mode = IDK;
    static char osc_pal_chr[513];
    static int osc_pal_chr_counter = 0;
    static int val = 0;
    static bool is_private = false;
    static bool has_val = 0;
    static Color now_fg_color = WHITE;
    static Color now_bg_color = BLACK;
    static enum osc_state osc_state = NORMAL;
    
    for (int i = 0; i < size; i++) {
      if (state == SQE_START) {
        if (mode == IDK) {
          mode = get_mode(buff, &i, size);
          continue; 
        }
        switch (mode) {
          case OSC_MODE:
              if (buff[i] >= '0' && buff[i] <= '9') {
                  val = val * 10 + (buff[i] - '0');
                  has_val = true;
              } else if (buff[i] == ';') {
                if (!has_val) val = 0;
                if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = val;
                osc_pal_chr[osc_pal_chr_counter++] = buff[i];
                val = 0;
                has_val = false;     
              } else if (buff[i] >= 0x40 && buff[i] <= 0x7E) osc_pal_chr[osc_pal_chr_counter++] = buff[i];
              else if (buff[i] == '\x1b') osc_state = OSC_EXPECT_ST;
              else if (buff[i] == '\a' || (osc_state == OSC_EXPECT_ST && buff[i] == '\\')) {
                if (has_val) {
                      if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = val;
                  } else if (*(ctx->palms_counter) == 0) {
                      ctx->palms[0] = 0;
                      *(ctx->palms_counter) = 1;
                }
                osc_pal_chr[osc_pal_chr_counter] = '\0';
                
                osc_mode(buff, ctx, osc_pal_chr);
                
                osc_pal_chr_counter = 0;
                *(ctx->palms_counter) = 0;
                val = 0;
                has_val = false;
                state = GROUND;
                mode = IDK;
              }
              continue;
              break;
          case CSI_MODE:
              if (buff[i] >= '0' && buff[i] <= '9') {
                  val = val * 10 + (buff[i] - '0');
                  has_val = true;
              } else if (buff[i] == ';') {
                  if (!has_val) val = 0;
                  if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = val;
                  val = 0;
                  has_val = false;
              } else if (buff[i] == '?') is_private = true;
              else if (buff[i] >= 0x40 && buff[i] <= 0x7E) {
                  if (has_val) {
                      if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = val;
                  } else if (*(ctx->palms_counter) == 0) {
                      ctx->palms[0] = 0;
                      *(ctx->palms_counter) = 1;
                  }
            
                  ls_chr_parse(ctx, buff[i], &now_fg_color, &now_bg_color, is_private);
                  
                  *(ctx->palms_counter) = 0;
                  val = 0;
                  has_val = false;
                  state = GROUND;
                  mode = IDK;
              }
              continue;
              break;
              
          case IDK:
              state = GROUND;
              mode = IDK;
              continue;
              break;
        }
      }
      else if (state == GROUND) {
        state = buff_state_check(buff[i], state);
        if (state == SQE_START) continue;
        if (buff[i] == '\b') {
            if (ctx->cur->cur_pos.w > 0) ctx->cur->cur_pos.w--;
            continue;
        } else if (buff[i] == '\r') {
            ctx->cur->cur_pos.w = 0;
            continue;
        } else if (buff[i] == '\n') {
            ctx->cur->cur_pos.w = 0;
            ctx->cur->cur_pos.h++;
        } else if (buff[i] == '\a') {
            continue;
        } else {
            if (ctx->insert_mode) {
                char_arry_insert_chr(ctx, 1);
            }
            int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
            if (idx >= 0 && idx < ctx->term_size.h * ctx->term_size.w) {
                ctx->term_cell[idx].character = buff[i];
                ctx->term_cell[idx].fg_color = now_fg_color;
                ctx->term_cell[idx].bg_color = now_bg_color;
                //カーソル移動可能範囲更新
                ctx->temp_cur_pos.h=ctx->cur->cur_pos.h;
                ctx->temp_cur_pos.w=ctx->cur->cur_pos.w;
            }
            ctx->cur->cur_pos.w++;
            if (ctx->cur->cur_pos.w >= ctx->term_size.w) {
                ctx->cur->cur_pos.w = 0;
                ctx->cur->cur_pos.h++;
            }
        }
        // 画面外スクロール処理
        if (ctx->cur->cur_pos.h >= ctx->term_size.h) {
            int total_cells = ctx->term_size.w * ctx->term_size.h;
            memmove(ctx->term_cell, ctx->term_cell + ctx->term_size.w, (total_cells - ctx->term_size.w) * sizeof(struct term_cell));
            for (int c = 0; c < ctx->term_size.w; c++) {
                int last_line_idx = (ctx->term_size.h - 1) * ctx->term_size.w + c;
                ctx->term_cell[last_line_idx].character = ' ';
                ctx->term_cell[last_line_idx].bg_color = BLACK;
                ctx->term_cell[last_line_idx].fg_color = WHITE;
            }
            ctx->cur->cur_pos.h = ctx->term_size.h - 1;
        }
      }
    }
}

void ls_chr_parse(struct term_context *ctx, char buff, Color *now_fg_color, Color *now_bg_color, bool is_private) {
  int palms_counter = *(ctx->palms_counter);
  int *palms = ctx->palms;

  switch(buff){
    case 'm': 
      for (int i = 0; i < palms_counter; i++) {
        int code = palms[i];
        if (code == 0) { 
          *now_fg_color = WHITE;
          *now_bg_color = BLACK;
        } else if (code >= 30 && code <= 37) { 
          Color colors[] = {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, SKYBLUE, WHITE};
          *now_fg_color = colors[code - 30];
        } else if (code >= 40 && code <= 47) { 
          Color colors[] = {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, SKYBLUE, WHITE};
          *now_bg_color = colors[code - 40];
        }
      }
      break;
    case 'H':
    case 'f':
    {
      int row = (palms_counter > 0 && palms[0] > 0) ? palms[0] - 1 : 0;
      int col = (palms_counter > 1 && palms[1] > 0) ? palms[1] - 1 : 0;
      if (row >= ctx->term_size.h) row = ctx->term_size.h - 1;
      if (col >= ctx->term_size.w) col = ctx->term_size.w - 1;
      
      ctx->cur->cur_pos.h = row;
      ctx->cur->cur_pos.w = col;
      break;
    }
    case 'J':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      if (mode == 2) {
        for (int i = 0; i < (ctx->term_size.h * ctx->term_size.w); i++){
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
        }
      }
      break;
    }
    case 'K':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      if (mode == 2) {
        int line_start = ctx->cur->cur_pos.h * ctx->term_size.w;
        for (int i = 0; i < ctx->term_size.w; i++){
          ctx->term_cell[line_start + i].character = ' ';
          ctx->term_cell[line_start + i].bg_color = *now_bg_color;
        }
      } else if (mode == 0) {
        int start = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        int end = (ctx->cur->cur_pos.h + 1) * ctx->term_size.w;
        for (int i = start; i < end; i++) {
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
        }
      }
      break;
    }
    case 'A':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.h -= n;
      if (ctx->cur->cur_pos.h < 0) ctx->cur->cur_pos.h = 0;
      break;
    }
    case 'B':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.h += n;
      if (ctx->cur->cur_pos.h >= ctx->term_size.h) ctx->cur->cur_pos.h = ctx->term_size.h - 1;
      break;
    }
    case 'C':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.w += n;
      if (ctx->cur->cur_pos.w >= ctx->term_size.w) ctx->cur->cur_pos.w = ctx->term_size.w - 1;
      break;
    }
    case 'D':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.w -= n;
      if (ctx->cur->cur_pos.w < 0) ctx->cur->cur_pos.w = 0;
      break;
    }
    case 'P':
      {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      char_array_alignment(ctx,n);
      break;
      }
    case 'x':
      {
        int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
        erase_chr(ctx,n);
      }
    case 'h':
    case 'l':
     if (is_private && palms_counter > 0) {
        bool is_on = (buff == 'h'); 
        switch (palms[0]) {
          case 4:
            if (!is_private && palms_counter > 0) {
              if (palms[0] == 4) { // 4番は インサートモード(IRM)
                bool is_on = (buff == 'h');
                ctx->insert_mode = is_on; 
              }
            }
          case 25:
            ctx->cur->lighting.blinking = is_on; 
            break;
          case 1049:
            if (is_on) {
              *(ctx->save_cur) = *(ctx->cur);
              if (ctx->alt_term_cell != NULL) free(ctx->alt_term_cell);     
              ctx->alt_term_cell = malloc(sizeof(struct term_cell) * (ctx->term_size.h * ctx->term_size.w));         
              memcpy(ctx->alt_term_cell, ctx->term_cell, ctx->term_size.h * ctx->term_size.w * sizeof(struct term_cell));
              for (int i = 0; i < (ctx->term_size.h * ctx->term_size.w); i++) {
                ctx->term_cell[i].character = ' ';
                ctx->term_cell[i].fg_color = WHITE;
                ctx->term_cell[i].bg_color = BLACK;
              }
            } else {
              if (ctx->alt_term_cell != NULL) {
                memcpy(ctx->term_cell, ctx->alt_term_cell, ctx->term_size.h * ctx->term_size.w * sizeof(struct term_cell));
                free(ctx->alt_term_cell);
                ctx->alt_term_cell = NULL; // 二重解放防止
                *(ctx->cur) = *(ctx->save_cur);
              } else {
                *(ctx->cur) = *(ctx->save_cur);
              }
            } 
            break;
            
          case 2004:
            if (is_on) ctx->paste_mode = true;
            else ctx->paste_mode = false;
            break;
          default:
            break;
        }
      }
      break;
    case '@':
      {
        int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
        char_array_alignment(ctx,n);      
        break;
    }
    default:
      break;
  }
}

// ★ 変更：構造体を受け取るように修正
void osc_mode(char *buff, struct term_context *ctx, char *osc_pal_chr){
  switch(ctx->palms[0]){
    case 0:
    case 1:
    case 2:
    {
      const char *new_win_title = strchr(osc_pal_chr, ';');
      if (new_win_title != NULL) {
        new_win_title = strchr(new_win_title, '~');
        if (new_win_title != NULL) {
          SetWindowTitle(new_win_title);
        }
      }
      break;
    }
    case 7:
      {
        free(ctx->abs_path_name);
        ctx->abs_path_name = NULL;
        char *str_result = strstr(osc_pal_chr, "file://");
        if (str_result != NULL) {
          str_result += strlen("file://");
          const char *path_start = strchr(str_result, '/');
          if (path_start != NULL) {
            ctx->abs_path_name = strdup(path_start);
          }
        }
        break;
      }
    case 8:
      break;
    case 9:
    case 777:
      break;
    case 10:
    case 11:
      {
      int mode=0;
      char *result=strchr(osc_pal_chr,';');
      if(result==NULL)break;
      char *rgb_result=strstr(result+1,"rgb:");
      if(rgb_result==NULL){
        result=strchr(result+1,'#');
        if(result==NULL)break;
        result++;
        mode++;
      }
      else result+=strlen("rgb:");
      Color c_col=conbert_num_to_color(result,mode);
      if(ctx->palms[0] == 10) {
        change_fg_color(ctx,c_col); // 後述の関数
      }else{
        change_bg_color(ctx, c_col);
      }
      break;
     }
    case 12:
      break;
    case 52:{
      char *decode_result = base64_decoder(osc_pal_chr);
      if (decode_result == NULL) break;
      SetClipboardText(decode_result);
      break;
    }
    default:
      break;
  }
}

char *mymemcpy(char *start, char *end, enum last_chr_mode mode){
  char *cpy;
  size_t len = end - start;

  switch(mode){
    case none:
      cpy = malloc(sizeof(char) * (len + 1)); 
      memcpy(cpy, start, len);
      cpy[len] = '\0';
      break;
    case line_down:
      cpy = malloc(sizeof(char) * (len + 2)); 
      memcpy(cpy, start, len);
      cpy[len] = '\n';     
      cpy[len + 1] = '\0';   
      break;
    case str_end:
      cpy = malloc(sizeof(char) * (len + 1));
      memcpy(cpy, start, len);
      cpy[len] = '\0';
      break;
  }
  return cpy;
}

void draw_cursor(struct cursor *cur, double *current_time){
  if (!cur->lighting.blinking) return;
  double now_time = GetTime();
  if (now_time - *current_time >= cur->lighting.speed_ms / 1000) {
    *current_time = now_time;
    cur->lighting.now_right = !cur->lighting.now_right;
  }
  if (cur->lighting.now_right) {
    DrawTextEx(
      cur->font,
      cur->shape,
      (Vector2){cur->cur_pos.w * 8, cur->cur_pos.h * 16},
      16, 
      0, 
      WHITE
    );
  }
}

bool IsAnyKeyReleased(void) {
    for (int i = 32; i <= 126; i++) if (IsKeyReleased(i)) return true;
    if (IsKeyReleased(KEY_ENTER) || IsKeyReleased(KEY_BACKSPACE)) return true;
    return false;
}

void load_cur_font(struct cur_mgr *cur_mgr){
  if (cur_mgr == NULL) {
    printf("cur_mgr is not init");
    exit(1);
  }
  FILE *cur_load = fopen("cur_font.txt", "r");
  if (cur_load == NULL) {
    cur_set_default(cur_mgr);
    return;
  }
  for (int i = 0; i < cur_font_load_max; i++){
    cur_mgr->load_cur_font_n = i;
    int in = fgetc(cur_load);
    if (in == EOF) break;
    char c = (char)in;
    cur_mgr->cur_font[i] = c;
  }
}

int init_cur_mgr(struct cur_mgr *cur_mgr){
  cur_mgr->cur_font = malloc(sizeof(char) * cur_font_load_max);
  if (cur_mgr->cur_font == NULL) {
    printf("cur_mgr malloc error");
    exit(1);
  }
  return 0;
}

void cur_mgr_free(struct cur_mgr *cur_mgr){
  free(cur_mgr);
}

void cur_set_default(struct cur_mgr *cur_mgr){
  cur_mgr->load_cur_font_n = 2;
  cur_mgr->cur_font[0] = '|';
  cur_mgr->cur_font[1] = '/';
}

void cur_font_set(struct cursor *cur, struct cur_mgr *cur_mgr, int n){
  if (n > cur_mgr->load_cur_font_n) {
    printf("your chose cur font nonber is big then cur_font_load_max");
    exit(1);
  }
  cur->shape[0] = cur_mgr->cur_font[n - 1];
  cur->shape[1] = '\0';
}

enum parse_state buff_state_check(char buff, enum parse_state now_state){
  enum parse_state return_state = now_state;
  if (buff == '\x1b' && return_state == GROUND) return_state = SQE_START;
  return return_state;
}

enum visiavle_chr check_visible_chr(char buff){
  enum visiavle_chr vis_state;
  if (buff == '\b') vis_state = BS_ST1;
  else if (buff == '\r') vis_state = NO;
  else vis_state = YES;
  return vis_state;
}

enum mode_state get_mode(char *buff, int *i, int size){
  enum mode_state return_state;
  if (buff[*i] == ']') return_state = OSC_MODE;
  else if (buff[*i] == '[') return_state = CSI_MODE;
  else return_state = IDK;
  return return_state;
}

char *base64_decoder(char *osc_pal_chr){
  char *converted_chr = NULL;
  char *result = strchr(osc_pal_chr, ';');
    if (result == NULL) return NULL;
    if (*(result + 1) == 'c') {
      char *str_ptr_st = strchr(result + 1, ';');
      if (str_ptr_st == NULL) return NULL;
      char str_ptr[strlen(str_ptr_st) + 1];
      memcpy(str_ptr, str_ptr_st + 1, strlen(str_ptr_st));
      str_ptr[strlen(str_ptr_st)] = '\0';
      struct return_binary *char_bin = char_conbert_binary_arry(str_ptr);
      if (char_bin != NULL) {
        int remainder = char_bin->char_binary_counter % 8;
        if (remainder != 0) {
          int add_bits = 8 - remainder;
          int *temp = realloc(char_bin->char_binary, sizeof(int) * (char_bin->char_binary_counter + (8 - (char_bin->char_binary_counter % 8))));
          if (temp == NULL) {
            free(char_bin->char_binary);
            free(char_bin);
            perror("char_bin realloc error");
            return NULL;
          }
          char_bin->char_binary = temp;
          for (int i = 0; i < add_bits; i++){
            char_bin->char_binary[char_bin->char_binary_counter + i] = 0;
          }
          char_bin->char_binary_counter += 8 - (char_bin->char_binary_counter % 8);
        }
        int final_len = char_bin->char_binary_counter / 8;
        converted_chr = malloc(sizeof(char) * ((char_bin->char_binary_counter / 8) + 1));
        for (int i = 0; i < char_bin->char_binary_counter / 8; i++){
          int total = 0;
          for (int c = 0; c < 8; c++){
            int bit = char_bin->char_binary[i * 8 + c];
            total = (total << 1) | bit;
          }
          converted_chr[i] = (char)total;
        }
        converted_chr[final_len] = '\0';
        free(char_bin->char_binary);
        free(char_bin);
      }
    }
    return converted_chr;
}

struct return_binary *char_conbert_binary_arry(char *osc_pal_chr){
  size_t len = strlen(osc_pal_chr);
  if (len == 0) return NULL;
  struct return_binary *char_conbert_binary = calloc(1, sizeof(struct return_binary));
  if (char_conbert_binary == NULL) return NULL;
  char_conbert_binary->char_binary = calloc(1, sizeof(int) * (len * 6));
  if (char_conbert_binary->char_binary == NULL) {
    free(char_conbert_binary);
    return NULL;
  }
  char_conbert_binary->char_binary_counter = 0;
  for (int i = 0; i < len; i++){
    conbert_chr_to_binary_table(char_conbert_binary, osc_pal_chr[i]);
  }
  return char_conbert_binary;
}

void conbert_chr_to_binary_table(struct return_binary *char_conbert_binary, char buff){
  const char base64_table[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const char *result = strchr(base64_table, buff);
  if (result != NULL) {
    int pos = result - base64_table;
    for (int i = 5; i >= 0; i--){
      if (pos % 2 == 0) {
        char_conbert_binary->char_binary[char_conbert_binary->char_binary_counter + i] = 0;
      }
      else char_conbert_binary->char_binary[char_conbert_binary->char_binary_counter + i] = 1;
      pos /= 2;
    }
    char_conbert_binary->char_binary_counter += 6;
  }
  return ;
}

void change_fg_color(struct term_context *ctx,Color c_col){
  for(int i=0;i<ctx->total_cells;i++){
    ctx->term_cell[i].fg_color=c_col;
  }
}
void change_bg_color(struct term_context *ctx,Color c_col){
  for(int i=0;i<ctx->total_cells;i++){
    ctx->term_cell[i].bg_color=c_col;
  }
}

Color conbert_num_to_color(char *color_str,int mode){
  Color target_color={0};
  int r = 0, g = 0, b = 0;
  if(mode==0){
    char hex_r[5] = {0}, hex_g[5] = {0}, hex_b[5] = {0};
    if(sscanf(color_str, "%4[^/]/%4[^/]/%4s", hex_r, hex_g, hex_b) == 3) {
        // 文字列の長さに応じて、最初の2桁(8ビット)だけを評価する
        sscanf(hex_r, "%02x", &r);
        sscanf(hex_g, "%02x", &g);
        sscanf(hex_b, "%02x", &b);
        
        target_color.r = r;
        target_color.g = g;
        target_color.b = b;
    }
  }
  else if(mode==1){
    if(sscanf(color_str, "#%02x%02x%02x", &r, &g, &b) == 3) {
      target_color.r = r;
      target_color.g = g;
      target_color.b = b;
    }
  }
  else target_color=WHITE;
  target_color.a = 255;
  return target_color;
}

void char_array_alignment(struct term_context *ctx,int n){
  int loop= ctx->term_size.w - ctx->cur->cur_pos.w;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  for(int i = 0; i < loop - n; i++){
    // 文字だけでなく、色情報なども含めて構造体ごとコピーする
    ctx->term_cell[idx + i] = ctx->term_cell[idx + i + n];
  }
  for(int i = loop - n; i < loop; i++){
    ctx->term_cell[idx + i].character = ' ';
  }
}
void char_arry_insert_chr(struct term_context *ctx,int n){
  int loop= ctx->term_size.w - ctx->cur->cur_pos.w;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  for(int i = loop - 1; i >= n; i--){
    ctx->term_cell[idx + i] = ctx->term_cell[idx + i - n];
  }
  for(int i=0;i<n;i++){
    ctx->term_cell[idx + i].character=' ';
  }
}
void erase_chr(struct term_context *ctx,int n){
  int loop = ctx->term_size.w - ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  for(int i=0;i<n;i++){
    ctx->term_cell[idx+i].character=' ';
  }
}