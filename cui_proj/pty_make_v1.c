#include "raylib.h"
#include <asm-generic/errno-base.h>
#include <bits/types/siginfo_t.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/wait.h>

#define ESC_PAL_MAX 32
#define cur_font_load_max 32
#define EVENT_WAIT_MAX 16
#define DEFAULT_SCREEN_SIZE_W 500
#define DEFAULT_SCREEN_SIZE_H 500
#define DEFAULT_KEY_REPEAT_INTERVAL 0.5
#define DEFAULT_CUR_BLINK_RESTART_TIMEOUT_SEC 0.6
#define RENDER_SCALE 8
enum cur_allow_mode{
  AP_MODE,
  NORMAL_MODE
};


enum key_type{
  ALPBT_NUM,
  BS,
  LEFT_ALLOW,
  RIGHT_ALLOW,
  UP_ALLOW,
  DOWN_ALLOW,
  NONE
};

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
    Font font;

    
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

struct check_key{
  enum key_type key_type;
  //リピート判定用
  int check_key_repeat;
  // 有効配列
  int len;
  //キーコード
  char utf8[4];
  //キーのリピートタイミング
  double key_repeat_timing;

  bool  key_repeat_state;
};

struct key_repeat {

  double key_repeat_st;
  double key_repeat_ed;
  double key_repeat_interbal_st;
  double key_repeat_interbal_ed;
  double key_repeat_interval;

  struct check_key key_data;
  
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
  int cell_w;
  int cell_h;
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

    char osc_pal_chr[513];
  }bash_parser_required_memb;
};

struct setting_data{
  double key_repeat_interval;//キーリーピートのタイミング
  double cursor_blink_restart_timeout_seconds;
};


Color conbert_num_to_color(char *color_str,int mode);
Color xterm_256color(int n);
Font LoadPSF2Font(const char *filename);

struct return_binary *char_conbert_binary_arry(char *osc_pal_chr);
struct csi_data csi_mode_pal_parse(char *buff, int *i, int size);
struct term_cell *allocate_cell(struct term_cell* term_cell,int size);

enum mode_state get_mode(char *buff, int *i, int size);
enum visiavle_chr check_visible_chr(char buff);
enum parse_state buff_state_check(char buff, enum parse_state now_state);

void cur_allow_write(enum cur_allow_mode mode, int master_fd, int key_code);
void key_repeat(struct key_repeat *rp_key,int master_fd,struct setting_data setting_data,struct term_context *ctx);
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
void load_settings(struct setting_data *data);
void set_default_settings(struct setting_data *data);
void scroll_region_up(struct term_context *ctx);
void scroll_region_down(struct term_context *ctx);
void esc_single_dispatch(struct term_context *ctx, char c);

char *base64_decoder(char *osc_pal_chr);
char ** split_line(int cols, char *buff_str);
char *mymemcpy(char *start, char*end, enum last_chr_mode mode);
char input_bash(char *n);

int init_cur_mgr(struct cur_mgr *cur_mgr);
int check_key();

