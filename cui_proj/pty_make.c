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
#include<sys/ioctl.h>
#define ESC_PAL_MAX 32
#define cur_font_load_max 32
struct pos{
  int w;
  int h;
};
struct cur_mgr{
  char *cur_font;
  int load_cur_font_n;
};
struct cursor{
    char *shape;
    int color;
    struct {
      bool blinking;
      double speed_ms;
      bool now_right;
    }lighting;
    struct{
      int w;
      int h;
    }cur_pos;
    Font font;
};
struct csi_data{
  int pal_count;
  char last_chr; 
};
enum last_chr_mode{
  line_down,
  str_end,
  none
};
enum parse_state{
  GROUND,//通常モード（届いた文字をそのまま画面に描画する)
  SQE_START,
};
enum mode_state{
  CSI_MODE,//引数を収集中	数字やセミコロンをメモリに溜める。
  OSC_MODE,//終端文字を受信	溜めた引数を使って、実際の命令（色変更など）を実行する。
  IDK
};
enum pal_p{
  st,
  ed
};
enum esc_state{
  CSI,//画面制御系([)文字
  ESC_UNIT,//ESC単体コマンド
  OSC//ターミナルの設定系
};
enum visiavle_chr{
  YES,
  NO,
  BS_ST1,
  BS_ST2,
  BS_ST3
};

