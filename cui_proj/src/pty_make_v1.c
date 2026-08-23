#include <GLFW/glfw3.h>
#include <fcntl.h>
#include <limits.h>
#include <ncurses.h>
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
#include "mouse_io.h"
#include "vulkan_mywrap.h"
#include "vulkan_otf_draw.h"
#include "keybord.h"
#include "error_log_output.h"
#include "pty_make.h"
#include "codepoint_comb.h"

#define ESC_PAL_MAX 32
#define cur_font_load_max 32
#define EVENT_WAIT_MAX 16
#define DEFAULT_SCREEN_SIZE_W 500
#define DEFAULT_SCREEN_SIZE_H 500
#define DEFAULT_KEY_REPEAT_INTERVAL 0.5
#define DEFAULT_CUR_BLINK_RESTART_TIMEOUT_SEC 0.6



// =====================================================================
// main(): プログラム全体の流れ
//   1. GLFW + Vulkan でウィンドウを初期化し、フレームバッファの物理ピクセル
//      サイズと文字セルのサイズ(cell_w/cell_h)からターミナルの行数・列数
//      (term_size)を計算する
//   2. openpty() で疑似端末(pty)を確保し、fork() した子プロセスの
//      標準入出力をpty(スレーブ側)に繋ぎ替えて bash -i を起動する
//   3. 親プロセスはpty(マスター側)の読み込みを epoll + ノンブロッキング
//      I/O で監視し、GLFWのイベントループ内でbashからの出力を
//      bash_str_parse() に渡してターミナル画面の状態(term_cell配列、
//      カーソル位置など)を更新する
//   4. ウィンドウサイズの変化を検出したら(0.1秒のデバウンス後)、
//      スワップチェーンとterm_sizeを再計算し、必要ならバッファを
//      再確保してreflow_terminal_text()で表示内容を組み直す
//   5. 画面内容が更新された(dirty)場合のみ、term_cellの内容をCPU側で
//      ピクセルに変換してステージングバッファへ書き込み、Vulkanで
//      スワップチェーン画像へコピー・提示(present)する
// =====================================================================
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
	glfwSetWindowSizeCallback(wd.window, window_size_callback);

	init_mouse(&wd);


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
	// master_fd(bashからの出力)が読み込み可能になったことを検知するための設定。
	// master_fdは上でO_NONBLOCKにしているため、epoll_waitで読み込み可能と分かった
	// 場合のみreadを行うことで、メインループをブロックさせずに済む。
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
	// term_cell_alloc_size: term_cell/alt_term_cell/read_bufなどの確保サイズ(セル数)。
	// 最初から total の4倍を確保しておき、リサイズ時に必要ならさらに2倍ずつ拡張する。
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
	// ctx(term_context)はターミナルの全状態を保持する中心的な構造体。
	// 画面の各文字セル(term_cell配列)、カーソル位置、エスケープシーケンス
	// パーサの状態(bash_parser_required_memb)などをここに集約する。
	ctx.term_cell            = calloc(term_cell_alloc_size, sizeof(struct term_cell));
	ctx.alt_term_cell        = NULL;
	ctx.cur                  = malloc(sizeof(struct cursor));
	ctx.save_cur             = malloc(sizeof(struct cursor));
	ctx.term_size            = term_size;
	ctx.palms                = malloc(sizeof(int) * 16);
	ctx.palms_counter        = malloc(sizeof(int));
	*ctx.palms_counter       = 0;
	ctx.paste_mode           = false;
	ctx.abs_path_name        = NULL;
	ctx.total_cells          = total;
	ctx.insert_mode          = false;
	ctx.lines                = lines;
	ctx.master_fd            = master_fd;
	ctx.window               = wd.window;
	ctx.term_cell_alloc_size = &term_cell_alloc_size;
	ctx.kbd_insert_mode      = false;
	ctx.cell_w               = cell_w;
	ctx.cell_h               = cell_h;
	ctx.display_scale        = content_scale_x;
	ctx.render_scale         = (int)(content_scale_x + 0.5f);
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
	// state=GROUND: 通常モード（受信した文字をそのまま画面に描画する）
	// mode=IDK: エスケープシーケンスの種類が未確定の状態
	// この2つの値の組み合わせがbash_str_parse()の状態機械の初期状態になる
	ctx.bash_parser_required_memb.state = GROUND;
	ctx.bash_parser_required_memb.mode = IDK;
	ctx.bash_parser_required_memb.osc_pal_chr_counter = 0;
	ctx.bash_parser_required_memb.val = 0;
	ctx.bash_parser_required_memb.is_private = false;
	ctx.bash_parser_required_memb.has_val = 0;
	ctx.use_g_charset = false;
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
	wd.mouce_data.mouce_button_left_down = false;
	wd.dirty = &dirty;

	wd.copy_data.copy_cell_counter = 0;
	wd.copy_data.copy_cell_idx_data.start_idx = 0;
	wd.copy_data.copy_cell_idx_data.end_idx = 0;
	wd.copy_data.copy_cell_idx_data.start_idx_block = false;
	wd.copy_data.start_copy = false;
	wd.copy_data.copy_cell = calloc(total,sizeof(struct term_cell *));
	wd.copy_data.copy_cell_orig_bg = calloc(total,sizeof(Color));
	wd.copy_data.copy_cell_orig_fg = calloc(total,sizeof(Color));

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

	// glfwGetWindowMonitor()はフルスクリーン時しかモニタを返さず、ウィンドウモードでは
	// 必ずNULLになるためプライマリモニタから取得する。
	// リフレッシュレートが取れない環境(Wayland等でrefreshRate=0)でも起動は続行し、
	// epoll_waitの待ち時間は従来値の4msにフォールバックする。
	GLFWmonitor *monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode *mode = (monitor != NULL) ? glfwGetVideoMode(monitor) : NULL;
	int wait_time_ms = 4;
	if (mode != NULL && mode->refreshRate > 0) {
		wait_time_ms = 1000 / mode->refreshRate;
		if (wait_time_ms < 1) wait_time_ms = 1;
	} else {
		error_log_write("リフレッシュレートを取得できないため待ち時間を4msに設定しました");
	}
	// ===== メインループ =====
	// 1フレームごとに「入力イベント処理」→「PTY出力の読み取り・パース」→
	// 「カーソル点滅/リサイズ処理」→「必要なら再描画」を行う。
	while (!glfwWindowShouldClose(wd.window)){
		glfwWaitEventsTimeout(wait_time_ms / 1000.0);

		// master_fd(bashの出力)が読めるかどうかを最大1msだけ待って確認する
		nfds = epoll_wait(epoll_fd_list,epoll_list,EVENT_WAIT_MAX,wait_time_ms);

		while(nfds>0){
			for(int i=0;i<nfds;i++){
				//もしfdがmaster_fdだったら
				if(((struct clientinfo *)epoll_list[i].data.ptr)->fd!=master_fd)
					continue;
				if((epoll_list[i].events & EPOLLIN)==false)
					break;

				// 読めるデータがなくなる(EAGAIN)まで読み続け、その都度パースする
				while (1){
					buf_size = read(master_fd, read_buf, term_cell_alloc_size - 1);
					if (buf_size > 0){
						// bashからの出力(プレーンテキスト+ANSIエスケープシーケンス)を
						// 解析し、term_cell配列(画面の文字セル)とカーソル状態を更新する
						bash_str_parse(read_buf, buf_size, &ctx);
						dirty = true;
					}
					else if(buf_size==0)break;
					else if (buf_size == -1){
						// -1 の場合は errno を確認する
						if (errno == EAGAIN || errno == EWOULDBLOCK){
							// 受信バッファが空になったので、正常に読み取りループを抜ける
							break;
						}
						else{
							// それ以外の本当のエラー
							error_log_write("read error");
							return 1;
						}
					}
				}
			}
			// 直後に追加された出力があれば同じフレームで処理する
			nfds = epoll_wait(epoll_fd_list,epoll_list,EVENT_WAIT_MAX,wait_time_ms);
		}

		////マウスカーソル点滅再開処理//////
		// 文字を書き込んだ直後はカーソルの点滅を一時停止し、一定時間
		// (cursor_blink_restart_timeout_seconds)経過したら点滅を再開する。
		// これによりタイプ中はカーソルが常に表示され続け、見失いにくくなる。
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
		CUR_RIGTHING_END_POINT:{};



		// リサイズ検知はwindow_size_callback()からのイベント通知(resize_event_pending)に
		// 一本化し、毎フレームのglfwGetFramebufferSize()による問い合わせ(ポーリング)は行わない。
		if (wd.resize_event_pending || wd.font_size_changed) {
			wd.resize_event_pending = false;
			last_resize_time = glfwGetTime();
			wd.font_size_changed = false;

			int current_width, current_height;
			glfwGetFramebufferSize(wd.window, &current_width, &current_height);

			// ドラッグ中も滑らかに追従させるため、軽い処理だけ毎フレーム行う:
			// スワップチェーンを即再作成してrenderExtentを新サイズへ合わせ、再描画フラグを立てる。
			// 重いterm_size再計算/reflow/reallocは下のデバウンス処理に残し、ドラッグ確定時に一度だけ実行する。
			// (最小化等でサイズが0の間は再作成しない)
			if (current_width > 0 && current_height > 0) {
				recreate_swapchain(&wd);
				dirty = true;
			}
		}

		//リサイズ処理（デバウンス: 0.1秒間リサイズが止まってから実行）
		// ウィンドウサイズ変更中は何度もイベントが発生するため、最後の変更から
		// 0.1秒操作が無いことを確認してから一度だけ実際のリサイズ処理を行う。
		// 処理内容: ① スワップチェーン再作成 → ② 新しいterm_size計算 →
		// ③ pty(TIOCSWINSZ)とbashプロセス(SIGWINCH)へサイズ変更を通知 →
		// ④ 必要ならセルバッファを拡張 → ⑤ reflow_terminal_textで表示内容を再配置


		if(last_resize_time > 0 && glfwGetTime() - last_resize_time > 0.1){
			old_term_cell_size = term_size;

			// スワップチェーンは上のサイズ変更検知時に毎フレーム即再作成済みのため、
			// ここではrenderExtentが既に確定している。再作成は行わない。

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

			// term_cellの再確保でポインタが移動する/セル数が変わるため、
			// マウス選択状態とそれに対応するバッファも作り直す
			wd.copy_data.copy_cell_counter = 0;
			wd.copy_data.copy_cell = realloc(wd.copy_data.copy_cell, sizeof(struct term_cell *) * total);
			wd.copy_data.copy_cell_orig_bg = realloc(wd.copy_data.copy_cell_orig_bg, sizeof(Color) * total);
			wd.copy_data.copy_cell_orig_fg = realloc(wd.copy_data.copy_cell_orig_fg, sizeof(Color) * total);
			memset(wd.copy_data.copy_cell, 0, sizeof(struct term_cell *) * total);
			wd.copy_data.copy_cell_idx_data.start_idx_block = false;

			ws.ws_col = term_size.w;
			ws.ws_row = term_size.h;
			ws.ws_xpixel = (unsigned short)screen_pixel.w;
			ws.ws_ypixel = (unsigned short)screen_pixel.h;

			ioctl(master_fd, TIOCSWINSZ, &ws);
			kill(pid_id, SIGWINCH);

			// セル数が現在の確保サイズを超えた場合、必要なサイズになるまで2倍ずつ
			// 拡張し、term_cell/alt_term_cell/temp_term_cell/read_bufを再確保する
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

			// 古いterm_size(old_term_cell_size)の内容を新しいterm_sizeに合わせて
			// 詰め直す（行の折り返し位置を再計算しつつ文字を移し替える）
			reflow_terminal_text(&ctx, old_term_cell_size, &temp_term_cell, term_cell_alloc_size);

			struct line_info *new_lines = calloc(term_size.h, sizeof(struct line_info));
			if (new_lines != NULL) {
				free(ctx.lines);
				ctx.lines = new_lines;
			}

			dirty = true;
			last_resize_time = 0;
		}


		// dirty(画面内容が更新された)時だけ描画する。Vulkanでは
		// term_cell配列の内容をCPU側でピクセル(BGRA)に変換してステージング
		// バッファへ書き込み、それをスワップチェーン画像にコピーして提示する。
		if(dirty){
			// recreate_swapchain()がステージングバッファの再確保に失敗していると
			// stagingMappedがNULLのままになり、commandBuffersも解放済みで無効。
			// 次のリサイズで再作成が成功するまで、このフレームの描画は諦めて待つ。
			if (wd.stagingMapped == NULL) {
				goto FRAME_END;
			}

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
				VkResult presentResult = vkQueuePresentKHR(wd.graphicsQueue, &presentInfo);

				if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
						// サーフェスとスワップチェーンのサイズが食い違っている(リサイズ中など)。
						// 再作成して次フレームで描き直す。
						recreate_swapchain(&wd);
						dirty = true;
				} else {
						dirty = false;
				}
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
	destroy_data(&wd);
	glfwTerminate();
	close(epoll_fd_list);
}

