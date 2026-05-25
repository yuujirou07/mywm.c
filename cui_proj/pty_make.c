#include "raylib.h"
#include <asm-generic/errno-base.h>
#include <bits/types/siginfo_t.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include<sys/epoll.h>
#include<errno.h>
#include<sys/wait.h>

#define ESC_PAL_MAX 32
#define cur_font_load_max 32
#define EVENT_WAIT_MAX 16
#define DEFAULT_SCREEN_SIZE_W 500
#define DEFAULT_SCREEN_SIZE_H 500

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

enum now_str_ovf{
  Y,
  N
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
  struct pos term_size;
  struct pos temp_cur_pos;
  struct line_info *lines;

  int *palms;
  int *palms_counter;
  int term_cell_alloc_size;
  int total_cells;
  int master_fd;
  bool paste_mode;
  bool insert_mode;
  char *abs_path_name;

  struct {//bash_str_parse関数で使用する変数
    enum parse_state state;
    enum mode_state mode;
    enum osc_state osc_state;

    Color now_fg_color;
    Color now_bg_color;

    int osc_pal_chr_counter;
    int val;

    bool is_private;
    bool has_val;

    char osc_pal_chr[513];
  }bash_parser_required_memb;
};


Color conbert_num_to_color(char *color_str,int mode);
struct return_binary *char_conbert_binary_arry(char *osc_pal_chr);
struct csi_data csi_mode_pal_parse(char *buff, int *i, int size);

enum mode_state get_mode(char *buff, int *i, int size);
enum visiavle_chr check_visible_chr(char buff);
enum parse_state buff_state_check(char buff, enum parse_state now_state);

void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell,int term_cell_alloc_size);
void unicode_utf8_encoder(char *utf8,int unicode, int *len);
void window_resized_update_memb(struct pos *screen_size,struct pos *term_size,struct term_context *ctx);
void error_log_write(char *error_statement);
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
void cur_mgr_free(struct cur_mgr *cur_mgr);
void draw_cursor(struct cursor *cur, double *current_time, struct term_context *ctx);
void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx);
void csi_mode_parse(char *buff, int *i, int size);
void load_cur_font(struct cur_mgr *cur_mgr);

char *base64_decoder(char *osc_pal_chr);
char ** split_line(int cols, char *buff_str);
char *mymemcpy(char *start, char*end, enum last_chr_mode mode);
char input_bash(char *n);

int init_cur_mgr(struct cur_mgr *cur_mgr);
int check_key();

bool IsAnyKeyReleased(void);



