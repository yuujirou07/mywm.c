#include "raylib.h"
#include <bits/types/siginfo_t.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
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
struct cursor{
    char *shape;
    int color;
    struct {
      bool blinking;
      double speed_ms;
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
  SQE_START
};
enum esc_state{
  CSI,//画面制御系([)文字
  ESC_UNIT,//ESC単体コマンド
  OSC//ターミナルの設定系
};

struct data{
  enum parse_state parce_state;
  enum esc_state esc_state;
  unsigned char esc_pal[32];//ESC内パラメーター
  unsigned char last_char;//終了文字
};
bool IsAnyKeyReleased(void);
void draw_cursor(struct cursor *cur);
char* bash_str_parse(char *buf,ssize_t buf_size,unsigned int **change_line_pos,int *cols,int *w,int *h);
char ** split_line(int cols,char *buff_str);
char *mymemcpy(char *start,char*end,enum last_chr_mode mode);
char input_bash(char *n);
int check_key();
unsigned int bash_line_total_ciunt=0;
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
  scr_h=GetScreenHeight();
  scr_w=GetScreenWidth();
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
  char kb_buf[total];
  kb_buf[0]='\0';
  int n=0;
  Vector2 str_pos;
  str_pos.x=str_start_pos_x;
  str_pos.y=0;
  char *read_buf=malloc(sizeof(unsigned char)*total);
  char *clean_buff=malloc(sizeof(char)*total);
  if(read_buf ==NULL || clean_buff==NULL){
    printf("read buff or clean buff error");
    return 0;
  }
  int clean_buff_counter=0;
  int w=0;
  int h=0;
  unsigned int *line_down_pos=NULL;
  char **clean_buff_line_splited=NULL;
  struct cursor cur;
  char **temp=realloc(clean_buff_line_splited,sizeof(char*)*rows);
  if(temp==NULL){
    printf("change_line_pos realloc error");
    return 0;
  }
  clean_buff_line_splited=temp;
  cur.shape="|";
  cur.lighting.blinking=false;
  cur.lighting.speed_ms=500;
  cur.font=myfont;
  while(!WindowShouldClose()){
    Vector2 current_pos = { (float)str_start_pos_x, (float)(0) };
    // 入力処理
    //英字、英数字、記号処理
    while((n=GetCharPressed())>0){
      if(IsAnyKeyReleased())break;
      char c=n;
      write(master_fd,&c, 1);
    }    
    if (IsKeyPressed(KEY_ENTER)) {
      char c = '\r'; // PTYドライバが自動で \n に変換するため、\r のみ送信する
      write(master_fd, &c, 1);
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
      char c = 0x7f; // ASCII DEL
      write(master_fd, &c, 1);
    }
    
    while(1){
      ssize_t buf_size = read(master_fd, read_buf, total - 1);
      if(buf_size>0){
        char *temp_str=bash_str_parse(read_buf,buf_size,&line_down_pos,&cols,&w,&h);
        size_t temp_size=strlen(temp_str);
        clean_buff_counter+=temp_size;
        if(clean_buff_counter>=total){
          printf("memory error");
          break;
        }
        memcpy(clean_buff+clean_buff_counter-temp_size,temp_str,temp_size);
        clean_buff[clean_buff_counter+1]='\0';
        printf("[DEBUG Read] PTY received: %s\n", read_buf); // ← 追加
        printf("%s\n", temp_str); // Bashから来た文字列をそのまま出力（余分な改行を避ける）
        fflush(stdout); // バッファに溜めず即座にコンソールに反映させる
        free(temp_str);
      }
      else break;
    }
    if(h>=rows){
      unsigned int first_line_len = line_down_pos[0]; 
      memmove(clean_buff, &clean_buff[first_line_len], clean_buff_counter - first_line_len + 1);
      memmove(line_down_pos, &line_down_pos[1], sizeof(line_down_pos[0]) * (h - 1));

      for(int i = 0; i < h - 1; i++){
          line_down_pos[i] -= first_line_len;
      }
      clean_buff_counter -= first_line_len;
      bash_line_total_ciunt -= first_line_len;
      h--;
    }
    for(int i=0;i<h;i++){
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




    int last_start = (h==0) ? 0 : line_down_pos[h-1];
    clean_buff_line_splited[h] = mymemcpy(clean_buff + last_start, clean_buff + clean_buff_counter, none);
    Vector2 draw_pos={current_pos.x,0};
    BeginDrawing();
    ClearBackground(BLACK);
    //bash受信文字
    for(int i=0;i<=h;i++){
      draw_pos.y=i*16;
      DrawTextEx(
        myfont,
        clean_buff_line_splited[i],
        draw_pos, 
        16, 
        0, 
        WHITE
      );
      if(i>=h)break;
    }
    //クライアント実装エフェクト
    int pos_x=strlen(clean_buff_line_splited[h]);
    cur.cur_pos.w=pos_x*8+1;
    cur.cur_pos.h=draw_pos.y;
    draw_cursor(&cur);
    EndDrawing();

  }
  free(clean_buff);
  close(master_fd);
  CloseWindow();
}
char *bash_str_parse(char *buff,ssize_t size,unsigned int **change_line_pos,int *cols,int *w,int *h){
  char *return_buff=malloc(size+1);
  int buff_counter=0;
  static enum parse_state state=GROUND;
  for(int i=0;i<size;i++){
    if(state==GROUND && buff[i]=='\x1b'){
      state=SQE_START;
      continue;
    }
    else if(state==SQE_START){
      if(buff[i]==']'){
        state=OSC_MODE;
        continue;
      }
      else if(buff[i]=='['){
        state=CSI_MODE;
        continue;
      }
      else state = GROUND;
    }
    else if(state==CSI_MODE){
      if(buff[i]>=0x40&&buff[i]<=0x7E){
        state=GROUND;
        continue;
      }
    }
    else if(state==OSC_MODE){
      if(buff[i]=='\a'){
        state=GROUND;
        continue;
      }
    }
    else if(state==GROUND){
      if(buff[i]==0x08||buff[i]==0x7f)continue;
      if(buff[i]=='\r')continue;
      else if(buff[i]=='\n'|| *w > *cols){
        unsigned int *temp = realloc(*change_line_pos, sizeof(unsigned int) * (*h+1));
        if(temp == NULL){
          printf("change_line_pos realloc error\n");
          exit(EXIT_FAILURE);
        }
        *change_line_pos = temp; // main側の line_down_pos がここで更新される
        // 配列の h 番目に改行位置を記録する
        (*change_line_pos)[*h] = bash_line_total_ciunt;
        (*w)=0;
        (*h)++;
        if(buff[i]=='\n')continue;
      }
      return_buff[buff_counter++]=buff[i];
      bash_line_total_ciunt++;
      (*w)++;
    }
  }
  return_buff[buff_counter]='\0';
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
void draw_cursor(struct cursor *cur){
  static bool lighting=true;
  static double start_time=0;
  double now_time=GetTime();
  if(now_time-start_time>=cur->lighting.speed_ms/1000){
    start_time=now_time;
    lighting=!lighting;
  }
  if(lighting){
    DrawTextEx(
      cur->font,
      cur->shape,
      (Vector2){cur->cur_pos.w,cur->cur_pos.h},
      16, 
      0, 
      WHITE
    );
  }
  else{
    DrawTextEx(
      cur->font,
      " ",
      (Vector2){cur->cur_pos.w,cur->cur_pos.h},
      16, 
      0, 
      WHITE
    );
  }
}
char input_bash(char *n){

}
bool IsAnyKeyReleased(void) {
    for (int i = 32; i <= 126; i++) {
        if (IsKeyReleased(i)) return true;
    }
    if (IsKeyReleased(KEY_ENTER) || IsKeyReleased(KEY_BACKSPACE)) return true;
    return false;
}
