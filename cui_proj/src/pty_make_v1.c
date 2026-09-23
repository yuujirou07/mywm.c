#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <raylib.h>
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
#include "mouse_io.h"
#include "pty_drawing.h"
#include "keybord.h"
#include "error_log_output.h"
#include "pty_make.h"

#define ESC_PAL_MAX 32
#define cur_font_load_max 32
#define EVENT_WAIT_MAX 16
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

	int cell_w;
	int cell_h;

	if(window_init()) {
		error_log_write("window init error");
		return 1;
	}
	cell_w = MeasureText("M", TERMINAL_FONT_SIZE);
	cell_h = TERMINAL_FONT_SIZE;
    EnableEventWaiting();
	screen_pixel = (struct pos){GetScreenWidth(), GetScreenHeight()};
	term_size.w = screen_pixel.w / cell_w;
	term_size.h = screen_pixel.h / cell_h;
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

	if(term_ptr && tcsetattr(slave_fd, TCSANOW, term_ptr)!=0){ // 設定を即時反映
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
	// PTY出力を一度に読むバッファ。
	int term_cell_alloc_size=total*4;
	int result=0;
	int nfds = 0;
	ssize_t buf_size = 0;

	struct cur_mgr *cur_mg = NULL;
	struct term_context ctx = {0};
	struct line_info *lines = NULL;
	struct setting_data setting_data;

	char *read_buf = NULL;

	read_buf      = malloc(term_cell_alloc_size);
	lines         = calloc(term_size.h,sizeof(struct line_info));
	cur_mg        = calloc(1,sizeof(struct cur_mgr));

	// ctx構造体直接初期化
	// ctx(term_context)はターミナルの全状態を保持する中心的な構造体。
	// 画面の各文字セル(term_cell配列)、カーソル位置、エスケープシーケンス
	// パーサの状態(bash_parser_required_memb)などをここに集約する。
	ctx.term_cell            = calloc(term_cell_alloc_size, sizeof(struct term_cell));
	ctx.alt_term_cell        = NULL;
	ctx.cur                  = calloc(1, sizeof(struct cursor));
	ctx.save_cur             = calloc(1, sizeof(struct cursor));
	ctx.term_size            = term_size;
	ctx.palms                = malloc(sizeof(int) * 16);
	ctx.palms_counter        = malloc(sizeof(int));
	ctx.paste_mode           = false;
	ctx.abs_path_name        = NULL;
	ctx.total_cells          = total;
	ctx.insert_mode          = false;
	ctx.lines                = lines;
	ctx.master_fd            = master_fd;
	ctx.bash_pid             = pid_id;
	ctx.kbd_insert_mode      = false;
	ctx.cell_w               = cell_w;
	ctx.cell_h               = cell_h;
	if (!read_buf || !lines || !cur_mg ||
		!ctx.term_cell || !ctx.cur || !ctx.save_cur || !ctx.palms || !ctx.palms_counter) {
		result = 1;
		goto cleanup;
	}
	*ctx.palms_counter = 0;

	// カーソル初期化
	ctx.cur->shape = malloc(2);
	if (!ctx.cur->shape) { result = 1; goto cleanup; }
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
	wd.ctx = &ctx;

	wd.copy_data.copy_cell_counter = 0;
	wd.copy_data.copy_cell_idx_data.start_idx = 0;
	wd.copy_data.copy_cell_idx_data.end_idx = 0;
	wd.copy_data.copy_cell_idx_data.start_idx_block = false;
	wd.copy_data.start_copy = false;
	wd.copy_data.copy_cell = calloc(total,sizeof(struct term_cell *));
	wd.copy_data.copy_cell_orig_bg = calloc(total,sizeof(Color));
	wd.copy_data.copy_cell_orig_fg = calloc(total,sizeof(Color));
	if (!wd.copy_data.copy_cell || !wd.copy_data.copy_cell_orig_bg || !wd.copy_data.copy_cell_orig_fg) {
		result = 1;
		goto cleanup;
	}

	memset(&ctx.fixrd_cur_scr_range,0,sizeof(struct margin));

	result = init_cur_mgr(cur_mg);
	load_settings(&setting_data);


	if (result == 1) {
		error_log_write("can not init cur_mgr code 245");
		return 0;
	}
	load_cur_font(cur_mg);
	cur_font_set(ctx.cur, cur_mg, 1);

	while (!WindowShouldClose() && !wd.should_close) {
		process_keyboard(&wd);
		if (wd.should_close) break;
		process_mouse(&wd);

		nfds = epoll_wait(epoll_fd_list, epoll_list, EVENT_WAIT_MAX, 0);
		for (int i = 0; i < nfds; i++) {
			if (((struct clientinfo *)epoll_list[i].data.ptr)->fd != master_fd)
				continue;
			if (!(epoll_list[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)))
				continue;
			// 大量出力中も入力・描画へ戻る。
			for (int batch = 0; batch < 64; batch++) {
				buf_size = read(master_fd, read_buf, term_cell_alloc_size - 1);
				if (buf_size > 0) {
					bash_str_parse(read_buf, buf_size, &ctx);
				} else if (buf_size == 0 || (buf_size < 0 && errno == EIO)) {
					wd.should_close = true;
					break;
				} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
					break;
				} else if (errno != EINTR) {
					error_log_write("read error");
					result = 1;
					wd.should_close = true;
					break;
				}
			}
		}
		////マウスカーソル点滅再開処理//////
		// 文字を書き込んだ直後はカーソルの点滅を一時停止し、一定時間
		// (cursor_blink_restart_timeout_seconds)経過したら点滅を再開する。
		// これによりタイプ中はカーソルが常に表示され続け、見失いにくくなる。
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
		CUR_RIGTHING_END_POINT:{};



		render_cells(&wd);
	}

cleanup:
	free(ctx.term_cell);
	free(ctx.alt_term_cell);
	free(ctx.lines);
	if (ctx.cur) {
		free(ctx.cur->shape);
		free(ctx.cur);
	}
	free(ctx.save_cur);
	free(ctx.palms);
	free(ctx.palms_counter);
	free(ctx.abs_path_name);
	free(read_buf);
	if (cur_mg) {
		free(cur_mg->cur_font);
		free(cur_mg);
	}
	free(master_fd_ev_poll.data.ptr);
	close(master_fd);
	close(epoll_fd_list);
	kill(pid_id, SIGHUP);
	while (waitpid(pid_id, NULL, 0) < 0 && errno == EINTR) {}
	destroy_data(&wd);
	return result;
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