int main(void) {
  int master_fd, slave_fd;
  int str_start_pos_x = 0; //文字の表示開始座標X
  int total;

  struct pos screen_pixel;
  struct pos term_size;
  struct termios term;
  struct termios *term_ptr = NULL;
  struct winsize ws;

  char slavename[256];

  screen_pixel.h=DEFAULT_SCREEN_SIZE_H;
  screen_pixel.w=DEFAULT_SCREEN_SIZE_W;
  term_size.w = (int)(screen_pixel.w - str_start_pos_x) / 8;
  term_size.h = screen_pixel.h / 16;
  total =  term_size.w*term_size.h;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);  

  int current_monitor = GetCurrentMonitor();
  int current_moniter_refreshrate = GetMonitorRefreshRate(current_monitor);

  InitWindow(screen_pixel.w, screen_pixel.h, "bash");
  SetTargetFPS(current_moniter_refreshrate);
  if (!IsWindowReady()) {
    printf("window error");
    return 0;
  }
  //Font myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf", 256, NULL, 0);
  Font myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf", 256, NULL, 0);
  SetTextureFilter(myfont.texture, TEXTURE_FILTER_POINT);
  
  // 元のターミナルの設定をコピーし、安全なウィンドウサイズを指定すru
  ws.ws_col = term_size.w; // 横幅 (壁の位置)
  ws.ws_row = term_size.h; // 縦幅
  ws.ws_xpixel=(unsigned short)screen_pixel.w;//横ピクセル
  ws.ws_ypixel=(unsigned short)screen_pixel.h;//縦ピクセル

  if (tcgetattr(STDIN_FILENO, &term) != -1) {
    term_ptr = &term;
    term.c_oflag |= OPOST;   // ポストプロセスを有効化
    term.c_oflag |= ONLCR;   // \n を \r\n に変換す
  }

  if (openpty(&master_fd, &slave_fd, slavename, term_ptr,&ws) == -1) {
    printf("pty error");
    exit(EXIT_FAILURE);
  }

  if(tcsetattr(slave_fd, TCSANOW, &term)!=0){ // 設定を即時反映
    char error_log[128];
    snprintf(error_log,sizeof(char)*128,"tcsetattr error log=%d: %s\n",errno,strerror(errno));
    error_log_write(error_log);
    return 1;
  }

  pid_t pid_id = fork();
  if (pid_id == -1) {
    error_log_write("fork faild code 186");
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
    setenv("LANG", "en_US.UTF-8", 1);
    execlp("bash", "bash", "-i", NULL);

    error_log_write("bash lunch afaild");
    exit(EXIT_FAILURE);
  }
  else {
    close(slave_fd); // 親プロセス（マスター側）では slave_fd は不要なので即座に閉じる
  }
  //現在のフラグを取得する
  int flags=fcntl(master_fd,F_GETFL,0);

  if(flags!=-1){
  //readを非ブロッキングにする
    fcntl(master_fd, F_SETFL,flags|O_NONBLOCK);
  }

  //epoll設定////////////
  int epoll_fd_list=epoll_create(EVENT_WAIT_MAX);
  if(epoll_fd_list<0){
    error_log_write("epoll create error code");
    return 1;
  }
  struct epoll_event master_fd_ev_poll;
  struct epoll_event epoll_list[EVENT_WAIT_MAX];
  memset(&master_fd_ev_poll,0,sizeof(master_fd_ev_poll));
  //読み込み監視
  master_fd_ev_poll.events=EPOLLIN;

  master_fd_ev_poll.data.ptr=malloc(sizeof(struct clientinfo));

  if(master_fd_ev_poll.data.ptr==NULL){
    error_log_write("e_ev.data.ptr malloc error");
    return 1;
  }
  memset(master_fd_ev_poll.data.ptr,0,sizeof(struct clientinfo));

  ((struct clientinfo *)master_fd_ev_poll.data.ptr)->fd=master_fd;

  if(epoll_ctl(epoll_fd_list,EPOLL_CTL_ADD,master_fd,&master_fd_ev_poll)!=0){
    error_log_write("epoll_ctl faild code");
    return 1;
  }
  // 変数の初期化
  int term_cell_alloc_size=total*4;
  int result=0;

  struct cur_mgr *cur_mg = NULL; 
  struct term_context ctx;
  struct term_cell *temp_term_cell = NULL; 
  struct line_info *lines = NULL;

  double current_time = 0;
  char *read_buf = NULL;
  const char* clip_bord_chr=NULL;
  bool write_buff_overflow=false;
  
  read_buf      =malloc(term_cell_alloc_size);
  temp_term_cell=calloc(term_cell_alloc_size,sizeof(struct term_cell));
  lines         =calloc(term_size.h,sizeof(struct line_info));
  cur_mg        =calloc(1,sizeof(struct cur_mgr));

  // ctx構造体の直接初期化
  ctx.term_cell = calloc(term_cell_alloc_size, sizeof(struct term_cell));
  ctx.alt_term_cell = NULL;
  ctx.cur = malloc(sizeof(struct cursor));
  ctx.save_cur = malloc(sizeof(struct cursor));
  ctx.term_size = term_size;
  ctx.palms = malloc(sizeof(int) * 16);
  ctx.palms_counter = malloc(sizeof(int));
  *ctx.palms_counter = 0;
  ctx.paste_mode = false;
  ctx.abs_path_name = NULL;
  ctx.total_cells = total;
  ctx.insert_mode = false;
  ctx.lines = lines;
  ctx.master_fd = master_fd;

  // curソル初期化
  ctx.cur->shape = malloc(2);
  ctx.cur->lighting.blinking = true;
  ctx.cur->lighting.speed_ms = 500;
  ctx.cur->font = myfont;
  ctx.cur->lighting.now_right = 0;
  ctx.cur->cur_pos.w = 0;
  ctx.cur->cur_pos.h = 0;

  // bash_parser_required_memb初期化
  ctx.bash_parser_required_memb.state = GROUND;
  ctx.bash_parser_required_memb.mode = IDK;
  ctx.bash_parser_required_memb.osc_pal_chr_counter = 0;
  ctx.bash_parser_required_memb.val = 0;
  ctx.bash_parser_required_memb.is_private = false;
  ctx.bash_parser_required_memb.has_val = 0;
  ctx.bash_parser_required_memb.now_fg_color = WHITE;
  ctx.bash_parser_required_memb.now_bg_color = BLACK;
  ctx.bash_parser_required_memb.osc_state = NORMAL;

  result = init_cur_mgr(cur_mg);
  
  if (result == 1) {
    error_log_write("can not init cur_mgr code 245");
    return 0;
  }

  load_cur_font(cur_mg);
  cur_font_set(ctx.cur, cur_mg, 1);
  
  while (!WindowShouldClose()) {
    int nfds = epoll_wait(epoll_fd_list,epoll_list,EVENT_WAIT_MAX, 1);
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)){
      if(write_buff_overflow==true && nfds>0){
        for(int i=0;i<nfds;i++){
          //もしepoll_list[i]番目のfdがmaster_fdだったら
          if(((struct clientinfo*)epoll_list[i].data.ptr)->fd!=master_fd)continue;
          //もし書き込み可能かwriteの書き込みバッファが溢れていなかったら
          if(epoll_list[i].events & EPOLLOUT){
            if(clip_bord_chr == NULL && (clip_bord_chr = GetClipboardText()) == NULL) {
              break;
            }
          }
        }
      }
      else if(write_buff_overflow==false){
        if(clip_bord_chr == NULL && (clip_bord_chr = GetClipboardText()) == NULL) {
          break;
        }
      }
      else break;

      if(clip_bord_chr!=NULL){
        size_t len = strlen(clip_bord_chr);   
        if(len> 0) {
          char temp_clip_bord_chr[len+1];
          int temp_clip_bord_chr_counter=0;
          for (size_t i = 0; i < len; i++) {
            char c = (char)clip_bord_chr[i];
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
              temp_clip_bord_chr[temp_clip_bord_chr_counter++]=c;
            }
          }
          temp_clip_bord_chr[temp_clip_bord_chr_counter]='\0';
          //何バイト読み込んだか
          ssize_t now_fd_input_size=write(master_fd,temp_clip_bord_chr,strlen(temp_clip_bord_chr));
          //次のループで再度書き込む位置を保存しておきたいのでclipbord_chr変数から書き込んだバイト数のポインタを加算する
          if(now_fd_input_size>0){
            clip_bord_chr+=now_fd_input_size;
            //もし送られたバイト数がクリップボードbuffのデータより大きかったら初期化する
            if(len<=now_fd_input_size){
              clip_bord_chr=NULL;
            }
          }
          else if(now_fd_input_size==0){
            clip_bord_chr=NULL;
            write_buff_overflow=false;
            master_fd_ev_poll.events=EPOLLIN;
            if(epoll_ctl(epoll_fd_list,EPOLL_CTL_MOD ,master_fd,&master_fd_ev_poll)!=0){
              char err_buff[128];
              snprintf(err_buff,128,"epoll_ctl func error errno = %d",errno);
              error_log_write(err_buff);
            }
          }
          //バッファが入らなかった場合EPOLLOUTに変更する
          else if(now_fd_input_size==-1 && errno==EAGAIN){
            master_fd_ev_poll.events=EPOLLOUT;
            write_buff_overflow=true;
            if(epoll_ctl(epoll_fd_list,EPOLL_CTL_MOD ,master_fd,&master_fd_ev_poll)!=0){
              char err_buff[128];
              snprintf(err_buff,128,"epoll_ctl func error errno = %d",errno);
              error_log_write(err_buff);
            }
            break;
          }
          else {//エラーの場合
            error_log_write("write error");
            return 0;
          }
        }
      }
      while (GetCharPressed() > 0) {} //ショートカットキーのバッファがたまっているので捨てる   
    }
    else{ 
      int n = 0;
      while ((n = GetCharPressed()) > 0) {
        if (n < 32 || n == 127) continue; 
        else if (IsAnyKeyReleased()) break;
        char utf8[4]={0};
        int len=0;
        unicode_utf8_encoder(utf8,n,&len);
        write(master_fd,utf8, len);
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
        if(ctx.cur->cur_pos.w<ctx.temp_cur_pos.w+1 || ctx.term_cell[ctx.cur->cur_pos.h * ctx.term_size.w + ctx.cur->cur_pos.w + 1].character!=' '){
          write(master_fd,"\x1b[C",strlen("\x1b[C"));
        }
      }
    }
    //fnキー入力処理  
    if (IsKeyPressed(KEY_F1))  write(master_fd, "\x1bOP", 3);
    if (IsKeyPressed(KEY_F2))  write(master_fd, "\x1bOQ", 3);
    if (IsKeyPressed(KEY_F10)) write(master_fd, "\x1b[21~", 5);
    if (IsKeyPressed(KEY_LEFT)){
      write(master_fd,"\x1b[D",strlen("\x1b[D"));
    }
    if(nfds>0){
      for(int i=0;i<nfds;i++){
        //もしfdがmaster_fdだったら
        if(((struct clientinfo *)epoll_list[i].data.ptr)->fd==master_fd){
          if(epoll_list[i].events & EPOLLIN){
            while (1) {
              ssize_t buf_size = read(master_fd, read_buf, term_cell_alloc_size - 1);
              if (buf_size > 0){
                bash_str_parse(read_buf, buf_size, &ctx);
              }
              else if(buf_size==0){
                break;
              }
              else if (buf_size == -1) {
                // -1 の場合は errno を確認する
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // 受信バッファが空になったので、正常に読み取りループを抜ける
                  break;
                } else {
                  // それ以外の本当のエラー
                  error_log_write("read error");
                  break;
                }
              }
            }
            break;
          }
        }
      }
    }
    
  
    //リサイズ処理//////////////
    if(IsWindowResized()){
      struct pos old_term_size=term_size;
      int old_total=total;
      window_resized_update_memb(&screen_pixel,&term_size,&ctx);
  
      total=term_size.h*term_size.w;
      //ctxの同期
      ctx.term_size=term_size;
      ctx.total_cells=total;

      ws.ws_col = term_size.w;
      ws.ws_row = term_size.h;
      ws.ws_xpixel = screen_pixel.w;
      ws.ws_ypixel = screen_pixel.h;

      ioctl(master_fd, TIOCSWINSZ, &ws);
      // 子プロセスに SIGWINCH シグナルを送る
      kill(pid_id, SIGWINCH);
      //bashに通知
      if(total>term_cell_alloc_size){
        // 古い確保サイズを記憶しておく（read_bufのクリアに必要）
        int old_alloc_size = term_cell_alloc_size;

        //確実に二倍にして確保する
        while(total>term_cell_alloc_size){
          term_cell_alloc_size*=2;
        }
        char *read_buff_temp = calloc(term_cell_alloc_size,sizeof(char));
        struct term_cell *main_term_cell_temp = realloc(ctx.term_cell,sizeof(struct term_cell)*term_cell_alloc_size);
        //リサイズ時にterm_cellのバッファとして使う
        struct term_cell *temp_temp_term_cell=calloc(term_cell_alloc_size,sizeof(struct term_cell));
        struct term_cell *temp_alt_term_cell = calloc(term_cell_alloc_size,sizeof(struct term_cell));

        if(read_buff_temp==NULL || main_term_cell_temp==NULL || temp_temp_term_cell==NULL || temp_alt_term_cell==NULL){
          char buff[128];
          snprintf(buff,128,"read buff or main_term_cell_temp realloc error code=%d\n",errno);
          error_log_write(buff);
          free(read_buf);
          return 1;
        }

        memcpy(read_buff_temp,read_buf,old_alloc_size);
        memset(read_buff_temp + old_alloc_size, 0, term_cell_alloc_size - old_alloc_size);

        ctx.term_cell=main_term_cell_temp;
        if(temp_term_cell!=NULL){
          free(temp_term_cell);
        }
        if(ctx.alt_term_cell!=NULL){
          free(ctx.alt_term_cell);
        }
        ctx.alt_term_cell=temp_alt_term_cell;
        temp_term_cell=temp_temp_term_cell;


        //term_cell拡張範囲の初期化
        for(int i=old_alloc_size;i<term_cell_alloc_size;i++){
          ctx.term_cell[i].bg_color=ctx.bash_parser_required_memb.now_bg_color;
          ctx.term_cell[i].fg_color=ctx.bash_parser_required_memb.now_fg_color;
          ctx.term_cell[i].character=' ';
          ctx.term_cell[i].is_bold=false;
          ctx.term_cell[i].is_real_chr=false;
        }

        free(read_buf);
        read_buf = read_buff_temp;
    
      }
      
      //リサイズ処理////////
      if(term_size.w != old_term_size.w || term_size.h != old_term_size.h){
        reflow_terminal_text(&ctx, old_term_size, &temp_term_cell,term_cell_alloc_size);
      }
      
    }
  

    

    BeginDrawing();
    ClearBackground(BLACK);
    for (int i = 0; i < term_size.h; i++){
      Color fg;
      Color bg;

      for (int j = 0; j < term_size.w; j++){
        int idx = i * term_size.w + j;
        int  c = ctx.term_cell[idx].character;
        if (c == 0) c = ' ';

        fg = ctx.term_cell[idx].fg_color.a == 0 ? WHITE : ctx.term_cell[idx].fg_color;
        bg = ctx.term_cell[idx].bg_color.a == 0 ? BLACK : ctx.term_cell[idx].bg_color;
        if (bg.r != 0 || bg.g != 0 || bg.b != 0) {
          DrawRectangle(j * 8, i * 16, 8, 16, bg);
        }
        DrawTextCodepoint(myfont,c, (Vector2){j * 8, i * 16}, 16, fg);
      }
    }
    draw_cursor(ctx.cur, &current_time, &ctx);
    EndDrawing();
  
  }
  // ctxのクリーンアップ
  if (ctx.term_cell) free(ctx.term_cell);
  if (ctx.alt_term_cell) free(ctx.alt_term_cell);
  if (ctx.cur) {
    if (ctx.cur->shape) free(ctx.cur->shape);
    free(ctx.cur);
  }
  if (ctx.save_cur) free(ctx.save_cur);
  if (ctx.palms) free(ctx.palms);
  if (ctx.palms_counter) free(ctx.palms_counter);
  if (read_buf) free(read_buf);
  
  free(master_fd_ev_poll.data.ptr);
  close(master_fd);
  CloseWindow();
  close(epoll_fd_list);
}