bool IsAnyKeyReleased(void);
bool ctl_c_sig_check(int *counter,int master_fd);


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

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);  

  int current_monitor = GetCurrentMonitor();
  int current_moniter_refreshrate = GetMonitorRefreshRate(current_monitor);

  InitWindow(screen_pixel.w, screen_pixel.h, "bash");

  SetTargetFPS(current_moniter_refreshrate);
  if (!IsWindowReady()) {
    printf("window error");
    return 0;
  }

  SetExitKey(KEY_NULL);
  ToggleFullscreen();

  screen_pixel.h = GetScreenHeight();
  screen_pixel.w = GetScreenWidth();


  // [AI生成] TTYと同じdefault8x16を優先ロード。失敗時は順にフォールバックする
  Font myfont = LoadPSF2Font("/usr/share/kbd/consolefonts/default8x16.psfu.gz");
  if (myfont.glyphs == NULL) {
    myfont = LoadPSF2Font("/usr/share/kbd/consolefonts/LatGrkCyr-12x22");
  }
  if (myfont.glyphs == NULL) {
    myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf", 16, NULL, 0);
  }
  SetTextureFilter(myfont.texture, TEXTURE_FILTER_POINT);

  // フォントの実グリフサイズをセル寸法として使う
  int cell_h = (int)((myfont.baseSize > 0 ? myfont.baseSize : 16) * 0.7f);
  int cell_w = (int)(((myfont.glyphCount > 0 && myfont.glyphs[0].advanceX > 0)
               ? myfont.glyphs[0].advanceX : 8) * 0.7f);
  if (cell_h < 1) cell_h = 1;
  if (cell_w < 1) cell_w = 1;


  term_size.w = (int)(screen_pixel.w - str_start_pos_x) / cell_w;
  term_size.h = screen_pixel.h / cell_h;
  total =  term_size.w*term_size.h;
  
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

  ((struct clientinfo *)master_fd_ev_poll.data.ptr)->fd = master_fd;

  if(epoll_ctl(epoll_fd_list,EPOLL_CTL_ADD,master_fd,&master_fd_ev_poll) != 0){
    error_log_write("epoll_ctl faild code");
    return 1;
  }
  // 変数の初期化
  int key_slash = 0;
  int term_cell_alloc_size=total*4;
  int result=0;
  int is_released_ctl_c = 0;

  struct cur_mgr *cur_mg = NULL; 
  struct term_context ctx;
  struct term_cell *temp_term_cell = NULL; 
  struct line_info *lines = NULL;
  struct setting_data setting_data;
  struct key_repeat key;
  struct pos old_term_cell_size = term_size;

  double current_time = 0;
  double last_resize_time = 0;
  RenderTexture2D render_tex = {0};
  char *read_buf = NULL;
  const char* clip_bord_chr=NULL;
  bool write_buff_overflow=false;
  
  read_buf      = malloc(term_cell_alloc_size);
  temp_term_cell= calloc(term_cell_alloc_size,sizeof(struct term_cell));
  lines         = calloc(term_size.h,sizeof(struct line_info));
  cur_mg        = calloc(1,sizeof(struct cur_mgr));

  key.key_data.key_type = NONE;

  // ctx構造体直接初期化
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
  ctx.term_cell_alloc_size = &term_cell_alloc_size;
  ctx.kbd_insert_mode = false;
  ctx.cell_w = cell_w;
  ctx.cell_h = cell_h;
  // [AI生成] RENDER_SCALE倍のオフスクリーンテクスチャを作成してスーパーサンプリングを行う
  ctx.render_scale = RENDER_SCALE;
  render_tex = LoadRenderTexture(screen_pixel.w * RENDER_SCALE, screen_pixel.h * RENDER_SCALE);
  SetTextureFilter(render_tex.texture, TEXTURE_FILTER_BILINEAR);

  // カーソル初期化
  ctx.cur->shape = malloc(2);
  ctx.cur->lighting.blinking = true;
  ctx.cur->lighting.speed_ms = 500;
  ctx.cur->font = myfont;
  ctx.cur->lighting.now_right = 0;
  ctx.cur->cur_pos.w = 0;
  ctx.cur->cur_pos.h = 0;
  ctx.cur->writing_st_time = 0;
  ctx.cur->writing_end_time = 0;
  ctx.cur->now_writing = false;
  ctx.home_pos = (struct pos){0.0};
  ctx.cur->allow_mode = NORMAL_MODE;


  // bash_parser_required_memb初期化
  ctx.bash_parser_required_memb.state = GROUND;
  ctx.bash_parser_required_memb.mode = IDK;
  ctx.bash_parser_required_memb.osc_pal_chr_counter = 0;
  ctx.bash_parser_required_memb.val = 0;
  ctx.bash_parser_required_memb.is_private = false;
  ctx.bash_parser_required_memb.has_val = 0;
  ctx.bash_parser_required_memb.now_fg_color = WHITE;
  ctx.bash_parser_required_memb.now_bg_color = BLACK;
  ctx.bash_parser_required_memb.now_is_bold = false;
  ctx.bash_parser_required_memb.now_is_reverse = false;
  ctx.bash_parser_required_memb.osc_state = NORMAL;
  ctx.cur->allow_mode = NORMAL_MODE;

  memset(&ctx.fixrd_cur_scr_range,0,sizeof(struct margin));
  memset (&key,0,sizeof(struct key_repeat));

  key.key_data.key_type = NONE;
  result = init_cur_mgr(cur_mg);
  load_settings(&setting_data);


  if (result == 1) {
    error_log_write("can not init cur_mgr code 245");
    return 0;
  }
  load_cur_font(cur_mg);
  cur_font_set(ctx.cur, cur_mg, 1);

  while (!WindowShouldClose())
  {
    int nfds = epoll_wait(epoll_fd_list,epoll_list,EVENT_WAIT_MAX, 1);

    //ペースト処理
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V))
    {
      // [AI生成] 前フレームで write() が EAGAIN を返した（カーネルの送信バッファが満杯）場合、
      // epoll を EPOLLOUT に切り替えて「空きができたら通知」を待っている。
      // 今フレームの epoll_wait で EPOLLOUT イベントが来た = 書き込み再開可能。
      if(write_buff_overflow==true && nfds>0)
      {
        for(int i=0;i<nfds;i++)
        {
          //もしepoll_list[i]番目のfdがmaster_fdだったら
          if(((struct clientinfo*)epoll_list[i].data.ptr)->fd!=master_fd)
            continue;
          //もし書き込み可能かwriteの書き込みバッファが溢れていなかったら
          if(epoll_list[i].events & EPOLLOUT)
          {
            if(clip_bord_chr == NULL && (clip_bord_chr = GetClipboardText()) == NULL)
            {
              break;
            }
          }
        }
      }
      else if(write_buff_overflow==true)
      {
        if(clip_bord_chr == NULL && (clip_bord_chr = GetClipboardText()) == NULL)
        {
          break;
        }
      }
      else 
      {
        clip_bord_chr = GetClipboardText();
      }
      
      if(clip_bord_chr!=NULL)
      {
        size_t len = strlen(clip_bord_chr);   
        if(len> 0) 
        {
          char temp_clip_bord_chr[len+1];
          int temp_clip_bord_chr_counter=0;
          for (size_t i = 0; i < len; i++) 
          {
            char c = (char)clip_bord_chr[i];
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t')
            {
              temp_clip_bord_chr[temp_clip_bord_chr_counter++]=c;
            }
          }
          temp_clip_bord_chr[temp_clip_bord_chr_counter]='\0';
          //何バイト読み込んだか
          ssize_t now_fd_input_size=write(master_fd,temp_clip_bord_chr,strlen(temp_clip_bord_chr));
          //次のループで再度書き込む位置を保存しておきたいのでclipbord_chr変数から書き込んだバイト数のポインタを加算する
          if(now_fd_input_size>0)
          {
            clip_bord_chr+=now_fd_input_size;
            //もし送られたバイト数がクリップボードbuffのデータより大きかったら初期化する
            if(len<=now_fd_input_size)
            {
              clip_bord_chr=NULL;
            }
          }
          else if(now_fd_input_size==0)
          {
            clip_bord_chr=NULL;
            write_buff_overflow=false;
            master_fd_ev_poll.events=EPOLLIN;
            if(epoll_ctl(epoll_fd_list,EPOLL_CTL_MOD ,master_fd,&master_fd_ev_poll)!=0)
            {
              char err_buff[128];
              snprintf(err_buff,128,"epoll_ctl func error errno = %d",errno);
              error_log_write(err_buff);
            }
          }
          // [AI生成] write() が EAGAIN を返した = カーネルの送信バッファが満杯でこれ以上送れない。
          // epoll を EPOLLOUT に切り替え、次にバッファが空いた時点で再度通知を受け取る。
          else if(now_fd_input_size==-1 && errno==EAGAIN)
          {

            master_fd_ev_poll.events=EPOLLOUT;
            write_buff_overflow=true;

            if(epoll_ctl(epoll_fd_list,EPOLL_CTL_MOD ,master_fd,&master_fd_ev_poll)!=0)
            {
              char err_buff[128];
              snprintf(err_buff,128,"epoll_ctl func error errno = %d",errno);
              error_log_write(err_buff);
            }
            break;
          }
          else
          {//エラーの場合
            error_log_write("write error");
            return 0;
          }
        }
      }
      while (GetCharPressed() > 0) {} //ショートカットキーのバッファがたまっているので捨てる
    }
    else if(IsKeyDown(KEY_LEFT_SUPER) && IsKeyDown(KEY_Q)){
      break;
    }
    else if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_D))
    {
      write(master_fd, "\x04", 1);
      while (GetCharPressed() > 0) {}
    }
    else if(ctl_c_sig_check(&is_released_ctl_c,master_fd)==false)
    {

      ////英数字入力処理////////////
      int key_code = 0;  
      while ((key_code = GetCharPressed()) > 0 )
      {

        if (key_code  < 32 || key_code  >127)
          continue; 
        else if (IsAnyKeyReleased())
          break;

        char utf8[4]={0};
        int len=0;
        unicode_utf8_encoder(utf8,key_code ,&len);

        if(key_code =='\\' || key_code == '_')
          key_slash++;
        else 
          key_slash = 0;

        if(key_slash <= 1){
          if(key.key_data.key_type == NONE && key_slash < 1)
          {
            memcpy(&key.key_data.utf8,utf8,sizeof(char) * 4);
            key.key_data.len = len;
            key.key_repeat_st = GetTime();
            key.key_data.key_type = ALPBT_NUM;
          }


          ctx.cur->now_writing = true;
          write(master_fd,utf8, len);
        }
      }
      if(key_slash > 1)
        key_slash  = 0;
    }

    if (IsKeyPressed(KEY_ENTER)) 
    {
      char enter_key = 13;
      write(master_fd, &enter_key, 1);
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
      char c = 0x7f;

      write(master_fd, &c, 1);
      if(key.key_data.key_type == NONE)
      {
        key.key_data.key_repeat_state = true;
        key.key_repeat_st = GetTime();
        key.key_data.key_type = BS;
      }
      
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
      //右のセルが空白ならカーソルをブロックする
      if(ctx.cur->cur_pos.w < ctx.term_size.w)
      {
        if(ctx.cur->cur_pos.w < ctx.temp_cur_pos.w+1 || 
          ctx.term_cell[ctx.cur->cur_pos.h * ctx.term_size.w + ctx.cur->cur_pos.w + 1].character!=' ')
        {

          cur_allow_write(ctx.cur->allow_mode, master_fd, KEY_RIGHT);

          if(key.key_data.key_type == NONE)
          {
            key.key_data.key_repeat_state = true;
            key.key_repeat_st = GetTime();
            key.key_data.key_type = RIGHT_ALLOW;
          }
        }
      }
    }
    //ESC入力処理
    if(IsKeyPressed(KEY_ESCAPE)) write(master_fd, "\x1b", 1);
    //fnキー入力処理
    if (IsKeyPressed(KEY_F1))  write(master_fd, "\x1bOP",   3);
    if (IsKeyPressed(KEY_F2))  write(master_fd, "\x1bOQ",   3);
    if (IsKeyPressed(KEY_F3))  write(master_fd, "\x1bOR",   3);
    if (IsKeyPressed(KEY_F4))  write(master_fd, "\x1bOS",   3);
    if (IsKeyPressed(KEY_F5))  write(master_fd, "\x1b[15~", 5);
    if (IsKeyPressed(KEY_F6))  write(master_fd, "\x1b[17~", 5);
    if (IsKeyPressed(KEY_F7))  write(master_fd, "\x1b[18~", 5);
    if (IsKeyPressed(KEY_F8))  write(master_fd, "\x1b[19~", 5);
    if (IsKeyPressed(KEY_F9))  write(master_fd, "\x1b[20~", 5);
    if (IsKeyPressed(KEY_F10)) write(master_fd, "\x1b[21~", 5);
    if (IsKeyPressed(KEY_F11)) write(master_fd, "\x1b[23~", 5);
    if (IsKeyPressed(KEY_F12)) write(master_fd, "\x1b[24~", 5);

    //ナビゲーションキー入力処理
    if (IsKeyPressed(KEY_HOME))      write(master_fd, "\x1b[1~", 4);
    if (IsKeyPressed(KEY_INSERT))    write(master_fd, "\x1b[2~", 4);
    if (IsKeyPressed(KEY_DELETE))    write(master_fd, "\x1b[3~", 4);
    if (IsKeyPressed(KEY_END))       write(master_fd, "\x1b[4~", 4);
    if (IsKeyPressed(KEY_PAGE_UP))   write(master_fd, "\x1b[5~", 4);
    if (IsKeyPressed(KEY_PAGE_DOWN)) write(master_fd, "\x1b[6~", 4);

    if (IsKeyPressed(KEY_TAB))
      write(master_fd, "\t", 1);

    if (IsKeyPressed(KEY_LEFT)){

      cur_allow_write(ctx.cur->allow_mode, master_fd, KEY_LEFT);

      if(key.key_data.key_type == NONE)
      {
        key.key_data.key_repeat_state = true;
        key.key_repeat_st = GetTime();
        key.key_data.key_type = LEFT_ALLOW;
      }
    }
    if(IsKeyPressed(KEY_UP))
    {
      cur_allow_write(ctx.cur->allow_mode, master_fd, KEY_UP);
      if(key.key_data.key_type == NONE)
      {
        key.key_data.key_repeat_state = true;
        key.key_repeat_st = GetTime();
        key.key_data.key_type = UP_ALLOW;
      }
    }
    if(IsKeyPressed(KEY_DOWN))
    {
      cur_allow_write(ctx.cur->allow_mode, master_fd, KEY_DOWN);
      if(key.key_data.key_type == NONE)
      {
        key.key_data.key_repeat_state = true;
        key.key_repeat_st = GetTime();
        key.key_data.key_type = DOWN_ALLOW;
      }

    }
    if(nfds>0)
    {
      for(int i=0;i<nfds;i++)
      {
        //もしfdがmaster_fdだったら
        if(((struct clientinfo *)epoll_list[i].data.ptr)->fd!=master_fd)
          continue;

        if((epoll_list[i].events & EPOLLIN)==false)
          break;
        
        while (1) 
        {
          ssize_t buf_size = read(master_fd, read_buf, term_cell_alloc_size - 1);
          if (buf_size > 0)
            bash_str_parse(read_buf, buf_size, &ctx);
          
          else if(buf_size==0)
            break;
          
          else if (buf_size == -1)
          {
            // -1 の場合は errno を確認する
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
              // 受信バッファが空になったので、正常に読み取りループを抜ける
              break;
            } else 
            {
              // それ以外の本当のエラー
              error_log_write("read error");
              break;
            }
          }
        }
        break;    
      }
    }

    // キーリピート関数
    key_repeat(&key,master_fd,setting_data,&ctx);


    ////マウスカーソル点滅再開処理//////
    if( ctx.cur->now_writing == true){
      if(ctx.cur->writing_st_time <= 0)
        ctx.cur->writing_st_time = GetTime();
 
      ctx.cur->writing_end_time = GetTime();
      
      if(ctx.cur->writing_end_time - ctx.cur->writing_st_time < setting_data.cursor_blink_restart_timeout_seconds)
        goto CUR_RIGTHING_END_POINT;


      ctx.cur->now_writing = false;
      ctx.cur->writing_st_time = 0;
      ctx.cur->writing_end_time = 0;
    }
    //マウスカーソル分岐抜け
    CUR_RIGTHING_END_POINT:
    


    if(IsWindowResized()){
      last_resize_time = GetTime();
    }

    //リサイズ処理（デバウンス: 0.1秒間リサイズが止まってから実行）
    if(last_resize_time > 0 && GetTime() - last_resize_time > 0.1){
      old_term_cell_size = term_size;

      //画面サイズ・セル数を更新
      window_resized_update_memb(&screen_pixel,&term_size,&ctx);

      // [AI生成] リサイズ後は新しいウィンドウサイズに合わせてRenderTextureを再作成する
      UnloadRenderTexture(render_tex);
      render_tex = LoadRenderTexture(screen_pixel.w * RENDER_SCALE, screen_pixel.h * RENDER_SCALE);
      SetTextureFilter(render_tex.texture, TEXTURE_FILTER_BILINEAR);

      total=term_size.h*term_size.w;
      ctx.term_size=term_size;
      ctx.total_cells=total;

      ws.ws_col = term_size.w;
      ws.ws_row = term_size.h;
      ws.ws_xpixel = (unsigned short)screen_pixel.w;
      ws.ws_ypixel = (unsigned short)screen_pixel.h;

      ioctl(master_fd, TIOCSWINSZ, &ws);
      kill(pid_id, SIGWINCH);

      if(total>term_cell_alloc_size){
        int old_alloc_size = term_cell_alloc_size;

        while(total>term_cell_alloc_size){
          term_cell_alloc_size*=2;
        }
        char *read_buff_temp = calloc(term_cell_alloc_size,sizeof(char));
        struct term_cell *main_term_cell_temp = realloc(ctx.term_cell,sizeof(struct term_cell)*term_cell_alloc_size);
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

      reflow_terminal_text(&ctx, old_term_cell_size, &temp_term_cell, term_cell_alloc_size);

      struct line_info *new_lines = calloc(term_size.h, sizeof(struct line_info));
      if (new_lines != NULL) {
        free(ctx.lines);
        ctx.lines = new_lines;
      }

      last_resize_time = 0;
    }
    
      

    // [AI生成] オフスクリーンテクスチャにRENDER_SCALE倍の座標で描画し、
    // その後ウィンドウサイズに縮小して表示するスーパーサンプリング描画
    int rs = ctx.render_scale;
    BeginTextureMode(render_tex);
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
          DrawRectangle(j * ctx.cell_w * rs, i * ctx.cell_h * rs, ctx.cell_w * rs, ctx.cell_h * rs, bg);
        }
        DrawTextCodepoint(myfont, c, (Vector2){j * ctx.cell_w * rs, i * ctx.cell_h * rs}, ctx.cell_h * rs, fg);
      }
    }
    draw_cursor(ctx.cur, &current_time, &ctx);
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    // [AI生成] RenderTextureはY軸が反転しているためsrc heightをマイナスで指定する
    DrawTexturePro(render_tex.texture,
      (Rectangle){0, 0, (float)render_tex.texture.width, -(float)render_tex.texture.height},
      (Rectangle){0, 0, (float)screen_pixel.w, (float)screen_pixel.h},
      (Vector2){0, 0}, 0.0f, WHITE);
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
  UnloadRenderTexture(render_tex);
  CloseWindow();
  close(epoll_fd_list);
}