// scroll_region_up(): スクロール領域(DECSTBMで設定、未設定なら画面全体)を
// 1行上にスクロールする。先頭行が画面外に消え、最終行に空行が追加される。
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

// scroll_region_down(): スクロール領域(DECSTBMで設定、未設定なら画面全体)を
// 1行下にスクロールする。最終行が画面外に消え、先頭行に空行が追加される。
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

// mymemcpy(): [start, end) の範囲をヒープ上に新しい文字列としてコピーする。
// mode==line_down の場合は末尾に'\n'を追加してから'\0'を付ける
// （現在この関数を呼び出している箇所は無い、未使用のユーティリティ）。
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

// load_cur_font(): "cur_font.txt" からカーソルの見た目として使う文字を
// 1文字ずつ読み込み、cur_mgr->cur_font[] に格納する(最大cur_font_load_max個)。
// ファイルが無ければcur_set_default()でデフォルト文字('|'と'/')を設定する。
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

// init_cur_mgr(): cur_mgr->cur_font配列(カーソル候補文字を格納する)を確保する
int init_cur_mgr(struct cur_mgr *cur_mgr){
	cur_mgr->cur_font = malloc(sizeof(char) * cur_font_load_max);
	if (cur_mgr->cur_font == NULL) {
		printf("cur_mgr malloc error");
		exit(1);
	}
	return 0;
}