void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx) {

  for (int i = 0; i < size; i++) {
    if (ctx->bash_parser_required_memb.state == SQE_START) {
      if (ctx->bash_parser_required_memb.mode == IDK) {
        ctx->bash_parser_required_memb.mode = get_mode(buff, &i, size);
        continue; 
      }
      switch (ctx->bash_parser_required_memb.mode) {
        case OSC_MODE:
            if (buff[i] == '\x1b') ctx->bash_parser_required_memb.osc_state = OSC_EXPECT_ST;
            else if (buff[i] == '\a' || (ctx->bash_parser_required_memb.osc_state == OSC_EXPECT_ST && buff[i] == '\\')) {
              if (ctx->bash_parser_required_memb.has_val) {
                    if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = ctx->bash_parser_required_memb.val;
                } else if (*(ctx->palms_counter) == 0) {
                    ctx->palms[0] = 0;
                    *(ctx->palms_counter) = 1;
              }
              ctx->bash_parser_required_memb.osc_pal_chr[ctx->bash_parser_required_memb.osc_pal_chr_counter] = '\0';
              
              osc_mode(buff, ctx, ctx->bash_parser_required_memb.osc_pal_chr);
              
              ctx->bash_parser_required_memb.osc_pal_chr_counter = 0;
              *ctx->palms_counter = 0;
              ctx->bash_parser_required_memb.val = 0;
              ctx->bash_parser_required_memb.has_val = false;
              ctx->bash_parser_required_memb.state = GROUND;
              ctx->bash_parser_required_memb.mode = IDK;
              ctx->bash_parser_required_memb.osc_state = NORMAL;

            } else if (*(ctx->palms_counter) == 0 && buff[i] >= '0' && buff[i] <= '9') {
                ctx->bash_parser_required_memb.val = ctx->bash_parser_required_memb.val * 10 + (buff[i] - '0');
                ctx->bash_parser_required_memb.has_val = true;
            } else if (*(ctx->palms_counter) == 0 && buff[i] == ';') {
                if (!ctx->bash_parser_required_memb.has_val) ctx->bash_parser_required_memb.val = 0;
                if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = ctx->bash_parser_required_memb.val;
                if (ctx->bash_parser_required_memb.osc_pal_chr_counter < sizeof(ctx->bash_parser_required_memb.osc_pal_chr) - 1) {
                  ctx->bash_parser_required_memb.osc_pal_chr[ctx->bash_parser_required_memb.osc_pal_chr_counter++] = buff[i];
                }
                ctx->bash_parser_required_memb.val = 0;
                ctx->bash_parser_required_memb.has_val = false;     
            } else if (buff[i] >= 0x20 && buff[i] <= 0x7E) {
              if (ctx->bash_parser_required_memb.osc_pal_chr_counter < sizeof(ctx->bash_parser_required_memb.osc_pal_chr) - 1){
                ctx->bash_parser_required_memb.osc_pal_chr[ctx->bash_parser_required_memb.osc_pal_chr_counter++] = buff[i];
              }
            }
            continue;
            break;
        case CSI_MODE:
            if (buff[i] >= '0' && buff[i] <= '9') {
                ctx->bash_parser_required_memb.val = ctx->bash_parser_required_memb.val * 10 + (buff[i] - '0');
                ctx->bash_parser_required_memb.has_val = true;

            } else if (buff[i] == ';') {
                if (!ctx->bash_parser_required_memb.has_val) ctx->bash_parser_required_memb.val = 0;
                if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] =ctx->bash_parser_required_memb.val;
                ctx->bash_parser_required_memb.val = 0;
                ctx->bash_parser_required_memb.has_val = false;

            } else if (buff[i] == '?') ctx->bash_parser_required_memb.is_private = true;

            else if (buff[i] >= 0x40 && buff[i] <= 0x7E) {
                if (ctx->bash_parser_required_memb.has_val) {
                    if (*(ctx->palms_counter) < 16) ctx->palms[(*(ctx->palms_counter))++] = ctx->bash_parser_required_memb.val;
                } else if (*(ctx->palms_counter) == 0) {
                    ctx->palms[0] = 0;
                    *(ctx->palms_counter) = 1;
                }
          
                ls_chr_parse(ctx, buff[i],
                  &ctx->bash_parser_required_memb.now_fg_color,
                  &ctx->bash_parser_required_memb.now_bg_color,
                  ctx->bash_parser_required_memb.is_private);
                
                *ctx->palms_counter = 0;
                ctx->bash_parser_required_memb.val = 0;
                ctx->bash_parser_required_memb.has_val = false;
                ctx->bash_parser_required_memb.state = GROUND;
                ctx->bash_parser_required_memb.mode = IDK;
                ctx->bash_parser_required_memb.is_private = false;
            }
            continue;
            break;
            
        case IDK:
            ctx->bash_parser_required_memb.state = GROUND;
            ctx->bash_parser_required_memb.mode = IDK;
            continue;
            break;
      }
    }
    else if (ctx->bash_parser_required_memb.state == GROUND) {
      ctx->bash_parser_required_memb.state = buff_state_check(buff[i],ctx->bash_parser_required_memb.state);
      if (ctx->bash_parser_required_memb.state == SQE_START) continue;
      if (buff[i] == '\b') {
          if (ctx->cur->cur_pos.w > 0) ctx->cur->cur_pos.w--;
          continue;
      } else if (buff[i] == '\r') {
          ctx->cur->cur_pos.w = 0;
          continue;
      } else if (buff[i] == '\n') {
          if (ctx->cur->cur_pos.h >= 0 && ctx->cur->cur_pos.h < ctx->term_size.h) {
              ctx->lines[ctx->cur->cur_pos.h].is_wrapped = false;
          }
          ctx->cur->cur_pos.w = 0;
          ctx->cur->cur_pos.h++;
      } else if (buff[i] == '\a') {
          continue;
      } else {
        if (ctx->insert_mode) {
            char_arry_insert_chr(ctx, 1);
        }
        // 次の文字を描画する直前に、カーソルが画面端に達していたら改行する（Delayed Wrap）
        if (ctx->cur->cur_pos.w >= ctx->term_size.w) {
            if (ctx->cur->cur_pos.h >= 0 && ctx->cur->cur_pos.h < ctx->term_size.h) {
                ctx->lines[ctx->cur->cur_pos.h].is_wrapped = true;
            }
            ctx->cur->cur_pos.w = 0;
            ctx->cur->cur_pos.h++;
            
            if (ctx->cur->cur_pos.h >= ctx->term_size.h) {
                memmove(ctx->term_cell, ctx->term_cell + ctx->term_size.w, (ctx->total_cells - ctx->term_size.w) * sizeof(struct term_cell));
                memmove(ctx->lines, ctx->lines + 1, (ctx->term_size.h - 1) * sizeof(struct line_info));
                ctx->lines[ctx->term_size.h - 1].is_wrapped = false;
                for (int c = 0; c < ctx->term_size.w; c++) {
                  int last_line_idx = (ctx->term_size.h - 1) * ctx->term_size.w + c;
                  ctx->term_cell[last_line_idx].character = ' ';
                  ctx->term_cell[last_line_idx].is_real_chr = false;
                }
                ctx->cur->cur_pos.h = ctx->term_size.h - 1;
            }
        }
        
        //可視文字処理
        int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        if (idx >= 0 && idx < ctx->term_size.h * ctx->term_size.w) {
            ctx->term_cell[idx].character = buff[i];
            ctx->term_cell[idx].fg_color = ctx->bash_parser_required_memb.now_fg_color;
            ctx->term_cell[idx].bg_color = ctx->bash_parser_required_memb.now_bg_color;
            ctx->term_cell[idx].is_real_chr = true;
            //カーソル移動可能範囲更新
            ctx->temp_cur_pos.h=ctx->cur->cur_pos.h;
            ctx->temp_cur_pos.w=ctx->cur->cur_pos.w;

            ctx->cur->cur_pos.w++;
        }
      }
      // 画面外スクロール処理
      if (ctx->cur->cur_pos.h >= ctx->term_size.h) {
        int total_cells = ctx->total_cells;
        memmove(ctx->term_cell, ctx->term_cell + ctx->term_size.w, (total_cells - ctx->term_size.w) * sizeof(struct term_cell));
        memmove(ctx->lines, ctx->lines + 1, (ctx->term_size.h - 1) * sizeof(struct line_info));
        ctx->lines[ctx->term_size.h - 1].is_wrapped = false;
        for (int c = 0; c < ctx->term_size.w; c++) {
          int last_line_idx = (ctx->term_size.h - 1) * ctx->term_size.w + c;
          ctx->term_cell[last_line_idx].character = ' ';
          ctx->term_cell[last_line_idx].bg_color = BLACK;
          ctx->term_cell[last_line_idx].fg_color = WHITE;
          ctx->term_cell[last_line_idx].is_real_chr = false;
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
    // SGR: 色や表示属性（リセットや文字色/背景色）を設定する
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
    // カーソル位置を行/列で指定して移動する (行,列)
    case 'H':
    case 'f':
    {
      int row = (palms_counter > 0 && palms[0] > 0) ? palms[0] - 1 : 0;
      int col = (palms_counter > 1 && palms[1] > 0) ? palms[1] - 1 : 0;
      if (row >= ctx->term_size.h) {
        row = ctx->term_size.h - 1;
      }
      if (col >= ctx->term_size.w){
        col = ctx->term_size.w - 1;
      }
      ctx->cur->cur_pos.h = row;
      ctx->cur->cur_pos.w = col;
      break;
    }
    // 端末画面の消去（モード2などで全体クリア）
    case 'J':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      if (mode == 2) {
        for (int i = 0; i < ctx->term_size.h * ctx->term_size.w; i++){
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
          ctx->term_cell[i].is_real_chr = false;
        }
        for (int i = 0; i < ctx->term_size.h; i++) {
          ctx->lines[i].is_wrapped = false;
        }
      } else if (mode == 0) { // カーソル位置から画面の最後までを消去
        int start_idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        if (start_idx < 0) start_idx = 0;
        for (int i = start_idx; i < ctx->term_size.h * ctx->term_size.w; i++) {
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
          ctx->term_cell[i].is_real_chr = false;
        }
        for (int i = ctx->cur->cur_pos.h; i < ctx->term_size.h; i++) {
          ctx->lines[i].is_wrapped = false;
        }
      } else if (mode == 1) { // 画面の最初からカーソル位置までを消去
        int end_idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        for (int i = 0; i <= end_idx && i < ctx->term_size.h * ctx->term_size.w; i++) {
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
          ctx->term_cell[i].is_real_chr = false;
        }
        for (int i = 0; i <= ctx->cur->cur_pos.h && i < ctx->term_size.h; i++) {
          ctx->lines[i].is_wrapped = false;
        }
      }
      break;
    }
    // 行の消去（モードにより行末、行頭から末尾、または全行を消す）
    case 'K':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      if (mode == 2) {
        int line_start = ctx->cur->cur_pos.h * ctx->term_size.w;
        for (int i = 0; i < ctx->term_size.w; i++){
          ctx->term_cell[line_start + i].character = ' ';
          ctx->term_cell[line_start + i].bg_color = *now_bg_color;
          ctx->term_cell[line_start + i].is_real_chr = false;
        }
        ctx->lines[ctx->cur->cur_pos.h].is_wrapped = false;
      } else if (mode == 0) {
        int start = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        int end = (ctx->cur->cur_pos.h + 1) * ctx->term_size.w;
        for (int i = start; i < end; i++) {
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
          ctx->term_cell[i].is_real_chr = false;
        }
        ctx->lines[ctx->cur->cur_pos.h].is_wrapped = false;
      } else if (mode == 1) { // 行頭からカーソル位置までを消去
        int start = ctx->cur->cur_pos.h * ctx->term_size.w;
        int end = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
        for (int i = start; i <= end && i < ctx->term_size.h * ctx->term_size.w; i++) {
          ctx->term_cell[i].character = ' ';
          ctx->term_cell[i].bg_color = *now_bg_color;
          ctx->term_cell[i].is_real_chr = false;
        }
      }
      break;
    }
    // カーソルを上に移動（パラメータ n 回分）
    case 'A':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.h -= n;
      if (ctx->cur->cur_pos.h < 0) ctx->cur->cur_pos.h = 0;
      break;
    }
    // カーソルを下に移動（パラメータ n 回分）
    case 'B':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.h += n;
      if (ctx->cur->cur_pos.h >= ctx->term_size.h) ctx->cur->cur_pos.h = ctx->term_size.h - 1;
      break;
    }
    // カーソルを右に移動（パラメータ n 回分）
    case 'C':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.w += n;
      if (ctx->cur->cur_pos.w >= ctx->term_size.w) ctx->cur->cur_pos.w = ctx->term_size.w - 1;
      break;
    }
    // カーソルを左に移動（パラメータ n 回分）
    case 'D':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      ctx->cur->cur_pos.w -= n;
      if (ctx->cur->cur_pos.w < 0) ctx->cur->cur_pos.w = 0;
      break;
    }
    // 指定数の文字を削除 (DCH: delete characters)
    case 'P':
      {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      char_array_alignment(ctx,n);
      break;
      }
    // 指定数の文字を空白で上書き（置換）する
    case 'x':
      {
        int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
        erase_chr(ctx,n);
        break;
      }
    // モードのセット/リセット（private でカーソル点滅や代替バッファ等を制御）
    case 'h':
    case 'l':
      bool is_on = (buff == 'h'); 
      if (is_private && palms_counter > 0) {
        switch (palms[0]) {
          case 25:
            ctx->cur->lighting.blinking = is_on; 
            break;
          case 1049:
            if (is_on) {
              *(ctx->save_cur) = *(ctx->cur);
              if (ctx->alt_term_cell != NULL) free(ctx->alt_term_cell);     
              ctx->alt_term_cell = malloc(sizeof(struct term_cell) * (ctx->total_cells));         
              memcpy(ctx->alt_term_cell, ctx->term_cell, ctx->term_size.h * ctx->term_size.w * sizeof(struct term_cell));
              for (int i = 0; i < (ctx->term_size.h * ctx->term_size.w); i++) {
                ctx->term_cell[i].character = ' ';
                ctx->term_cell[i].fg_color = WHITE;
                ctx->term_cell[i].bg_color = BLACK;
                ctx->term_cell[i].is_real_chr = false;
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
      }else if (!is_private && palms_counter > 0) {
        if (palms[0] == 4) { // 4番: インサートモード (IRM)
          ctx->insert_mode = is_on;
        }
      } 
      break;
    // 指定数分の空白を挿入（ICH: insert characters）
    case '@':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      char_arry_insert_chr(ctx,n);      
      break;
    }
    // Device Status Report (Bash等のカーソル位置問い合わせに対する応答)
    case 'n':
    {
      if (palms_counter > 0 && palms[0] == 6) {
        char response[32];
        int len = snprintf(response, sizeof(response), "\x1b[%d;%dR", ctx->cur->cur_pos.h + 1, ctx->cur->cur_pos.w + 1);
        write(ctx->master_fd, response, len);
      }
      break;
    }
    }
}

