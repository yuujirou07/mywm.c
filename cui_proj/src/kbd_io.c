#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include<stddef.h>
#include<string.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include"vulkan_mywrap.h"
#include"keybord.h"
#include"error_log_output.h"
#include"pty_make.h"
#define my_container_of(ptr, type, member) \
    ((type *)( (char *)(ptr) - offsetof(type, member) ))

    
// key_callback(): GLFWのキーイベントをpty側へ送る制御文字/エスケープシーケンスへ変換する。
// Ctrl+Vの貼り付け、Ctrl+E終了、矢印やファンクションキーもここで処理する。
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{   
    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);
  
    if(key == GLFW_KEY_E && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
    {
        destroy_data(wd);
    }
    else if(key == GLFW_KEY_V && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
    {
        //前フレームで write() が EAGAIN を返した（カーネルの送信バッファが満杯）場合、
      // epoll を EPOLLOUT に切り替えて「空きができたら通知」を待っている。
      // 今フレームの epoll_wait で EPOLLOUT イベントが来た = 書き込み再開可能。
      if(wd->kbd_data.write_buff_overflow==true && *wd->nfds>0)
      {
        for(int i=0;i<*wd->nfds;i++)
        {
          //もしepoll_list[i]番目のfdがmaster_fdだったら
          if(((struct clientinfo*)wd->kbd_data.epoll[i].data.ptr)->fd!=wd->master_fd)
            continue;
          //もし書き込み可能かwriteの書き込みバッファが溢れていなかったら
          if(wd->kbd_data.epoll[i].events & EPOLLOUT)
          {
            if(wd->kbd_data.clip_bord_chr == NULL && (wd->kbd_data.clip_bord_chr = glfwGetClipboardString(wd->window)) == NULL)
            {
              break;
            }
          }
        }
      }
      else if(wd->kbd_data.write_buff_overflow==true)
      {
        if(wd->kbd_data.clip_bord_chr == NULL && (wd->kbd_data.clip_bord_chr = glfwGetClipboardString(wd->window)) == NULL)
        {
          return;
        }
      }
      else 
      {
        wd->kbd_data.clip_bord_chr = glfwGetClipboardString(wd->window);
      }
      
      if(wd->kbd_data.clip_bord_chr!=NULL)
      {
        size_t len = strlen(wd->kbd_data.clip_bord_chr);   
        if(len> 0) 
        {
          char temp_clip_bord_chr[len+1];
          int temp_clip_bord_chr_counter=0;
          for (size_t i = 0; i < len; i++) 
          {
            char c = (char)wd->kbd_data.clip_bord_chr[i];
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t')
            {
              temp_clip_bord_chr[temp_clip_bord_chr_counter++]=c;
            }
          }
          temp_clip_bord_chr[temp_clip_bord_chr_counter]='\0';
          //何バイト読み込んだか
          ssize_t now_fd_input_size = write(wd->master_fd,temp_clip_bord_chr,strlen(temp_clip_bord_chr));
          //次のループで再度書き込む位置を保存しておきたいのでclipbord_chr変数から書き込んだバイト数のポインタを加算する
          if(now_fd_input_size>0)
          {
            wd->kbd_data.clip_bord_chr+=now_fd_input_size;
            //もし送られたバイト数がクリップボードbuffのデータより大きかったら初期化する
            if(len<=now_fd_input_size)
            {
              wd->kbd_data.clip_bord_chr=NULL;
            }
          }
          else if(now_fd_input_size==0)
          {
            wd->kbd_data.clip_bord_chr=NULL;
            wd->kbd_data.write_buff_overflow=false;
            wd->kbd_data.master_fd_ev_poll->events=EPOLLIN;
            if(epoll_ctl(*wd->kbd_data.epoll_fd_list,EPOLL_CTL_MOD ,wd->master_fd,wd->kbd_data.master_fd_ev_poll)!=0)
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

            wd->kbd_data.master_fd_ev_poll->events=EPOLLOUT;
            wd->kbd_data.write_buff_overflow=true;

            if(epoll_ctl(*wd->kbd_data.epoll_fd_list,EPOLL_CTL_MOD ,wd->master_fd,wd->kbd_data.master_fd_ev_poll)!=0)
            {
              char err_buff[128];
              snprintf(err_buff,128,"epoll_ctl func error errno = %d",errno);
              error_log_write(err_buff);
            }
            return;
          }
          else
          {//エラーの場合
            error_log_write("write error");
            return ;
          }
        }
      }
    }
    //文字サイズ変更（Ctrl + +/-）
    else if((key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
    {
        change_font_size(wd, FONT_CELL_H_STEP);
    }
    else if((key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
    {
        change_font_size(wd, -FONT_CELL_H_STEP);
    }
    else if(action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        char ctrl_key = (char)(key - GLFW_KEY_A + 1);
        write(wd->master_fd, &ctrl_key, 1);
        wd->ctx->cur->now_writing = true;
    }
    else if(key == GLFW_KEY_ENTER && action == GLFW_PRESS)
    {
        char enter_key = 13;
        write(wd->master_fd, &enter_key, 1);
    }
    else if(key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        char c = 0x7f;
        write(wd->master_fd, &c, 1);
        wd->ctx->cur->now_writing = true;
    }
    else if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        write(wd->master_fd, "\x1b", 1);
    }
    //fnキー入力処理
    else if(action == GLFW_PRESS && key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
    {
        switch(key)
        {
            case GLFW_KEY_F1:  write(wd->master_fd, "\x1bOP",   3); break;
            case GLFW_KEY_F2:  write(wd->master_fd, "\x1bOQ",   3); break;
            case GLFW_KEY_F3:  write(wd->master_fd, "\x1bOR",   3); break;
            case GLFW_KEY_F4:  write(wd->master_fd, "\x1bOS",   3); break;
            case GLFW_KEY_F5:  write(wd->master_fd, "\x1b[15~", 5); break;
            case GLFW_KEY_F6:  write(wd->master_fd, "\x1b[17~", 5); break;
            case GLFW_KEY_F7:  write(wd->master_fd, "\x1b[18~", 5); break;
            case GLFW_KEY_F8:  write(wd->master_fd, "\x1b[19~", 5); break;
            case GLFW_KEY_F9:  write(wd->master_fd, "\x1b[20~", 5); break;
            case GLFW_KEY_F10: write(wd->master_fd, "\x1b[21~", 5); break;
            case GLFW_KEY_F11: write(wd->master_fd, "\x1b[23~", 5); break;
            case GLFW_KEY_F12: write(wd->master_fd, "\x1b[24~", 5); break;
        }
    }
    //ナビゲーションキー入力処理
    else if(key == GLFW_KEY_HOME      && action == GLFW_PRESS) write(wd->master_fd, "\x1b[1~", 4);
    else if(key == GLFW_KEY_INSERT    && action == GLFW_PRESS) write(wd->master_fd, "\x1b[2~", 4);
    else if(key == GLFW_KEY_DELETE    && action == GLFW_PRESS) write(wd->master_fd, "\x1b[3~", 4);
    else if(key == GLFW_KEY_END       && action == GLFW_PRESS) write(wd->master_fd, "\x1b[4~", 4);
    else if(key == GLFW_KEY_PAGE_UP   && action == GLFW_PRESS) write(wd->master_fd, "\x1b[5~", 4);
    else if(key == GLFW_KEY_PAGE_DOWN && action == GLFW_PRESS) write(wd->master_fd, "\x1b[6~", 4);
    else if(key == GLFW_KEY_TAB && action == GLFW_PRESS)
    {
        write(wd->master_fd, "\t", 1);
    }
    else if(key == GLFW_KEY_RIGHT && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        //右のセルが空白ならカーソルをブロックする
        if(wd->ctx->cur->cur_pos.w < wd->ctx->term_size.w)
        {
            if(wd->ctx->cur->cur_pos.w < wd->ctx->temp_cur_pos.w + 1 ||
               wd->ctx->term_cell[wd->ctx->cur->cur_pos.h * wd->ctx->term_size.w + wd->ctx->cur->cur_pos.w + 1].character != ' ')
            {
                cur_allow_write(wd->ctx->cur->allow_mode, wd->master_fd, key);
                wd->ctx->cur->now_writing = true;
            }
        }
    }
    else if((key == GLFW_KEY_LEFT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        cur_allow_write(wd->ctx->cur->allow_mode, wd->master_fd, key);
        wd->ctx->cur->now_writing = true;
    }
    else{
        if(wd->kbd_data.cftl_c_sig_counter == 1 && key == GLFW_KEY_LEFT_CONTROL){
            wd->kbd_data.cftl_c_sig_counter = 0;
            return ;
        }
        if(wd->kbd_data.cftl_c_sig_counter > 1 && key == GLFW_KEY_C){
            wd->kbd_data.cftl_c_sig_counter = 0;
        }

        if(wd->kbd_data.cftl_c_sig_counter == 0 && key == GLFW_KEY_LEFT_CONTROL)
            (wd->kbd_data.cftl_c_sig_counter)++;

        else if(wd->kbd_data.cftl_c_sig_counter == 1 && key == GLFW_KEY_LEFT_CONTROL)
        {
            write(wd->master_fd,"\x03",1);
            (wd->kbd_data.cftl_c_sig_counter) ++;
            return ;
        }
        return;
    }
}

// character_callback(): 通常の文字入力をUTF-8へ変換してptyのmaster_fdへ書き込む。
void character_callback(GLFWwindow* window, unsigned int codepoint)
{
    struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);

    ////英数字入力処理////////////
    if(codepoint < 32 || codepoint > 127)
        return;

    char utf8[4] = {0};
    int len = 0;
    unicode_utf8_encoder(utf8, codepoint, &len);

    wd->ctx->cur->now_writing = true;
    write(wd->master_fd, utf8, len);
}