void scroll_region_up(struct term_context *ctx) {
  int W      = ctx->term_size.w;
  int top    = ctx->fixrd_cur_scr_range.decstbm_state ? ctx->fixrd_cur_scr_range.top_margin    : 0;
  int bottom = ctx->fixrd_cur_scr_range.decstbm_state ? ctx->fixrd_cur_scr_range.bottom_margin : ctx->term_size.h - 1;
  if (top >= bottom || bottom >= ctx->term_size.h) return;
  // [AI生成] top+1行目から bottom行目を1行分上にシフト。src/dstが重なるためmemcpyではなくmemmoveが必要
  memmove(&ctx->term_cell[top * W], &ctx->term_cell[(top + 1) * W],
    sizeof(struct term_cell) * W * (bottom - top));
  // [AI生成] シフトで空いた最終行をクリア
  for (int c = bottom * W; c < (bottom + 1) * W; c++) {
    ctx->term_cell[c].character   = ' ';
    ctx->term_cell[c].bg_color    = ctx->bash_parser_required_memb.now_bg_color;
    ctx->term_cell[c].fg_color    = ctx->bash_parser_required_memb.now_fg_color;
    ctx->term_cell[c].is_bold     = false;
    ctx->term_cell[c].is_real_chr = false;
  }
  memmove(&ctx->lines[top], &ctx->lines[top + 1], sizeof(struct line_info) * (bottom - top));
  ctx->lines[bottom].is_wrapped = false;
}

