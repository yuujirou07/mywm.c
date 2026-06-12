#include <GLFW/glfw3.h>
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
#include <vulkan/vulkan.h>
#include "vulkan_mywrap.h"
#include "vulkan_otf_draw.h"
#include "keybord.h"
#include "error_log_output.h"
#include "pty_make.h"

#define ESC_PAL_MAX 32
#define cur_font_load_max 32
#define EVENT_WAIT_MAX 16
#define DEFAULT_SCREEN_SIZE_W 500
#define DEFAULT_SCREEN_SIZE_H 500
#define DEFAULT_KEY_REPEAT_INTERVAL 0.5
#define DEFAULT_CUR_BLINK_RESTART_TIMEOUT_SEC 0.6



int main(void) {
  int master_fd, slave_fd;
  int total;

  struct pos screen_pixel;
  struct pos term_size;
  struct termios term;
  struct termios *term_ptr = NULL;
  struct winsize ws;
  struct windata wd = {0};

  char slavename[256];

  // [AI生成] フォント読み込みは未実装のため、セルの実寸は暫定値を使う
  int cell_w = 8;
  int cell_h = 16;
  float content_scale_x = 1.0f;
  float content_scale_y = 1.0f;

  // ウィンドウ・Vulkanの初期化（screen_pixelの取得に必要なため、ptyのセットアップより先に行う）
  if(window_init(&wd))
  {
    error_log_write("window init error");
    exit(1);
  }
  glfwSetWindowUserPointer(wd.window,&wd);
  set_kbd_callback(&wd);


  // HIGHDPI環境でぼやけるのを防ぐため、論理サイズではなく実際の物理ピクセルサイズを取得する
  glfwGetFramebufferSize(wd.window, &screen_pixel.w, &screen_pixel.h);
  glfwGetWindowContentScale(wd.window, &content_scale_x, &content_scale_y);

  term_size.w = (int)((float)screen_pixel.w / content_scale_x) / cell_w;
  term_size.h = (int)((float)screen_pixel.h / content_scale_y) / cell_h;
  if (term_size.w < 1) term_size.w = 1;
  if (term_size.h < 1) term_size.h = 1;
  total = term_size.w * term_size.h;

  // 元のターミナルの設定をコピーし、安全なウィンドウサイズを指定する
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
  int term_cell_alloc_size=total*4;
  int result=0;
  int nfds = 0;
  ssize_t buf_size = 0;

  struct cur_mgr *cur_mg = NULL;
  struct term_context ctx;
  struct term_cell *temp_term_cell = NULL;
  struct line_info *lines = NULL;
  struct setting_data setting_data;
  struct pos old_term_cell_size = term_size;

  double last_resize_time = 0;
  char *read_buf = NULL;
  bool dirty = true;
  
  read_buf      = malloc(term_cell_alloc_size);
  temp_term_cell= calloc(term_cell_alloc_size,sizeof(struct term_cell));
  lines         = calloc(term_size.h,sizeof(struct line_info));
  cur_mg        = calloc(1,sizeof(struct cur_mgr));

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
  ctx.window = wd.window;
  ctx.term_cell_alloc_size = &term_cell_alloc_size;
  ctx.kbd_insert_mode = false;
  ctx.cell_w = cell_w;
  ctx.cell_h = cell_h;
  ctx.display_scale = content_scale_x;
  ctx.render_scale = (int)(content_scale_x + 0.5f);
  if (ctx.render_scale < 1) ctx.render_scale = 1;

  // カーソル初期化
  ctx.cur->shape = malloc(2);
  ctx.cur->lighting.blinking = true;
  ctx.cur->lighting.speed_ms = 500;
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

  wd.master_fd = master_fd;
  wd.nfds = &nfds;
  wd.ctx = &ctx;
  wd.kbd_data.clip_bord_chr =NULL;;
  wd.kbd_data.epoll = epoll_list;
  wd.kbd_data.write_buff_overflow = false;
  wd.kbd_data.master_fd_ev_poll = &master_fd_ev_poll;
  wd.kbd_data.epoll_fd_list = &epoll_fd_list;
  wd.kbd_data.cftl_c_sig_counter = 0;



  memset(&ctx.fixrd_cur_scr_range,0,sizeof(struct margin));

  result = init_cur_mgr(cur_mg);
  load_settings(&setting_data);


  if (result == 1) {
    error_log_write("can not init cur_mgr code 245");
    return 0;
  }
  load_cur_font(cur_mg);
  cur_font_set(ctx.cur, cur_mg, 1);

  // OTFフォントから全ASCII印刷可能文字のグリフをキャッシュする
  {
    struct pos font_size = {cell_w, cell_h};
    if (load_otf_glyphs("/home/yuujirou07/myfont.otf", font_size,
                        wd.glyphs, &wd.font_ascender) != 0) {
      error_log_write("フォントグリフの読み込みに失敗しました");
    }
  }

  int old_width = 0;
  int old_height = 0;

  glfwGetFramebufferSize(wd.window, &old_width, &old_height);  

  while (!glfwWindowShouldClose(wd.window))
  {
    glfwPollEvents();

    nfds = epoll_wait(epoll_fd_list,epoll_list,EVENT_WAIT_MAX, 1);

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
          buf_size = read(master_fd, read_buf, term_cell_alloc_size - 1);
          if (buf_size > 0)
          {
            bash_str_parse(read_buf, buf_size, &ctx);
            dirty = true;
          }
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

    ////マウスカーソル点滅再開処理//////
    if( ctx.cur->now_writing == true){
      if(ctx.cur->writing_st_time <= 0)
        ctx.cur->writing_st_time = glfwGetTime();
 
      ctx.cur->writing_end_time = glfwGetTime();
      
      if(ctx.cur->writing_end_time - ctx.cur->writing_st_time < setting_data.cursor_blink_restart_timeout_seconds)
        goto CUR_RIGTHING_END_POINT;


      ctx.cur->now_writing = false;
      ctx.cur->writing_st_time = 0;
      ctx.cur->writing_end_time = 0;
    }
    //マウスカーソル分岐抜け
    CUR_RIGTHING_END_POINT:
    

    int current_width;
    int current_height;
    glfwGetFramebufferSize(wd.window, &current_width, &current_height);

    if (current_width != old_width || current_height != old_height || wd.font_size_changed) {
      last_resize_time = glfwGetTime();
      old_width = current_width;
      old_height = current_height;
      wd.font_size_changed = false;
    }

    //リサイズ処理（デバウンス: 0.1秒間リサイズが止まってから実行）
    if(last_resize_time > 0 && glfwGetTime() - last_resize_time > 0.1){
      old_term_cell_size = term_size;

      // スワップチェーンを先に再作成してrenderExtentを確定させる
      // (Waylandではスワップチェーン再作成後にフレームバッファサイズが確定する)
      recreate_swapchain(&wd);

      // display_scale / render_scale を更新（別モニター対応）
      float xscale = 1.0f;
      glfwGetWindowContentScale(wd.window, &xscale, NULL);
      ctx.display_scale = xscale;
      ctx.render_scale = (int)(xscale + 0.5f);
      if (ctx.render_scale < 1) ctx.render_scale = 1;

      // 確定したrenderExtentからterm_sizeを計算
      screen_pixel.w = (int)wd.renderExtent.width;
      screen_pixel.h = (int)wd.renderExtent.height;
      term_size.w = screen_pixel.w / ctx.cell_w;
      term_size.h = screen_pixel.h / ctx.cell_h;
      if (term_size.w < 1) term_size.w = 1;
      if (term_size.h < 1) term_size.h = 1;

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

      dirty = true;
      last_resize_time = 0;
    }

    
    if(dirty){
      // 前のフレームが完全に終わるのをCPU側で待つ
        // 第2引数の TRUE は「フェンスがシグナル状態になるまで待つ」という意味
        // 最後の引数はタイムアウト時間（UINT64_MAX = 無限に待つ）
        vkWaitForFences(wd.device, 1, &wd.inFlightFence, VK_TRUE, UINT64_MAX);
        // 次のフレームのために、フェンスを非シグナル状態（未完了）にリセットしておく
        vkResetFences(wd.device, 1, &wd.inFlightFence);

        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(wd.device, wd.swapchain, UINT64_MAX,
            wd.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain(&wd);
            dirty = true;
            goto FRAME_END;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "vkAcquireNextImageKHR に失敗しました: %d\n", acquireResult);
            break;
        }

        VkCommandBuffer commandBuffer = wd.commandBuffers[imageIndex];

        // CPUでterm_cellをBGRAピクセルとしてステージングバッファに描画
        render_cells_to_buffer(&wd);

        // コマンドバッファの録音開始
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo = {0};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        //UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier toTransferDst = {0};
        toTransferDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransferDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.image               = wd.swapchainImages[imageIndex];
        toTransferDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransferDst.subresourceRange.levelCount = 1;
        toTransferDst.subresourceRange.layerCount = 1;
        toTransferDst.srcAccessMask       = 0;
        toTransferDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &toTransferDst);

        // ステージングバッファ → スワップチェーン画像へコピー
        VkBufferImageCopy region = {0};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width           = wd.chosenExtent.width;
        region.imageExtent.height          = wd.chosenExtent.height;
        region.imageExtent.depth           = 1;
        vkCmdCopyBufferToImage(commandBuffer, wd.stagingBuffer,
            wd.swapchainImages[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // バリア②: TRANSFER_DST_OPTIMAL → PRESENT_SRC_KHR
        VkImageMemoryBarrier toPresent = {0};
        toPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image               = wd.swapchainImages[imageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.layerCount = 1;
        toPresent.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask       = 0;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, NULL, 0, NULL, 1, &toPresent);

        // 録音終了
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            fprintf(stderr, "コマンドバッファの録音に失敗しました。\n");
            break;
        }

        VkSubmitInfo submitInfo = {0};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {wd.imageAvailableSemaphore};
        // 転送ステージでセマフォを待つ
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        // 送信するコマンドバッファを指定
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // 処理がすべて終わったらシグナル状態にするセマフォ
        VkSemaphore signalSemaphores[] = {wd.renderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        // 第3引数に inFlightFence を渡すことで、GPUの全処理が終わった瞬間にフェンスが自動でシグナル状態になります
        if (vkQueueSubmit(wd.graphicsQueue, 1, &submitInfo, wd.inFlightFence) != VK_SUCCESS) {
            fprintf(stderr, "コマンドバッファの送信に失敗しました。\n");
            break;
        }

        //描き終わったキャンバスを OS（Wayland）に提出（Present）して画面に映す
        VkPresentInfoKHR presentInfo = {0};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        // 提出する前に、GPUの描画が完全に終わる（renderFinishedSemaphoreがシグナルされる）のを待つ
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {wd.swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        // 画面への提示を実行
        vkQueuePresentKHR(wd.graphicsQueue, &presentInfo);
        dirty = false;
    }
    FRAME_END:;
  }



  free_otf_glyphs(wd.glyphs);

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
  glfwDestroyWindow(wd.window);
  glfwTerminate();
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
          glfwSetWindowTitle(ctx->window, new_win_title);
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
      glfwSetClipboardString(ctx->window, decode_result);
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
    ctx->term_cell[idx+i].fg_color = ctx->bash_parser_required_memb.now_fg_color;
    ctx->term_cell[idx+i].bg_color = ctx->bash_parser_required_memb.now_bg_color;
    ctx->term_cell[idx+i].is_real_chr = false;
  }
}
void window_resized_update_memb(GLFWwindow *window, struct pos *screen_pixel,struct pos *term_size,struct term_context *ctx){
  glfwGetFramebufferSize(window, &screen_pixel->w, &screen_pixel->h);

  // [改善] 別解像度モニタへ移動した場合に備え、拡大率を取り直す
  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale(window, &xscale, &yscale);
  ctx->display_scale = xscale;

  int rs = (int)(xscale + 0.5f);
  if (rs < 1) rs = 1;
  ctx->render_scale = rs;

  int virtual_w = screen_pixel->w / ctx->display_scale;
  int virtual_h = screen_pixel->h / ctx->display_scale;

  // [改善] 1セルの実寸 cell*render_scale で割って桁数・行数を求める
  term_size->w = virtual_w / (ctx->cell_w * ctx->render_scale);
  term_size->h = virtual_h / (ctx->cell_h * ctx->render_scale);
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

void cur_allow_write(enum cur_allow_mode mode, int master_fd, int key_code) {
  const char *seq = NULL;
  if (mode == AP_MODE) {
    switch (key_code) {
      case GLFW_KEY_UP:    seq = "\x1bOA"; break;
      case GLFW_KEY_DOWN:  seq = "\x1bOB"; break;
      case GLFW_KEY_RIGHT: seq = "\x1bOC"; break;
      case GLFW_KEY_LEFT:  seq = "\x1bOD"; break;
      default: return;
    }
  } else {
    switch (key_code) {
      case GLFW_KEY_UP:    seq = "\x1b[A"; break;
      case GLFW_KEY_DOWN:  seq = "\x1b[B"; break;
      case GLFW_KEY_RIGHT: seq = "\x1b[C"; break;
      case GLFW_KEY_LEFT:  seq = "\x1b[D"; break;
      default: return;
    }
  }
  write(master_fd, seq, strlen(seq));
}