void osc_mode(char *buff, struct term_context *ctx, char *osc_pal_chr){
  switch(ctx->palms[0]){
    // OSC 0/1/2: ウィンドウタイトルなどの設定（プロセス/ウィンドウ名を反映）
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
    // OSC 7: カレントディレクトリ(file://)情報を取得して保存
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
    // OSC 8: ハイパーリンク操作（ここでは未処理/無視）
    case 8:
      break;
    // OSC 9 / 777: 拡張用途（未実装・無視）
    case 9:
    case 777:
      break;
    // OSC 10/11: フォア/バックグラウンド色を指定（rgb: や # 形式を処理）
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
      else result = rgb_result + strlen("rgb:");
      Color c_col=conbert_num_to_color(result,mode);
      if(ctx->palms[0] == 10) {
        change_fg_color(ctx,c_col); // 後述の関数
      }else{
        change_bg_color(ctx, c_col);
      }
      break;
     }
    // OSC 12: カーソル色等（未実装だがここで扱う想定）
    case 12:
      break;
    // OSC 52: base64 で渡されたデータを復号してクリップボードへ設定
    case 52:{
      char *decode_result = base64_decoder(osc_pal_chr);
      if (decode_result == NULL) break;
      SetClipboardText(decode_result);
      free(decode_result);
      break;
    }
    // その他の OSC: 未知のシーケンスは何もしない（無視）
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

void draw_cursor(struct cursor *cur, double *current_time, struct term_context *ctx){
  if (!cur->lighting.blinking) return;
  double now_time = GetTime();
  if (now_time - *current_time >= cur->lighting.speed_ms / 1000) {
    *current_time = now_time;
    cur->lighting.now_right = !cur->lighting.now_right;
  }
  if (cur->lighting.now_right) {
    int draw_w = cur->cur_pos.w >= ctx->term_size.w ? ctx->term_size.w - 1 : cur->cur_pos.w;
    DrawTextEx(
      cur->font,
      cur->shape,
      (Vector2){draw_w * 8, cur->cur_pos.h * 16},
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
  fclose(cur_load);
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
      char str_ptr[strlen(str_ptr_st)+1];
      strcpy(str_ptr, str_ptr_st + 1);
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
  ctx->bash_parser_required_memb.now_fg_color=c_col;
}
void change_bg_color(struct term_context *ctx,Color c_col){
  ctx->bash_parser_required_memb.now_fg_color=c_col;
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
    ctx->term_cell[idx + i].is_real_chr = false;
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
    ctx->term_cell[idx + i].is_real_chr = false;
  }
}
void erase_chr(struct term_context *ctx,int n){
  int loop = ctx->term_size.w - ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  for(int i=0;i<n;i++){
    ctx->term_cell[idx+i].character=' ';
    ctx->term_cell[idx+i].is_real_chr = false;
  }
}
void error_log_write(char *error_statement){
  FILE *error_f;
  error_f=fopen("error_log.txt","a");
  if(error_f==NULL){
    perror("cant open error_log.txt");
    exit(1);
  }
  ssize_t size=fputs(error_statement,error_f);
  if(size<0)perror("can not write on error_log.txt");
  fclose(error_f);
}
void window_resized_update_memb(struct pos *screen_pixel,struct pos *term_size,struct term_context *ctx){
  screen_pixel->w=GetScreenWidth();
  screen_pixel->h=GetScreenHeight();
  
  term_size->w = (int)(screen_pixel->w) / 8;
  term_size->h = (int)screen_pixel->h / 16;
  if (term_size->w <= 0) term_size->w = 1;
  if (term_size->h <= 0) term_size->h = 1;
}

void unicode_utf8_encoder(char *utf8,int unicode, int *len){   
  int n=unicode; 
    if (n <= 0x7F) {
        utf8[0] = (char)n;
        *len = 1;
    } else if (n <= 0x7FF) {
        utf8[0] = (char)(0xC0 | (n >> 6));
        utf8[1] = (char)(0x80 | (n & 0x3F));
        *len = 2;
    } else if (n <= 0xFFFF) {
        utf8[0] = (char)(0xE0 | (n >> 12));
        utf8[1] = (char)(0x80 | ((n >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (n & 0x3F));
        *len = 3;
    } else if (n <= 0x10FFFF) {
        utf8[0] = (char)(0xF0 | (n >> 18));
        utf8[1] = (char)(0x80 | ((n >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((n >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (n & 0x3F));
        *len = 4;
    }
} 

void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell_ptr,int term_cell_alloc_size) {

}