void scroll_region_down(struct term_context *ctx) {
  int W      = ctx->term_size.w;
  int top    = ctx->fixrd_cur_scr_range.decstbm_state ? ctx->fixrd_cur_scr_range.top_margin    : 0;
  int bottom = ctx->fixrd_cur_scr_range.decstbm_state ? ctx->fixrd_cur_scr_range.bottom_margin : ctx->term_size.h - 1;
  if (top >= bottom || bottom >= ctx->term_size.h) return;
  // [AI生成] top行目から bottom-1行目を1行分下にシフト。src/dstが重なるためmemmoveが必要
  memmove(&ctx->term_cell[(top + 1) * W], &ctx->term_cell[top * W],
    sizeof(struct term_cell) * W * (bottom - top));
  // [AI生成] シフトで空いた先頭行をクリア
  for (int c = top * W; c < (top + 1) * W; c++) {
    ctx->term_cell[c].character   = ' ';
    ctx->term_cell[c].bg_color    = ctx->bash_parser_required_memb.now_bg_color;
    ctx->term_cell[c].fg_color    = ctx->bash_parser_required_memb.now_fg_color;
    ctx->term_cell[c].is_bold     = false;
    ctx->term_cell[c].is_real_chr = false;
  }
  memmove(&ctx->lines[top + 1], &ctx->lines[top], sizeof(struct line_info) * (bottom - top));
  ctx->lines[top].is_wrapped = false;
}

Color xterm_256color(int n) {
  if (n < 0 || n > 255) return WHITE;
  const Color base[8] = {
    {0,0,0,255},{170,0,0,255},{0,170,0,255},{170,85,0,255},
    {0,0,170,255},{170,0,170,255},{0,170,170,255},{170,170,170,255}
  };
  const Color bright[8] = {
    {85,85,85,255},{255,85,85,255},{85,255,85,255},{255,255,85,255},
    {85,85,255,255},{255,85,255,255},{85,255,255,255},{255,255,255,255}
  };
  if (n < 8)  return base[n];
  if (n < 16) return bright[n - 8];
  if (n < 232) {
    // [AI生成] 16〜231: xterm が定義する 6x6x6 の RGB カラーキューブ (216色)
    // n-16 を3桁の6進数として解釈し、各桁をR/G/Bに割り当てる
    int idx = n - 16;
    int b = idx % 6; idx /= 6; // [AI生成] 6進数の1桁目 (最下位)
    int g = idx % 6; idx /= 6; // [AI生成] 6進数の2桁目
    int r = idx;               // [AI生成] 6進数の3桁目 (最上位)
    // [AI生成] 値0→0, 1〜5→55+40*n の xterm 公式変換
    return (Color){r ? 55 + r*40 : 0, g ? 55 + g*40 : 0, b ? 55 + b*40 : 0, 255};
  }
  // [AI生成] 232〜255: グレースケール (8〜238, 10刻み)
  int v = 8 + (n - 232) * 10;
  return (Color){v, v, v, 255};
}

void esc_single_dispatch(struct term_context *ctx, char c) {
  switch (c) {
    case '7':  // DECSC: カーソル位置を保存
      *(ctx->save_cur) = *(ctx->cur);
      break;
    case '8':  // DECRC: カーソル位置を復元
      *(ctx->cur) = *(ctx->save_cur);
      break;
    case 'M':  // RI: 逆スクロール（カーソルが先頭行なら下スクロール）
    {
      int top = ctx->fixrd_cur_scr_range.decstbm_state
        ? ctx->fixrd_cur_scr_range.top_margin : 0;
      if (ctx->cur->cur_pos.h > top)
        ctx->cur->cur_pos.h--;
      else
        scroll_region_down(ctx);
      break;
    }
    case 'E':  // NEL: 次の行の先頭へ
      ctx->cur->cur_pos.w = 0;
      ctx->cur->cur_pos.h++;
      break;
    case 'c':  // RIS: ターミナル全体リセット
      for (int i = 0; i < ctx->term_size.h * ctx->term_size.w; i++) {
        ctx->term_cell[i].character   = ' ';
        ctx->term_cell[i].fg_color    = WHITE;
        ctx->term_cell[i].bg_color    = BLACK;
        ctx->term_cell[i].is_bold     = false;
        ctx->term_cell[i].is_real_chr = false;
      }
      ctx->cur->cur_pos.w = 0;
      ctx->cur->cur_pos.h = 0;
      break;
    case '=':  // DECKPAM: キーパッドアプリケーションモード（無視）
    case '>':  // DECKPNM: キーパッド数値モード（無視）
    default:
      break;
  }
}