// cur_mgr_free(): カーソル管理構造体を解放する。
void cur_mgr_free(struct cur_mgr *cur_mgr){
	free(cur_mgr);
}

// cur_set_default(): cur_font.txtが無い場合のデフォルトのカーソル候補文字
// ('|'と'/')を設定する
void cur_set_default(struct cur_mgr *cur_mgr){
	cur_mgr->load_cur_font_n = 2;
	cur_mgr->cur_font[0] = '|';
	cur_mgr->cur_font[1] = '/';
}

// cur_font_set(): cur_mgr->cur_font[n-1]の文字をカーソルの形状(cur->shape)
// として設定する(1始まりのインデックス)
void cur_font_set(struct cursor *cur, struct cur_mgr *cur_mgr, int n){
	if (n > cur_mgr->load_cur_font_n) {
		printf("your chose cur font nonber is big then cur_font_load_max");
		exit(1);
	}
	cur->shape[0] = cur_mgr->cur_font[n - 1];
	cur->shape[1] = '\0';
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
// erase_chr(): ECH (Erase Character) - カーソル位置からn文字を
// 「現在の前景色/背景色の空白」で上書きする(文字の削除/シフトは行わない)
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
// window_resized_update_memb(): ウィンドウのフレームバッファサイズと
// コンテンツスケールを取得し直し、それに基づいてterm_size(行数・列数)を
// 再計算する。main()内のリサイズ処理は同様の計算を直接行っているため、
// 現在この関数を呼び出している箇所は無い(未使用のユーティリティ)。
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

// unicode_utf8_encoder(): UnicodeコードポイントをUTF-8バイト列へ変換する。
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

// reflow_terminal_text(): ウィンドウサイズ変更時に呼ばれる。
// old_term_size(変更前の行数・列数)で格納されていたterm_cellの内容を、
// ctx->term_size(変更後の行数・列数)に合わせて詰め直す(リフロー)。
// 各論理行(is_wrappedで繋がった行の集まり)の末尾の空白を除いた実文字だけを
// 取り出し、新しい列数で改行しながらtemp(一時バッファ)に詰めていく。
// 最後にctx->term_cellとtempを入れ替える(ポインタswap)ことで、
// 呼び出し元が持つtemp_term_cellは「次にリフローで使う旧バッファ」になる。
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


// load_settings(): "pty_make_settings.json" から設定を読み込む。
// ファイルが開けない場合はエラーログを出してset_default_settings()で
// デフォルト値を設定する。
// 注意: ファイルが開けた場合でもJSONの解析自体は行われておらず、
// settings_fileもfcloseされていない(未実装/TODOの状態)。
void load_settings(struct setting_data *data){

	FILE *settings_file = fopen("pty_make_settings.json","r");

	//開けなかったらデフォルト設定
	if(settings_file == NULL){
		error_log_write("can not open settings.json");
		set_default_settings(data);
	}
}

// set_default_settings(): キーリピート間隔・カーソル点滅再開までの
// タイムアウトをデフォルト値に設定する
void set_default_settings(struct setting_data *data){
	data->key_repeat_interval = DEFAULT_KEY_REPEAT_INTERVAL;
	data->cursor_blink_restart_timeout_seconds = DEFAULT_CUR_BLINK_RESTART_TIMEOUT_SEC;
}

// allocate_cell(): term_cell配列をsize個分にrealloc()するだけのラッパー
// （現在この関数を呼び出している箇所は無い、未使用のユーティリティ）
struct term_cell *allocate_cell(struct term_cell* term_cell,int size)
{
	struct term_cell * temp = realloc(term_cell,sizeof(struct term_cell)* size);
	return temp;
}

// cur_allow_write(): キーボードの矢印キー入力をbash側へ送るエスケープ
// シーケンスに変換する。カーソルがAP_MODE(アプリケーションキーパッド
// モード、CSI ?1h で有効化)の場合は"ESC O 方向"、NORMAL_MODEの場合は
// "ESC [ 方向"を送信する(vim等のアプリで矢印キーが正しく動くようにするため)。
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



// window_size_callback(): GLFWがウィンドウサイズ変更を検知した時に呼ばれる。
// メインループ側は毎フレームglfwGetFramebufferSize()を問い合わせる代わりに
// wd->resize_event_pendingを見るだけで済むようにする(ポーリング→イベント駆動)。
// 実際のピクセルサイズ(HiDPI考慮)はイベント発生時にメインループ側で
// glfwGetFramebufferSize()を使って取得するため、ここではwidth/heightは使わない。
void window_size_callback(GLFWwindow* window, int width, int height){
	(void)width;
	(void)height;
	struct windata *wd = (struct windata *)glfwGetWindowUserPointer(window);
	if (wd == NULL) return;

	wd->resize_event_pending = true;
	wd->resize_event_time = glfwGetTime();
}


