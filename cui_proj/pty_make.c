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
enum last_chr_mode{
  line_down,
  str_end,
  none
};
enum parse_state{
  GROUND,//通常モード（届いた文字をそのまま画面に描画する)
  ESC_MODE,//ESC (0x1b) を受信直後	次の文字を見て、CSI（[）かOSC（]）かを判断する。
  CSI_MODE,//引数を収集中	数字やセミコロンをメモリに溜める。
  OSC_MODE,//終端文字を受信	溜めた引数を使って、実際の命令（色変更など）を実行する。
  SQE_START,
  PRV_MODE
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
enum visiavle_chr check_visible_chr(char buff);
void bs_st1(struct pos *pos,char *buff,int cols,int *buff_counter,unsigned int *bash_line_total_ciunt);
void cur_font_set(struct cursor *cur,struct cur_mgr *cur_mgr,int n);
void cur_set_default(struct cur_mgr *cur_mgr);
void cur_mgr_free(struct cur_mgr *cur_mgr);
int init_cur_mgr(struct cur_mgr *cur_mgr);
void load_cur_font(struct cur_mgr *cur_mgr);
bool IsAnyKeyReleased(void);
void draw_cursor(struct cursor *cur,double *current_time);
char *bash_str_parse(char *buff,ssize_t size,unsigned int **change_line_pos,int *cols,struct pos *reading_pos,unsigned int *bash_line_total_ciunt,enum visiavle_chr *vis_state);
char ** split_line(int cols,char *buff_str);
char *mymemcpy(char *start,char*end,enum last_chr_mode mode);
char input_bash(char *n);
int check_key();
enum parse_state buff_state_check(char buff,enum parse_state now_state);
int main(void){
  int master_fd,slave_fd;
  char slavename[256];

  int scr_h=400;
  int scr_w=400;
  int str_start_pos_x = 3;//文字の表示開始座標X
  InitWindow(scr_w,scr_h, "bash");
  if(!IsWindowReady()){
    printf("window error");
    return 0;
  }
  scr_h=500;
  scr_w=500;
  SetTargetFPS(60);
  Font myfont = LoadFontEx("/usr/share/fonts/TTF/TerminusTTF.ttf",256, NULL, 0);
  SetTextureFilter(myfont.texture, TEXTURE_FILTER_POINT);
  int cols = (int)(scr_w-str_start_pos_x)/8;
  int rows = scr_h/16;
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
  struct cur_mgr cur_mg;
  struct pos now_reading_chr_pos={0,0};
  Vector2 str_pos;
  int n=0;
  int clean_buff_counter=0;
  double current_time=0;
  char **clean_buff_line_splited=NULL;
  char *read_buf=malloc(total);
  char *clean_buff=malloc(total);
  char **temp=realloc(clean_buff_line_splited,sizeof(char*)*(rows+1));
  unsigned int *line_down_pos=calloc(rows+1,sizeof(unsigned int));
  unsigned int bash_line_total_ciunt=0;
  if(read_buf ==NULL || clean_buff==NULL || line_down_pos==NULL){
    printf("read buff or clean buff error");
    return 0;
  }
  if(temp==NULL){
    printf("change_line_pos realloc error");
    return 0;
  }
  str_pos.x=str_start_pos_x;
  str_pos.y=0;
  clean_buff_line_splited=temp;
  cur.shape=malloc(2);
  cur.lighting.blinking=false;
  cur.lighting.speed_ms=500;
  cur.font=myfont;
  cur.lighting.now_right=0;
  int result=init_cur_mgr(&cur_mg);
  if(result==1){
    perror("can not init cur_mgr");
    return 0;
  }
  load_cur_font(&cur_mg);
  cur_font_set(&cur,&cur_mg,1);
  enum visiavle_chr vis_state=YES;
  while(!WindowShouldClose()){
    Vector2 current_pos = { (float)str_start_pos_x, (float)(0) };
    // 入力処理
    //英字、英数字、記号処理
    while((n=GetCharPressed())>0){
      if(n == 13 || n == 10)continue; 
      else if(IsAnyKeyReleased())break;
      char c=n;
      write(master_fd,&c, 1);
      printf("%d\n",n);
    } 
    if(IsKeyPressed(KEY_ENTER)){
      char enter_key=13;
      write(master_fd,&enter_key,1);
    }
    if(IsKeyPressed(KEY_BACKSPACE)){
      char c=0x7f;
      write(master_fd,&c,1);
    }
    while(1){
      ssize_t buf_size = read(master_fd, read_buf, total - 1);
      if(buf_size>0){
        char *temp_str=bash_str_parse(read_buf,buf_size,&line_down_pos,&cols,&now_reading_chr_pos,&bash_line_total_ciunt,&vis_state);
        size_t temp_size=strlen(temp_str);
        clean_buff_counter+=temp_size;
        if(clean_buff_counter>=total){
          printf("memory error");
          break;
        }
        memcpy(clean_buff+clean_buff_counter-temp_size,temp_str,temp_size);
        clean_buff[clean_buff_counter]='\0';
        free(temp_str);
      }
      else break;
    }
    if(now_reading_chr_pos.h>=rows){
      unsigned int first_line_len = line_down_pos[0]; 
      memmove(clean_buff, &clean_buff[first_line_len], clean_buff_counter - first_line_len + 1);
      memmove(line_down_pos, &line_down_pos[1], sizeof(line_down_pos[0]) * (now_reading_chr_pos.h - 1));
      for(int i = 0; i <now_reading_chr_pos.h - 1; i++)line_down_pos[i] -= first_line_len; 
      clean_buff_counter-=first_line_len;
      bash_line_total_ciunt-= first_line_len;
      now_reading_chr_pos.h--;
    }
    for(int i=0;i<now_reading_chr_pos.h;i++){
      int str_len =(i==0) ? 0:line_down_pos[i-1];
      clean_buff_line_splited[i]=mymemcpy(clean_buff + str_len, clean_buff + line_down_pos[i],line_down);
    }
    bool win_r=IsWindowResized();
    if(win_r){
      scr_w=GetScreenWidth();
      scr_h=GetScreenHeight();
      cols=(int)(scr_w-str_start_pos_x)/8;
      rows=scr_h/16;
      total=cols*rows;
      ws.ws_col = cols; // 横幅 (壁の位置)
      ws.ws_row = rows; // 縦幅
      ioctl(slave_fd, TIOCSWINSZ, &ws);
      SetWindowSize(cols,rows);
    }
    int last_start = (now_reading_chr_pos.h==0) ? 0 : line_down_pos[now_reading_chr_pos.h-1];
    clean_buff_line_splited[now_reading_chr_pos.h] = mymemcpy(clean_buff + last_start, clean_buff + clean_buff_counter, none);
    Vector2 draw_pos={current_pos.x,0};
    int pos_x=0;
    BeginDrawing();
    ClearBackground(BLACK);
    //bash受信文字
    for(int i=0;i<=now_reading_chr_pos.h;i++){
      draw_pos.y=i*16;
      DrawTextEx(
        myfont,
        clean_buff_line_splited[i],
        draw_pos, 
        16, 
        0, 
        WHITE
      );
      if(i>=now_reading_chr_pos.h){
        pos_x=strlen(clean_buff_line_splited[i]);
        break;
      }
      free(clean_buff_line_splited[i]);
    }
    //クライアント実装エフェクト
    cur.cur_pos.w=pos_x*8+1;
    cur.cur_pos.h=draw_pos.y;
    draw_cursor(&cur,&current_time);
    EndDrawing();
    fflush(stdout); // バッファに溜めず即座にコンソールに反映させる
  }
  free(clean_buff);
  close(master_fd);
  CloseWindow();
}
char *bash_str_parse(char *buff,ssize_t size,unsigned int **change_line_pos,int *cols,struct pos *reading_pos,unsigned int *bash_line_total_ciunt,enum visiavle_chr *vis_state){
  char *return_buff=malloc(size+1);
  int buff_counter=0;
  static enum parse_state state=GROUND;
  for(int i=0;i<size;i++){
    printf("%c ",buff[i]);
    enum parse_state old_state=state;
    state=buff_state_check(buff[i],state);
    if(!(old_state==GROUND && state==GROUND))continue;
    *vis_state=check_visible_chr(buff[i]);
    if(*vis_state==BS_ST1){
      bs_st1(reading_pos,return_buff,*cols,&buff_counter,bash_line_total_ciunt);
      continue;
    }
    //改行か、壁まで文字が入力されたら
    if(buff[i]=='\n' || reading_pos->w > *cols){
        (*change_line_pos)[reading_pos->h++]=*bash_line_total_ciunt;
        (reading_pos->w)=0;
        if(buff[i]=='\n')continue;
    }
    return_buff[buff_counter++]=buff[i];
    (*bash_line_total_ciunt)++;
    (reading_pos->w)++;
  }
  return_buff[buff_counter]='\0';
  char *temp_buff=realloc(return_buff,buff_counter+1);
  if(temp_buff==NULL){
    perror("shrunk_buff malloc error");
    exit(1);
  }
  return_buff=temp_buff;
  return return_buff;
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
  double now_time=GetTime();
  if(now_time-*current_time>=cur->lighting.speed_ms/1000){
    *current_time=now_time;
    cur->lighting.now_right=!cur->lighting.now_right;
  }
  if(cur->lighting.now_right){
    DrawTextEx(
      cur->font,
      cur->shape,
      (Vector2){cur->cur_pos.w,cur->cur_pos.h},
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
  if(return_state==GROUND){
    if(buff=='\x1b')return_state=SQE_START;
  }
  else if(return_state==SQE_START){
    if(buff==']')return_state=OSC_MODE;
    else if(buff=='[')return_state=CSI_MODE;
    else return_state = GROUND;
  }
  else if(return_state==CSI_MODE){
    if(buff=='?')return_state=PRV_MODE;
    if(buff>=0x40 && buff<=0x7E)return_state=GROUND;
  }
  else if(return_state==OSC_MODE){
    if(buff=='\a')return_state=GROUND;
  }
  else if(return_state==PRV_MODE){
    if(buff>=0x40 && buff<=0x7E)return_state=GROUND;
  }
  return return_state;
}
enum visiavle_chr check_visible_chr(char buff){
  enum visiavle_chr vis_state;
  if(buff=='\b')vis_state=BS_ST1;
  else if(buff=='\r')vis_state=NO;
  else vis_state=YES;
  return vis_state;
}
void bs_st1(struct pos *pos,char *buff,int cols,int *buff_counter,unsigned int *bash_line_total_ciunt){
  if (*buff_counter <= 0) return;
  if(pos->w>0){
    (*buff_counter)--;
    pos->w--;
  }
  else if(pos->h>0){
    pos->h--;
    pos->w=cols;
    (*buff_counter)--;
  }
}