void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx) {

  for (int i = 0; i < size; i++) {
    if (ctx->bash_parser_required_memb.state == SQE_START) {
      if (ctx->bash_parser_required_memb.mode == IDK) {
        char esc_char = buff[i];
        if (esc_char == '[') {
          ctx->bash_parser_required_memb.mode = CSI_MODE;
        } else if (esc_char == ']') {
          ctx->bash_parser_required_memb.mode = OSC_MODE;
        } else {
          // 2文字ESCシーケンス: ESC ( / ESC ) は次の1文字を消費して無視
          if (esc_char == '(' || esc_char == ')') {
            if (i + 1 < size) i++;
          } else {
            esc_single_dispatch(ctx, esc_char);
          }
          ctx->bash_parser_required_memb.state = GROUND;
        }
        continue;
      }
      switch (ctx->bash_parser_required_memb.mode) {
        case OSC_MODE:
            // [AI生成] OSC の終端は BEL (\a) か、ST (ESC \) の2文字シーケンス。
            // ESC を見たら osc_state を OSC_EXPECT_ST にセットし、
            // 次の文字が '\\' なら終端確定として処理を実行する。
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

            // [AI生成] '?' はDEC private シーケンスの印（例: ESC[?25h でカーソル表示）。
            // is_private フラグを立てておき、終端文字到達時に ls_chr_parse 内で分岐する。
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
          if (ctx->cur->cur_pos.h >= 0 && ctx->cur->cur_pos.h < ctx->term_size.h)
              ctx->lines[ctx->cur->cur_pos.h].is_wrapped = false;
          // スクロール領域の底にいる場合は領域内スクロール、それ以外は行を下へ
          if (ctx->fixrd_cur_scr_range.decstbm_state &&
              ctx->cur->cur_pos.h == ctx->fixrd_cur_scr_range.bottom_margin) {
              scroll_region_up(ctx);
          } else {
              ctx->cur->cur_pos.h++;
          }
      } else if (buff[i] == '\a') {
          continue;
      } else if (buff[i] == '\t') {
          int next_tab = (ctx->cur->cur_pos.w / 8 + 1) * 8;
          if (next_tab >= ctx->term_size.w) next_tab = ctx->term_size.w - 1;
          ctx->cur->cur_pos.w = next_tab;
          continue;
      } else {
        if (ctx->insert_mode) {
            char_arry_insert_chr(ctx, 1);
        }
        // [AI生成] Delayed Wrap: VT100の仕様で、画面端に達した時点では折り返さず、
        // 次の可視文字を書こうとした瞬間に初めて改行する。
        // これにより行末ぴったりに文字が収まった場合に不要な空行が生まれない。
        if (ctx->cur->cur_pos.w >= ctx->term_size.w) {
            if (ctx->cur->cur_pos.h >= 0 && ctx->cur->cur_pos.h < ctx->term_size.h) {
                ctx->lines[ctx->cur->cur_pos.h].is_wrapped = true;
            }
            ctx->cur->cur_pos.w = 0;
            ctx->cur->cur_pos.h++;
            
            if (ctx->fixrd_cur_scr_range.decstbm_state &&
                ctx->cur->cur_pos.h > ctx->fixrd_cur_scr_range.bottom_margin) {
                scroll_region_up(ctx);
                ctx->cur->cur_pos.h = ctx->fixrd_cur_scr_range.bottom_margin;
            } else if (ctx->cur->cur_pos.h >= ctx->term_size.h) {
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

            bool rev = ctx->bash_parser_required_memb.now_is_reverse;

            ctx->term_cell[idx].character = buff[i];

            ctx->term_cell[idx].fg_color  = rev
                ? ctx->bash_parser_required_memb.now_bg_color
                : ctx->bash_parser_required_memb.now_fg_color;
                
            ctx->term_cell[idx].bg_color  = rev
                ? ctx->bash_parser_required_memb.now_fg_color
                : ctx->bash_parser_required_memb.now_bg_color;
                
            ctx->term_cell[idx].is_bold     = ctx->bash_parser_required_memb.now_is_bold;
            ctx->term_cell[idx].is_real_chr = true;
            ctx->temp_cur_pos.h = ctx->cur->cur_pos.h;
            ctx->temp_cur_pos.w = ctx->cur->cur_pos.w;
            ctx->cur->cur_pos.w++;
        }
      }
      // 画面外スクロール処理（スクロール領域未使用時のみ全体スクロール）
      if (ctx->cur->cur_pos.h >= ctx->term_size.h) {
        if (ctx->fixrd_cur_scr_range.decstbm_state) {
          ctx->cur->cur_pos.h = ctx->term_size.h - 1;
        } else {
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
          ctx->bash_parser_required_memb.now_is_bold    = false;
          ctx->bash_parser_required_memb.now_is_reverse = false;
        } else if (code == 1) {
          ctx->bash_parser_required_memb.now_is_bold = true;
        } else if (code == 7) {
          ctx->bash_parser_required_memb.now_is_reverse = true;
        } else if (code == 22) {
          ctx->bash_parser_required_memb.now_is_bold = false;
        } else if (code == 27) {
          ctx->bash_parser_required_memb.now_is_reverse = false;
        } else if (code >= 30 && code <= 37) {
          Color colors[] = {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, SKYBLUE, WHITE};
          *now_fg_color = colors[code - 30];
        } else if (code == 39) {
          *now_fg_color = WHITE;
        } else if (code >= 40 && code <= 47) {
          Color colors[] = {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, SKYBLUE, WHITE};
          *now_bg_color = colors[code - 40];
        } else if (code == 49) {
          *now_bg_color = BLACK;
        } else if (code >= 90 && code <= 97) {
          Color colors[] = {
            {85,85,85,255},{255,85,85,255},{85,255,85,255},{255,255,85,255},
            {85,85,255,255},{255,85,255,255},{85,255,255,255},{255,255,255,255}
          };
          *now_fg_color = colors[code - 90];
        } else if (code >= 100 && code <= 107) {
          Color colors[] = {
            {85,85,85,255},{255,85,85,255},{85,255,85,255},{255,255,85,255},
            {85,85,255,255},{255,85,255,255},{85,255,255,255},{255,255,255,255}
          };
          *now_bg_color = colors[code - 100];
        } else if ((code == 38 || code == 48) && i + 1 < palms_counter) {
          if (palms[i+1] == 5 && i + 2 < palms_counter) {
            // 256色
            Color c = xterm_256color(palms[i+2]);
            if (code == 38) *now_fg_color = c;
            else            *now_bg_color = c;
            i += 2;
          } else if (palms[i+1] == 2 && i + 4 < palms_counter) {
            // truecolor
            Color c = {palms[i+2], palms[i+3], palms[i+4], 255};
            if (code == 38) *now_fg_color = c;
            else            *now_bg_color = c;
            i += 4;
          }
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
    //スクロール領域 DECSTBM）
    case 'r':
    {
      // VT100は1-indexed; 0-indexedに変換して保存
      int top    = (palms_counter > 0 && palms[0] > 0) ? palms[0] - 1 : 0;
      int bottom = (palms_counter > 1 && palms[1] > 0) ? palms[1] - 1 : ctx->term_size.h - 1;
      ctx->fixrd_cur_scr_range.top_margin    = top;
      ctx->fixrd_cur_scr_range.bottom_margin = bottom;
      ctx->fixrd_cur_scr_range.decstbm_state = true;

      // カーソルをスクロール領域の先頭行へ移動
      ctx->cur->cur_pos.h = top;
      ctx->cur->cur_pos.w = 0;
      ctx->home_pos = (struct pos){0, top};
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
    //カーソル水平絶対位置指定
    case 'G':
    {
      // VT100は1-indexed; palms[0]==0はデフォルト列1扱い
      int width = (palms[0] > 0) ? palms[0] - 1 : 0;
      if(width >= ctx->term_size.w)
        width = ctx->term_size.w - 1;
      ctx->cur->cur_pos.w = width;
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
          case 1:
          {
            if(ctx->bash_parser_required_memb.is_private == false)
              break;

            if(is_on)
              ctx->cur->allow_mode = AP_MODE;
            else 
              ctx->cur->allow_mode = NORMAL_MODE;
          }
          case 6:
          {
            if(!ctx->bash_parser_required_memb.is_private)
              break;
            //ホームポジション更新
            ctx->home_pos = (struct pos){0,0};
            break;
          }
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
      // 行削除(M) / 行挿入(L)
    case 'M':
    case 'L':
    {
      int R = ctx->cur->cur_pos.h;
      int W = ctx->term_size.w;
      int bottom_row = ctx->fixrd_cur_scr_range.decstbm_state
        ? ctx->fixrd_cur_scr_range.bottom_margin
        : ctx->term_size.h - 1;

      if(buff == 'M')
      {
        // DL: カーソル行を削除し、下の行をスクロール領域内で上にシフト
        if(R < bottom_row)
          memmove(&ctx->term_cell[R * W], &ctx->term_cell[(R + 1) * W],
            sizeof(struct term_cell) * W * (bottom_row - R));
        // スクロール領域の最終行をクリア
        for(int i = bottom_row * W; i < (bottom_row + 1) * W; i++){
          ctx->term_cell[i].bg_color    = ctx->bash_parser_required_memb.now_bg_color;
          ctx->term_cell[i].fg_color    = ctx->bash_parser_required_memb.now_fg_color;
          ctx->term_cell[i].character   = ' ';
          ctx->term_cell[i].is_bold     = false;
          ctx->term_cell[i].is_real_chr = false;
        }
      }
      else
      {
        // IL: カーソル行に空行を挿入し、下の行をスクロール領域内で下にシフト
        if(R < bottom_row)
          memmove(&ctx->term_cell[(R + 1) * W], &ctx->term_cell[R * W],
            sizeof(struct term_cell) * W * (bottom_row - R));
        // カーソル行をクリア
        for(int i = R * W; i < (R + 1) * W; i++){
          ctx->term_cell[i].bg_color    = ctx->bash_parser_required_memb.now_bg_color;
          ctx->term_cell[i].fg_color    = ctx->bash_parser_required_memb.now_fg_color;
          ctx->term_cell[i].character   = ' ';
          ctx->term_cell[i].is_bold     = false;
          ctx->term_cell[i].is_real_chr = false;
        }
      }

      break;
    }
    // 指定数分の空白を挿入（ICH: insert characters）
    case '@':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      char_arry_insert_chr(ctx,n);      
      break;
    }
    case '~':
    {
      switch(palms[0])
      {
        // Home: カーソルを行の先頭へ移動
        case 1:
          ctx->cur->cur_pos.w = 0;
          break;
        // Insert: インサートモードのトグル
        case 2:
          ctx->insert_mode = !ctx->insert_mode;
          break;
        // Delete: カーソル位置の文字を削除（DCH相当）
        case 3:
          char_array_alignment(ctx, 1);
          break;
        // End: カーソルを行末の実文字の次へ移動
        case 4:
        {
          int line_start = ctx->cur->cur_pos.h * ctx->term_size.w;
          int last_real = ctx->cur->cur_pos.w;
          for (int c = ctx->term_size.w - 1; c >= 0; c--) {
            if (ctx->term_cell[line_start + c].is_real_chr) {
              last_real = c + 1;
              break;
            }
          }
          if (last_real >= ctx->term_size.w) last_real = ctx->term_size.w - 1;
          ctx->cur->cur_pos.w = last_real;
          break;
        }
        // Page Up: 1ページ分上にスクロール（画面を下にずらす）
        case 5:
          for (int j = 0; j < ctx->term_size.h; j++)
            scroll_region_down(ctx);
          break;
        // Page Down: 1ページ分下にスクロール（画面を上にずらす）
        case 6:
          for (int j = 0; j < ctx->term_size.h; j++)
            scroll_region_up(ctx);
          break;
        case 21: // F10: 無視
        default:
          break;
      }
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
    // SU: n行上にスクロール
    case 'S':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      for (int j = 0; j < n; j++)
        scroll_region_up(ctx);
      break;
    }
    // SD: n行下にスクロール
    case 'T':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      for (int j = 0; j < n; j++)
        scroll_region_down(ctx);
      break;
    }
    // VPA: 垂直絶対位置（行のみ移動、列は保持）
    case 'd':
    {
      int row = (palms_counter > 0 && palms[0] > 0) ? palms[0] - 1 : 0;
      if (row >= ctx->term_size.h) row = ctx->term_size.h - 1;
      ctx->cur->cur_pos.h = row;
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

  if(cur->now_writing)
    cur->lighting.now_right = true;

  else if (now_time - *current_time >= cur->lighting.speed_ms / 1000){
    *current_time = now_time;
    cur->lighting.now_right = !cur->lighting.now_right;
  }
  
  if (cur->lighting.now_right) {
    // [AI生成] render_scaleを掛けてオフスクリーンテクスチャ内の座標に変換する
    int rs = ctx->render_scale;
    int draw_w = cur->cur_pos.w >= ctx->term_size.w ? ctx->term_size.w - 1 : cur->cur_pos.w;
    DrawTextEx(
      cur->font,
      cur->shape,
      (Vector2){draw_w * ctx->cell_w * rs, cur->cur_pos.h * ctx->cell_h * rs},
      ctx->cell_h * rs,
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
    if (*(result + 1) == 'c') 
    {
      char *str_ptr_st = strchr(result + 1, ';');
      if (str_ptr_st == NULL) return NULL;
      char str_ptr[strlen(str_ptr_st)+1];
      strcpy(str_ptr, str_ptr_st + 1);
      struct return_binary *char_bin = char_conbert_binary_arry(str_ptr);
      if (char_bin != NULL) 
      {
        int remainder = char_bin->char_binary_counter % 8;
        if (remainder != 0) 
        {
          int add_bits = 8 - remainder;
          int *temp = realloc(char_bin->char_binary, sizeof(int) * (char_bin->char_binary_counter + (8 - (char_bin->char_binary_counter % 8))));
          if (temp == NULL) 
          {
            free(char_bin->char_binary);
            free(char_bin);
            perror("char_bin realloc error");
            return NULL;
          }
          char_bin->char_binary = temp;
          for (int i = 0; i < add_bits; i++)
          {
            char_bin->char_binary[char_bin->char_binary_counter + i] = 0;
          }
          char_bin->char_binary_counter += 8 - (char_bin->char_binary_counter % 8);
        }
        int final_len = char_bin->char_binary_counter / 8;
        converted_chr = malloc(sizeof(char) * ((char_bin->char_binary_counter / 8) + 1));
        // [AI生成] 8ビットずつ取り出して1バイトに組み立てる。
        // ビット列は MSB 優先で格納されているので、左シフトしながら OR するだけで正しい値になる。
        for (int i = 0; i < char_bin->char_binary_counter / 8; i++)
        {
          int total = 0;
          for (int c = 0; c < 8; c++)
          {
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

struct return_binary *char_conbert_binary_arry(char *osc_pal_chr)
{
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
    // [AI生成] Base64の1文字は0〜63のインデックス（6ビット）に対応する。
    // i=5→i=0の逆順ループで LSB から 1ビットずつ取り出し、
    // 配列上は [counter+0]=MSB, [counter+5]=LSB の大端(big-endian)順で格納する。
    // こうすることで後段の再組み立て（i=0から順に左シフト）でそのまま正しい値になる。
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
  ctx->bash_parser_required_memb.now_bg_color=c_col;
}

Color conbert_num_to_color(char *color_str,int mode){
  Color target_color={0};
  int r = 0, g = 0, b = 0;
  if(mode==0){
    // [AI生成] X11 の rgb: 形式は "rrrr/gggg/bbbb" (各チャンネル最大4桁の16進数)。
    // 上位2桁だけを 8bit 値として使う（%02x で先頭2桁を読む）。
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

// [AI生成] DCH (Delete Characters): カーソル位置から n 文字削除し、右の文字を左にシフト。
// 前方（小インデックス方向）から順にコピーすることで src/dst の重なりを避けられる。
void char_array_alignment(struct term_context *ctx,int n){
  int loop= ctx->term_size.w - ctx->cur->cur_pos.w;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  for(int i = 0; i < loop - n; i++){
    // 文字だけでなく、色情報なども含めて構造体ごとコピーする
    ctx->term_cell[idx + i] = ctx->term_cell[idx + i + n];
  }
  // [AI生成] シフトで空いた末尾 n セルをスペースで埋める
  for(int i = loop - n; i < loop; i++){
    ctx->term_cell[idx + i].character = ' ';
    ctx->term_cell[idx + i].is_real_chr = false;
  }
}
// [AI生成] ICH (Insert Characters): カーソル位置に n 個のスペースを挿入し、右の文字を押し出す。
// 後方（大インデックス方向）から順にコピーしないと、まだコピーしていない src を上書きしてしまう。
void char_arry_insert_chr(struct term_context *ctx,int n){
  int loop= ctx->term_size.w - ctx->cur->cur_pos.w;
  int idx = ctx->cur->cur_pos.h * ctx->term_size.w + ctx->cur->cur_pos.w;
  if (n > loop) n = loop;
  for(int i = loop - 1; i >= n; i--){
    ctx->term_cell[idx + i] = ctx->term_cell[idx + i - n];
  }
  // [AI生成] 挿入位置 n セルをスペースで埋める
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
  char buff[strlen(error_statement  )+1];

  snprintf(buff,strlen(error_statement  )+1,"%s\n",error_statement);

  ssize_t size=fputs(buff,error_f);
  if(size<0)
    perror("can not write on error_log.txt");

  fclose(error_f);
}
void window_resized_update_memb(struct pos *screen_pixel,struct pos *term_size,struct term_context *ctx){
  screen_pixel->w=GetScreenWidth();
  screen_pixel->h=GetScreenHeight();

  term_size->w = (int)(screen_pixel->w) / ctx->cell_w;
  term_size->h = (int)screen_pixel->h / ctx->cell_h;
  if (term_size->w <= 0) term_size->w = 1;
  if (term_size->h <= 0) term_size->h = 1;
}

void unicode_utf8_encoder(char *utf8,int unicode, int *len){
  // [AI生成] UTF-8エンコード規則:
  //   1バイト: 0xxxxxxx                        (0x00〜0x7F)
  //   2バイト: 110xxxxx 10xxxxxx               (0x80〜0x7FF)
  //   3バイト: 1110xxxx 10xxxxxx 10xxxxxx      (0x800〜0xFFFF)
  //   4バイト: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx (0x10000〜)
  // 先頭バイトのマスク(0xC0, 0xE0, 0xF0)はバイト数を示す識別子。
  // 継続バイトは必ず 0x80 | (6ビット) の形になる。
  int n=unicode;
    if (n <= 0x7F) {
        utf8[0] = (char)n;
        *len = 1;
    } else if (n <= 0x7FF) {
        utf8[0] = (char)(0xC0 | (n >> 6));        // [AI生成] 上位5ビット
        utf8[1] = (char)(0x80 | (n & 0x3F));      // [AI生成] 下位6ビット
        *len = 2;
    } else if (n <= 0xFFFF) {
        utf8[0] = (char)(0xE0 | (n >> 12));        // [AI生成] 上位4ビット
        utf8[1] = (char)(0x80 | ((n >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (n & 0x3F));
        *len = 3;
    } else if (n <= 0x10FFFF) {
        utf8[0] = (char)(0xF0 | (n >> 18));        // [AI生成] 上位3ビット
        utf8[1] = (char)(0x80 | ((n >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((n >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (n & 0x3F));
        *len = 4;
    }
}

void reflow_terminal_text(struct term_context *ctx, struct pos old_term_size, struct term_cell **temp_term_cell_ptr, int term_cell_alloc_size) {
  struct term_cell *temp = *temp_term_cell_ptr;
  int new_w = ctx->term_size.w;
  int new_h = ctx->term_size.h;

  for (int i = 0; i < new_h * new_w; i++) {
    temp[i].character   = ' ';
    temp[i].fg_color    = ctx->bash_parser_required_memb.now_fg_color;
    temp[i].bg_color    = ctx->bash_parser_required_memb.now_bg_color;
    temp[i].is_bold     = false;
    temp[i].is_real_chr = false;
  }

  int now_w = 0;
  int now_h = 0;

  for (int h = 0; h < old_term_size.h && now_h < new_h; h++) {
    // 旧行の最後の実文字を探す（末尾の空白は無視）
    int last_real = -1;
    for (int w = old_term_size.w - 1; w >= 0; w--) {
      if (ctx->term_cell[h * old_term_size.w + w].is_real_chr) {
        last_real = w;
        break;
      }
    }

    for (int w = 0; w <= last_real; w++) {
      if (now_w >= new_w) {
        now_h++;
        now_w = 0;
        if (now_h >= new_h) goto reflow_done;
      }
      temp[now_h * new_w + now_w] = ctx->term_cell[h * old_term_size.w + w];
      now_w++;
    }

    // 論理行の末尾（折り返しでない行）なら新バッファでも改行
    if (!ctx->lines[h].is_wrapped && now_w > 0) {
      now_h++;
      now_w = 0;
    }
  }

reflow_done:;
  struct term_cell *swap = ctx->term_cell;
  ctx->term_cell = temp;
  *temp_term_cell_ptr = swap;
}


void load_settings(struct setting_data *data){

  FILE *settings_file = fopen("pty_make_settings.json","r");

  //開けなかったらデフォルト設定
  if(settings_file == NULL){
    error_log_write("can not open settings.json");
    set_default_settings(data);
  }
}

void set_default_settings(struct setting_data *data){
  data->key_repeat_interval = DEFAULT_KEY_REPEAT_INTERVAL;
  data->cursor_blink_restart_timeout_seconds = DEFAULT_CUR_BLINK_RESTART_TIMEOUT_SEC;
}

struct term_cell *allocate_cell(struct term_cell* term_cell,int size)
{
  struct term_cell * temp = realloc(term_cell,sizeof(struct term_cell)* size);
  return temp;
}

void key_repeat(struct key_repeat *key,int master_fd,struct setting_data setting_data,struct term_context *ctx){
  ////英数字キーリピート処理//////
    // [AI生成] gotoはキーリピート判定の複数条件（キー離し / 初回ディレイ / 連射間隔）を
    // ネストせずにまとめてスキップするために使っている。
    // KEY_REPEAT_END_POINT に飛べば write() せずに次の処理へ進む。
  switch(key->key_data.key_type){
    case ALPBT_NUM:
    {

      if(IsAnyKeyReleased())
      {
        key->key_data.key_type = NONE;
        break;
      }

      double now_time = GetTime();

      // [AI生成] キーを押してから key_repeat_interval 秒以内は連射しない（初回ディレイ）
      if(now_time - key->key_repeat_st < setting_data.key_repeat_interval)
        break;

      // [AI生成] 前回の連射から 50ms 未満ならまだ送らない（連射レート制限）
      if(now_time - key->key_repeat_interval < 0.05)
        break;


      write(master_fd,key->key_data.utf8,key->key_data.len);

      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
      key->key_repeat_interval = now_time;

      break;
    }
    case BS:
    {
      //バックスペースリピート処理
    if(key->key_data.key_type == BS)
    {
      key->key_repeat_interbal_ed  = GetTime();

      if((GetTime() - key->key_repeat_st) < 0.6)
        break;

     
      if(IsKeyUp(KEY_BACKSPACE))
      {
        key->key_data.key_type = NONE;
        break;
      }

      if(key->key_repeat_interbal_ed - key->key_repeat_interbal_st < 0.05)
        break;;
     
      char c = 0x7f;
      write(master_fd, &c, 1);

      key->key_repeat_interbal_st = key->key_repeat_interbal_ed;
      
      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
    }
      break;
    }
    case RIGHT_ALLOW:
    {
      double time = GetTime();
      if(time - key->key_repeat_st < setting_data.key_repeat_interval)
        break;

      if(IsKeyUp(KEY_RIGHT))
      {
        key->key_data.key_type = NONE;
        break;
      }

      key->key_repeat_interbal_ed = time;

      if(key->key_repeat_interbal_ed - key->key_repeat_interbal_st < 0.05)
        break;

      cur_allow_write(ctx->cur->allow_mode, master_fd, KEY_RIGHT);

      key->key_repeat_interbal_st = key->key_repeat_interbal_ed;

      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
      break;
    }
    case LEFT_ALLOW:
    {
      double time = GetTime();
      if(time - key->key_repeat_st < setting_data.key_repeat_interval)
        break;

      if(IsKeyUp(KEY_LEFT))
      {
        key->key_data.key_type = NONE;
        break;
      }

      key->key_repeat_interbal_ed = time;

      if(key->key_repeat_interbal_ed - key->key_repeat_interbal_st < 0.05)
        break;

      cur_allow_write(ctx->cur->allow_mode, master_fd, KEY_LEFT);

      key->key_repeat_interbal_st = key->key_repeat_interbal_ed;

      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
      break;
    }
    case UP_ALLOW:
    {
      double time = GetTime();
      if(time - key->key_repeat_st < setting_data.key_repeat_interval)
        break;

      if(IsKeyUp(KEY_UP))
      {
        key->key_data.key_type = NONE;
        break;
      }

      key->key_repeat_interbal_ed = time;

      if(key->key_repeat_interbal_ed - key->key_repeat_interbal_st < 0.05)
        break;

      cur_allow_write(ctx->cur->allow_mode, master_fd, KEY_UP);

      key->key_repeat_interbal_st = key->key_repeat_interbal_ed;

      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
      break;
    }
    case DOWN_ALLOW:
    {
      double time = GetTime();
      if(time - key->key_repeat_st < setting_data.key_repeat_interval)
        break;

      if(IsKeyUp(KEY_DOWN))
      {
        key->key_data.key_type = NONE;
        break;
      }

      key->key_repeat_interbal_ed = time;

      if(key->key_repeat_interbal_ed - key->key_repeat_interbal_st < 0.05)
        break;

      cur_allow_write(ctx->cur->allow_mode, master_fd, KEY_DOWN);

      key->key_repeat_interbal_st = key->key_repeat_interbal_ed;

      //書き込み中はカーソルの点滅をなくす
      ctx->cur->now_writing = true;
      break;
    }
    case NONE:
    {
      break;
      
    }
  }
  return;
}

bool ctl_c_sig_check(int *counter,int master_fd){
   
  if(*counter == 1 && IsKeyUp(KEY_LEFT_CONTROL)){
    *counter = 0;
    return 0;
  }
  if(*counter > 1 && IsKeyUp(KEY_C)){
    *counter = 0;
  }

  if(*counter == 0 && IsKeyDown(KEY_LEFT_CONTROL))
    (*counter)++;

  else if(*counter == 1 && IsKeyDown(KEY_C))
  {
    write(master_fd,"\x03",1);
    (*counter) ++;
    return 1;
  }
  return 0;
}

void cur_allow_write(enum cur_allow_mode mode, int master_fd, int key_code) {
  const char *seq = NULL;
  if (mode == AP_MODE) {
    switch (key_code) {
      case KEY_UP:    seq = "\x1bOA"; break;
      case KEY_DOWN:  seq = "\x1bOB"; break;
      case KEY_RIGHT: seq = "\x1bOC"; break;
      case KEY_LEFT:  seq = "\x1bOD"; break;
      default: return;
    }
  } else {
    switch (key_code) {
      case KEY_UP:    seq = "\x1b[A"; break;
      case KEY_DOWN:  seq = "\x1b[B"; break;
      case KEY_RIGHT: seq = "\x1b[C"; break;
      case KEY_LEFT:  seq = "\x1b[D"; break;
      default: return;
    }
  }
  write(master_fd, seq, strlen(seq));
}

// PSF2フォント(.psfu / .psfu.gz 両対応)をビットマップテクスチャとして読み込む。
// TTFパイプラインを経由しないので、アンチエイリアス・ヒンティングによる線の太りが起きない。
// .gz ファイルは zcat でパイプ展開し、全データをメモリに読んでからパースする。
Font LoadPSF2Font(const char *filename) {
  static const uint8_t PSF2_MAGIC[4] = {0x72, 0xb5, 0x4a, 0x86};
  typedef struct {
    uint8_t  magic[4];
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
  } PSF2Header;

  Font font = {0};

  size_t name_len = strlen(filename);
  bool is_gz = name_len > 3 && strcmp(filename + name_len - 3, ".gz") == 0;
  FILE *f;
  if (is_gz) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "zcat '%s'", filename);
    f = popen(cmd, "r");
  } else {
    f = fopen(filename, "rb");
  }
  if (!f) {
    TraceLog(LOG_ERROR, "LoadPSF2Font: cannot open %s", filename);
    return font;
  }

  // パイプはシーク不可なのでファイル全体をバッファに読み込む
  size_t buf_size = 0;
  size_t buf_cap  = 131072;
  uint8_t *buf = malloc(buf_cap);
  if (!buf) { is_gz ? pclose(f) : fclose(f); return font; }
  while (1) {
    size_t n = fread(buf + buf_size, 1, buf_cap - buf_size, f);
    buf_size += n;
    if (n == 0) break;
    if (buf_size == buf_cap) {
      buf_cap *= 2;
      uint8_t *tmp = realloc(buf, buf_cap);
      if (!tmp) { free(buf); is_gz ? pclose(f) : fclose(f); return font; }
      buf = tmp;
    }
  }
  is_gz ? pclose(f) : fclose(f);

  if (buf_size < sizeof(PSF2Header)) {
    TraceLog(LOG_ERROR, "LoadPSF2Font: file too small: %s", filename);
    free(buf); return font;
  }

  PSF2Header hdr;
  memcpy(&hdr, buf, sizeof(hdr));
  if (memcmp(hdr.magic, PSF2_MAGIC, 4) != 0) {
    TraceLog(LOG_ERROR, "LoadPSF2Font: invalid PSF2 header in %s", filename);
    free(buf); return font;
  }

  int numGlyphs     = (int)hdr.numglyph;
  int bytesPerGlyph = (int)hdr.bytesperglyph;
  int glyphH        = (int)hdr.height;
  int glyphW        = (int)hdr.width;
  int bytesPerRow   = (glyphW + 7) / 8;
  size_t data_offset = (size_t)hdr.headersize;

  if (buf_size < data_offset + (size_t)numGlyphs * (size_t)bytesPerGlyph) {
    TraceLog(LOG_ERROR, "LoadPSF2Font: file truncated: %s", filename);
    free(buf); return font;
  }

  uint8_t *glyphData = buf + data_offset;

  // テクスチャアトラス: 16列×N行
  int atlasW    = 16 * glyphW;
  int atlasRows = (numGlyphs + 15) / 16;
  int atlasH    = atlasRows * glyphH;

  Color *pixels = calloc((size_t)(atlasW * atlasH), sizeof(Color));
  if (!pixels) { free(buf); return font; }

  for (int g = 0; g < numGlyphs; g++) {
    int baseX = (g % 16) * glyphW;
    int baseY = (g / 16) * glyphH;
    uint8_t *glyph = glyphData + g * bytesPerGlyph;
    for (int y = 0; y < glyphH; y++) {
      for (int x = 0; x < glyphW; x++) {
        int bit = (glyph[y * bytesPerRow + x / 8] >> (7 - (x % 8))) & 1;
        if (bit) pixels[(baseY + y) * atlasW + (baseX + x)] = WHITE;
      }
    }
  }

  Image atlas = {
    .data    = pixels,
    .width   = atlasW,
    .height  = atlasH,
    .mipmaps = 1,
    .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
  };
  font.texture = LoadTextureFromImage(atlas);
  UnloadImage(atlas); // atlas.data(=pixels) をここで解放
  free(buf);

  font.baseSize     = glyphH;
  font.glyphCount   = numGlyphs;
  font.glyphPadding = 0;
  font.recs   = malloc((size_t)numGlyphs * sizeof(Rectangle));
  font.glyphs = malloc((size_t)numGlyphs * sizeof(GlyphInfo));

  for (int g = 0; g < numGlyphs; g++) {
    int col = g % 16;
    int row = g / 16;
    font.recs[g] = (Rectangle){(float)(col * glyphW), (float)(row * glyphH),
                                (float)glyphW, (float)glyphH};
    font.glyphs[g] = (GlyphInfo){
      .value    = g,
      .offsetX  = 0,
      .offsetY  = 0,
      .advanceX = glyphW,
      .image    = {0},
    };
  }

  TraceLog(LOG_INFO, "LoadPSF2Font: loaded %d glyphs (%dx%d) from %s",
           numGlyphs, glyphW, glyphH, filename);
  return font;
}