struct data{
  enum parse_state parce_state;
  enum esc_state esc_state;
  unsigned char esc_pal[32];//ESC内パラメーター
  unsigned char last_char;//終了文字
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
void osc_mode(char *win_title,char *buff,int *palms,int palms_counter,struct term_cell *term_cell,char *osc_pal_chr);
void ls_chr_parse(struct term_cell *term_cell, char buff, int *palms, int palms_counter, Color *now_fg_color, Color *now_bg_color,struct cursor *cur,struct pos term_size,bool is_private,struct cursor *save_cur,struct term_cell *alt_term_cell,bool *paste_mode);
struct csi_data csi_mode_pal_parse(char *buff,int *i,int size);
void read_counter_inc(int *i,int size);
enum mode_state get_mode(char *buff,int *i,int size);
void csi_mode_parse(char *buff,int *i,int size);
enum visiavle_chr check_visible_chr(char buff);
void bs_st1(struct pos *pos,char *buff,int cols,int *buff_counter,unsigned int *bash_line_total_ciunt);
void cur_font_set(struct cursor *cur,struct cur_mgr *cur_mgr,int n);
void cur_set_default(struct cur_mgr *cur_mgr);
void cur_mgr_free(struct cur_mgr *cur_mgr);
int init_cur_mgr(struct cur_mgr *cur_mgr);
void load_cur_font(struct cur_mgr *cur_mgr);
bool IsAnyKeyReleased(void);
void draw_cursor(struct cursor *cur,double *current_time);
void bash_str_parse(char *buff, ssize_t size, int *palms, int *palms_counter, struct term_cell *term_cell, struct cursor *cur, struct pos term_size,struct cursor *save_cur,
  struct term_cell *alt_term_cell,bool *paste_mode,char *win_title);
char ** split_line(int cols,char *buff_str);
char *mymemcpy(char *start,char*end,enum last_chr_mode mode);
char input_bash(char *n);
int check_key();
enum parse_state buff_state_check(char buff,enum parse_state now_state);
int main(void){
  int master_fd,slave_fd;
  char slavename[256];
  int scr_h=500;
  int scr_w=500;
  int str_start_pos_x = 3;//文字の表示開始座標X
  char *window_title=malloc(128);
  window_title="bash";
  InitWindow(scr_w,scr_h,window_title);
  if(!IsWindowReady()){
    printf("window error");
    return 0;
  }
  SetTargetFPS(60);
  Font myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf",256, NULL, 0);
  SetTextureFilter(myfont.texture, TEXTURE_FILTER_POINT);
  struct pos term_size;
  int cols = (int)(scr_w-str_start_pos_x)/8;
  int rows = scr_h/16;
  term_size.h=rows;
  term_size.w=cols;
  int total=cols*rows;
  
  // 元のターミナルの設定をコピーし、安全なウィンドウサイズを指定する
  struct termios term;
  struct termios *term_ptr = NULL;
  struct winsize ws;
  ws.ws_col = cols; // 横幅 (壁の位置)
  ws.ws_row = rows; // 縦幅

  if(openpty(&master_fd,&slave_fd,slavename,term_ptr,&ws)==-1){
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
  if(pid_id ==-1){
    perror("forkに失敗しました");
        exit(EXIT_FAILURE);
  }
  else if(pid_id == 0){
    close(master_fd);
    setsid();
    ioctl(slave_fd, TIOCSCTTY, 0);
    //ファイルディスクリプタのすり替え
    dup2(slave_fd, STDIN_FILENO);  // 標準入力 (0) を slave_fd に
    dup2(slave_fd, STDOUT_FILENO); // 標準出力 (1) を slave_fd に
    dup2(slave_fd, STDERR_FILENO); // 標準エラー (2) を slave_fd に
    // 繋ぎ変えが終わったら、元の slave_fd は不要なので閉じる
    close(slave_fd);
    setenv("TERM", "xterm-256color",1);
    execlp("bash", "bash", "-i", NULL);

    perror("bashの起動に失敗しました");
    exit(EXIT_FAILURE);
  }
  else{
    close(slave_fd); // 親プロセス（マスター側）では slave_fd は不要なので即座に閉じる
  }
  //readを非ブロッキングにする
  fcntl(master_fd, F_SETFL, O_NONBLOCK);
  struct cursor cur;
  struct cursor save_cur;
  struct cur_mgr cur_mg;
  struct term_cell *main_term_cell = calloc(total, sizeof(struct term_cell));
  struct term_cell *alt_term_cell=NULL;
  Vector2 str_pos;
  int n=0;
  int palms[16];
  int palms_counter=0;
  double current_time=0;
  char *read_buf=malloc(total);
  bool paste_mode=false;
  str_pos.x=str_start_pos_x;
  str_pos.y=0;
  cur.shape=malloc(2);
  cur.lighting.blinking=true;
  cur.lighting.speed_ms=500;
  cur.font=myfont;
  cur.lighting.now_right=0;
  cur.cur_pos.w = 0;
  cur.cur_pos.h = 0;
  int result=init_cur_mgr(&cur_mg);
  if(result==1){
    perror("can not init cur_mgr");
    return 0;
  }
  load_cur_font(&cur_mg);
  cur_font_set(&cur,&cur_mg,1);
  while(!WindowShouldClose()){
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char* clip_bord_chr = GetClipboardText();
        // クリップボードが空でない場合のみPTY（擬似端末）へ書き込む
        if (clip_bord_chr != NULL&& strlen(clip_bord_chr) > 0){   
          if(paste_mode){
            write(master_fd, "\x1b[200~", strlen("\x1b[200~"));
            write(master_fd, clip_bord_chr, strlen(clip_bord_chr));
            write(master_fd, "\x1b[201~", strlen("\x1b[201~"));
          }
          else write(master_fd, clip_bord_chr, strlen(clip_bord_chr));
      }
    }
    else {
      while((n = GetCharPressed()) > 0){
        // 制御文字(32未満)やDEL(127)は二重送信を防ぐため無視する
        if(n < 32 || n == 127) continue; 
        else if(IsAnyKeyReleased()) break;
        char c = n;
        write(master_fd, &c, 1);
      }
    }
    if(IsKeyPressed(KEY_ENTER)){
      char enter_key = 13;
      write(master_fd, &enter_key, 1);
    }
    if(IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)){
      char c = 0x7f; 
      write(master_fd, &c, 1);
    }
    while(1){
      ssize_t buf_size = read(master_fd, read_buf, total - 1);
      if(buf_size > 0){
        bash_str_parse(read_buf, buf_size, palms, &palms_counter, main_term_cell, &cur, term_size,&save_cur,alt_term_cell,&paste_mode,window_title);
      }
      else break;
    }
    BeginDrawing();
    ClearBackground(BLACK);
    for(int i=0;i<rows;i++){
      for(int j=0;j<cols;j++){
        int idx=i*cols+j;
        char c=main_term_cell[idx].character;
        if (c == 0) c = ' '; // 未設定のセルはスペースとして描画
        char char_str[2] = { c, '\0' };
        Color fg = main_term_cell[idx].fg_color.a == 0 ? WHITE : main_term_cell[idx].fg_color;
        Color bg = main_term_cell[idx].bg_color.a == 0 ? BLACK : main_term_cell[idx].bg_color;
        if (bg.r != 0 || bg.g != 0 || bg.b != 0) {
           DrawRectangle(j*8, i*16, 8, 16, bg);
        }
        DrawTextEx(myfont, char_str, (Vector2){3+j*8, i*16}, 16, 0, fg);
      }
    }
    draw_cursor(&cur,&current_time);
    EndDrawing();
    fflush(stdout); // バッファに溜めず即座にコンソールに反映させる
  }
  close(master_fd);
  CloseWindow();
}
void bash_str_parse(char *buff, ssize_t size, int *palms, int *palms_counter, struct term_cell *term_cell, struct cursor *cur, struct pos term_size,struct cursor *save_cur,struct term_cell *alt_term_cell,bool *paste_mode,char *win_title) {
    static enum parse_state state = GROUND;
    static enum mode_state mode=IDK;
    static char osc_pal_chr[64];
    static int osc_pal_chr_counter=0;
    static int val=0;
    static bool is_private=false;
    static bool has_val=0;
    static Color now_fg_color=WHITE;
    static Color now_bg_color=BLACK;
    for (int i = 0; i < size; i++) {
      if(state==SQE_START){
        if (mode == IDK) {
          mode = get_mode(buff, &i, size);
          continue; 
        }
        switch (mode) {
          case OSC_MODE:
              if(buff[i] >= '0' && buff[i] <= '9') {
                  val = val * 10 + (buff[i] - '0');
                  has_val = true;
              }else if (buff[i] == ';') {
                  if (!has_val) val = 0;
                  if (*palms_counter < 16) palms[(*palms_counter)++] = val;
                  val = 0;
                  has_val = false;
              }else if (buff[i] >= 0x40 && buff[i] <= 0x7E){
                osc_pal_chr[osc_pal_chr_counter++]=buff[i];
              }
              else if(buff[i]=='\a'){
                if (has_val) {
                      if (*palms_counter < 16) palms[(*palms_counter)++] = val;
                  } else if (*palms_counter == 0) {
                      palms[0] = 0;
                      *palms_counter = 1;
                }
                osc_pal_chr[osc_pal_chr_counter]='\0';
                osc_mode(win_title,buff,palms,*palms_counter,term_cell,&osc_pal_chr[0]);
                osc_pal_chr_counter=0;
                *palms_counter = 0;
                val = 0;
                has_val = false;
                state = GROUND;
                mode = IDK;
              }    
              continue;
              break;
          case CSI_MODE:
              if(buff[i] >= '0' && buff[i] <= '9') {
                  val = val * 10 + (buff[i] - '0');
                  has_val = true;
              }else if (buff[i] == ';') {
                  if (!has_val) val = 0;
                  if (*palms_counter < 16) palms[(*palms_counter)++] = val;
                  val = 0;
                  has_val = false;
              }else if(buff[i]=='?')is_private=true;
              else if (buff[i] >= 0x40 && buff[i] <= 0x7E) {
                  if (has_val) {
                      if (*palms_counter < 16) palms[(*palms_counter)++] = val;
                  } else if (*palms_counter == 0) {
                      palms[0] = 0;
                      *palms_counter = 1;
                  }
                  ls_chr_parse(term_cell, buff[i], palms, *palms_counter, &now_fg_color, &now_bg_color, cur, term_size,is_private,save_cur,alt_term_cell,paste_mode);
                  *palms_counter = 0;
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
      else if(state == GROUND){
        state = buff_state_check(buff[i], state);
        if(state==SQE_START)continue;
        if (buff[i] == '\b') {
            if (cur->cur_pos.w > 0) cur->cur_pos.w--;
            continue;
        } else if (buff[i] == '\r') {
            cur->cur_pos.w = 0;
            continue;
        } else if (buff[i] == '\n') {
            cur->cur_pos.w = 0;
            cur->cur_pos.h++;
        } else if (buff[i] == '\a') {
            continue;
        } else {
            int idx = cur->cur_pos.h * term_size.w + cur->cur_pos.w;
            if (idx >= 0 && idx < term_size.h * term_size.w) {
                term_cell[idx].character = buff[i];
                term_cell[idx].fg_color = now_fg_color;
                term_cell[idx].bg_color = now_bg_color;
            }
            cur->cur_pos.w++;
            if (cur->cur_pos.w >= term_size.w) {
                cur->cur_pos.w = 0;
                cur->cur_pos.h++;
            }
        }
        // 画面外スクロール処理
        if (cur->cur_pos.h >= term_size.h) {
            int total_cells = term_size.w * term_size.h;
            memmove(term_cell, term_cell + term_size.w, (total_cells - term_size.w) * sizeof(struct term_cell));
            for (int c = 0; c < term_size.w; c++) {
                int last_line_idx = (term_size.h - 1) * term_size.w + c;
                term_cell[last_line_idx].character = ' ';
                term_cell[last_line_idx].bg_color = BLACK;
                term_cell[last_line_idx].fg_color = WHITE;
            }
            cur->cur_pos.h = term_size.h - 1;
        }
      }
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
void draw_cursor(struct cursor *cur,double *current_time){
  if(!cur->lighting.blinking)return;
  double now_time=GetTime();
  if(now_time-*current_time>=cur->lighting.speed_ms/1000){
    *current_time=now_time;
    cur->lighting.now_right=!cur->lighting.now_right;
  }
  if(cur->lighting.now_right){
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
    for (int i = 32; i <= 126; i++)if(IsKeyReleased(i))return true;
    if (IsKeyReleased(KEY_ENTER) || IsKeyReleased(KEY_BACKSPACE)) return true;
    return false;
}
void load_cur_font(struct cur_mgr *cur_mgr){
  if(cur_mgr==NULL){
    printf("cur_mgr is not init");
    exit(1);
  }
  FILE *cur_load=fopen("cur_font.txt","r");
  if(cur_load==NULL){
    cur_set_default(cur_mgr);
    return;
  }
  for(int i=0;i<cur_font_load_max;i++){
    cur_mgr->load_cur_font_n=i;
    int in=fgetc(cur_load);
    if(in==EOF)break;
    char c=(char)in;
    cur_mgr->cur_font[i]=c;
  }
}
int init_cur_mgr(struct cur_mgr *cur_mgr){
  cur_mgr->cur_font=malloc(sizeof(char)*cur_font_load_max);
  if(cur_mgr->cur_font==NULL){
    printf("cur_mgr malloc error");
    exit(1);
  }
  return 0;
}
void cur_mgr_free(struct cur_mgr *cur_mgr){
  free(cur_mgr);
}
void cur_set_default(struct cur_mgr *cur_mgr){
  cur_mgr->load_cur_font_n=2;
  cur_mgr->cur_font[0]='|';
  cur_mgr->cur_font[1]='/';
}
void cur_font_set(struct cursor *cur,struct cur_mgr *cur_mgr,int n){
  if(n>cur_mgr->load_cur_font_n){
    printf("your chose cur font nonber is big then cur_font_load_max");
    exit(1);
  }
  cur->shape[0]=cur_mgr->cur_font[n-1];
  cur->shape[1]='\0';
}
enum parse_state buff_state_check(char buff,enum parse_state now_state){
  enum parse_state return_state=now_state;
  if(buff=='\x1b' && return_state==GROUND)return_state=SQE_START;
  return return_state;
}
enum visiavle_chr check_visible_chr(char buff){
  enum visiavle_chr vis_state;
  if(buff=='\b')vis_state=BS_ST1;
  else if(buff=='\r')vis_state=NO;
  else vis_state=YES;
  return vis_state;
}
enum mode_state get_mode(char *buff,int *i,int size){
  enum mode_state return_state;
  if(buff[*i]==']')return_state=OSC_MODE;
  else if(buff[*i]=='[')return_state=CSI_MODE;
  else return_state=IDK;
  return return_state;
}
void read_counter_inc(int *i,int size){
  if(*i<size)i++;
}
void ls_chr_parse(struct term_cell *term_cell, char buff, int *palms, int palms_counter, Color *now_fg_color, Color *now_bg_color, struct cursor *cur, struct pos term_size,bool is_private,struct cursor *save_cur,struct term_cell *alt_term_cell,bool *paste_mode) {
  switch(buff){
    case 'm': // SGR (Select Graphic Rendition) - 文字の色やスタイルを変更
      for (int i = 0; i < palms_counter; i++) {
        int code = palms[i];
        if (code == 0) { // リセット
          *now_fg_color = WHITE;
          *now_bg_color = BLACK;
        } else if (code >= 30 && code <= 37) { // 前景色
          Color colors[] = {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, SKYBLUE, WHITE};
          *now_fg_color = colors[code - 30];
        } else if (code >= 40 && code <= 47) { // 背景色
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
      // 画面の範囲内に収める
      if(row >= term_size.h) row = term_size.h - 1;
      if(col >= term_size.w) col = term_size.w - 1;
      
      cur->cur_pos.h = row;
      cur->cur_pos.w = col;
      break;
    }
    case 'J':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      // mode 2: 画面全体を消去 (本来はmode 0, 1の対応も必要)
      if (mode == 2) {
        for(int i = 0; i < (term_size.h * term_size.w); i++){
          term_cell[i].character = ' ';
          term_cell[i].bg_color = *now_bg_color;
        }
      }
      break;
    }
    case 'K':
    {
      int mode = (palms_counter > 0) ? palms[0] : 0;
      if (mode == 2) {
        // 行全体消去
        int line_start = cur->cur_pos.h * term_size.w;
        for(int i = 0; i < term_size.w; i++){
          term_cell[line_start + i].character = ' ';
          term_cell[line_start + i].bg_color = *now_bg_color;
        }
      } else if (mode == 0) {
        // カーソル位置から行末まで消去
        int start = cur->cur_pos.h * term_size.w + cur->cur_pos.w;
        int end = (cur->cur_pos.h + 1) * term_size.w;
        for (int i = start; i < end; i++) {
          term_cell[i].character = ' ';
          term_cell[i].bg_color = *now_bg_color;
        }
      }
      break;
    }
    case 'A':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      cur->cur_pos.h -= n;
      if (cur->cur_pos.h < 0) cur->cur_pos.h = 0;
      break;
    }
    case 'B':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      cur->cur_pos.h += n;
      if (cur->cur_pos.h >= term_size.h) cur->cur_pos.h = term_size.h - 1;
      break;
    }
    case 'C':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      cur->cur_pos.w += n;
      if (cur->cur_pos.w >= term_size.w) cur->cur_pos.w = term_size.w - 1;
      break;
    }
    case 'D':
    {
      int n = (palms_counter > 0 && palms[0] > 0) ? palms[0] : 1;
      cur->cur_pos.w -= n;
      if (cur->cur_pos.w < 0) cur->cur_pos.w = 0;
      break;
    }
    case 'h':
    case 'l':
     if(is_private && palms_counter > 0) {
        bool is_on = (buff == 'h'); // h なら true(オン)、l なら false(オフ)
        switch (palms[0]) {
          case 25:
            // カーソルの表示・非表示
            cur->lighting.blinking = is_on; 
            break;
          case 1049:
            // 代替画面の切り替え
            if(is_on){
              *save_cur=*cur;
              if(alt_term_cell!=NULL)free(alt_term_cell);     
              alt_term_cell=malloc(sizeof(struct term_cell)*(term_size.h*term_size.w));         
              memcpy(alt_term_cell,term_cell,term_size.h*term_size.w*sizeof(struct term_cell));
              for (int i = 0; i < (term_size.h * term_size.w); i++) {
                term_cell[i].character = ' ';
                term_cell[i].fg_color = WHITE;
                term_cell[i].bg_color = BLACK;
              }
            }else{
              if(alt_term_cell!=NULL){
                memcpy(term_cell, alt_term_cell,term_size.h*term_size.w*sizeof(struct term_cell));
                free(alt_term_cell);
                *cur=*save_cur;
              }
              *cur=*save_cur;
            } 
            break;
            
          case 2004:
            if(is_on)*paste_mode=true;
            else *paste_mode=false;
            break;
          default:
            // 未対応の番号が来たら、エラーにせず優しく無視する
            break;
        }
      }
      break;
    default:
      // 未実装のCSIシーケンス
      break;
  }
}
void osc_mode(char *win_title,char *buff,int *palms,int palms_counter,struct term_cell *term_cell,char *osc_pal_chr){
  switch(palms[0]){
    case 0:
    case 2:
      //SetWindowTitle(osc_pal_chr);
      break;
    default:
      break;
  }
}
