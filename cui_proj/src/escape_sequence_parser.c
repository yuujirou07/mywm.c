#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "codepoint_comb.h"
#include "pty_make.h"
//文字セットを保存している関数ポインタを返すので書き換えることができる
static enum chr_set *chr_data(bool num);

// xterm_256color(): SGR 38;5 / 48;5 で使う0〜255のxterm色番号をColorへ変換する。
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

// esc_single_dispatch(): "ESC <1文字>" 形式の、CSIでもOSCでもない
// 単純なエスケープシーケンスを処理する(7/8/M/E/cなど)
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

static int dec_special_graphics_cp(int c) {
	switch (c) {
		case '_': return ' ';
		case '`': return 0x25c6;
		case 'a': return 0x2592;
		case 'b': return 0x2409;
		case 'c': return 0x240c;
		case 'd': return 0x240d;
		case 'e': return 0x240a;
		case 'f': return 0x00b0;
		case 'g': return 0x00b1;
		case 'h': return 0x2424;
		case 'i': return 0x240b;
		case 'j': return 0x2518;
		case 'k': return 0x2510;
		case 'l': return 0x250c;
		case 'm': return 0x2514;
		case 'n': return 0x253c;
		case 'q': return 0x2500;
		case 't': return 0x251c;
		case 'u': return 0x2524;
		case 'v': return 0x2534;
		case 'w': return 0x252c;
		case 'x': return 0x2502;
		case 'y': return 0x2264;
		case 'z': return 0x2265;
		case '{': return 0x03c0;
		case '|': return 0x2260;
		case '}': return 0x00a3;
		case '~': return 0x00b7;
		default:  return c;
	}
}

// bash_str_parse(): bashから読み取った生バイト列を1バイトずつ処理し、
// ANSI/VT100エスケープシーケンスを解釈しながらterm_cell配列とカーソル
// 位置を更新する、ターミナルエミュレータの中核となる状態機械。
//
// state(parse_state)とmode(mode_state)の2段階の状態で管理する:
//   - state == GROUND   : 通常状態。バイトはそのまま画面に出力するか、
//                          \n \r \t \b 等の制御文字として処理する。
//                          0x1b(ESC)を受け取るとSQE_STARTへ遷移する。
//   - state == SQE_START: ESCを受け取った直後。次の1バイトでシーケンスの
//                          種類(mode)を確定させる。
//       mode == IDK      : 種類未確定。'['ならCSI_MODE、']'ならOSC_MODE、
//                          それ以外は2文字限定のシーケンスとして
//                          esc_single_dispatch()で処理しGROUNDへ戻る。
//       mode == CSI_MODE : "ESC [ ... 終端文字" 形式。数字と';'で区切られた
//                          パラメータをctx->palms[]に集め、0x40-0x7Eの
//                          終端文字でls_chr_parse()を呼んでGROUNDへ戻る。
//       mode == OSC_MODE : "ESC ] ... BEL または ESC \" 形式。パラメータは
//                          osc_pal_chr[]に文字列として集め、終端で
//                          osc_mode()を呼んでGROUNDへ戻る。
void bash_str_parse(char *buff, ssize_t size, struct term_context *ctx) {

	for (int i = 0; i < size; i++) {
		enum parse_state *parse_state = get_parse_state(ctx);
		if(parse_state == NULL)return;

		//esc文字が来た場合
		if (*parse_state == SQE_START){
			//escが来て初回の条件分岐
			if(ctx->bash_parser_required_memb.mode == IDK){
				char esc_char = buff[i];
				ctx->bash_parser_required_memb.mode = check_esc_mode(esc_char);
				//switch分でesc_charと同じ文字を解析してしまうのでSINGLE_MODE以外の場合continueする
				if(ctx->bash_parser_required_memb.mode != SINGLE_MODE)continue;
				esc_single_dispatch(ctx, esc_char);
				set_parse_state(ctx,GROUND);
				set_mode(ctx,IDK);
			}

			switch (ctx->bash_parser_required_memb.mode) {
				case OSC_MODE:
						// OSC の終端は BEL (\a) か、ST (ESC \) の2文字シーケンス。
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
							set_parse_state(ctx,GROUND);
							ctx->bash_parser_required_memb.mode = IDK;
							ctx->bash_parser_required_memb.osc_state = NORMAL;

						}
						else{
							ctx->bash_parser_required_memb.osc_state = NORMAL;

							if (*(ctx->palms_counter) == 0 && buff[i] >= '0' && buff[i] <= '9') {
								ctx->bash_parser_required_memb.val = ctx->bash_parser_required_memb.val * 10 + (buff[i] - '0');
								ctx->bash_parser_required_memb.has_val = true;
							} else if (*(ctx->palms_counter) == 0 && buff[i] == ';') {
								if (!ctx->bash_parser_required_memb.has_val) ctx->bash_parser_required_memb.val = 0;
								if (*(ctx->palms_counter) < 16)
									ctx->palms[(*(ctx->palms_counter))++] = ctx->bash_parser_required_memb.val;

								if (ctx->bash_parser_required_memb.osc_pal_chr_counter < sizeof(ctx->bash_parser_required_memb.osc_pal_chr) - 1) {
									ctx->bash_parser_required_memb.osc_pal_chr[ctx->bash_parser_required_memb.osc_pal_chr_counter++] = buff[i];
								}
								ctx->bash_parser_required_memb.val = 0;
								ctx->bash_parser_required_memb.has_val = false;
							}else if (buff[i] >= 0x20 && buff[i] <= 0x7E) {
								if (ctx->bash_parser_required_memb.osc_pal_chr_counter < sizeof(ctx->bash_parser_required_memb.osc_pal_chr) - 1){
									ctx->bash_parser_required_memb.osc_pal_chr[ctx->bash_parser_required_memb.osc_pal_chr_counter++] = buff[i];
								}
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
								set_parse_state(ctx,GROUND);
								set_mode(ctx,IDK);
								ctx->bash_parser_required_memb.is_private = false;
						}
						continue;
						break;
				case G0_SP_MODE:
				case G1_SP_MODE:{
					// chr_data()の対応は false→G0 / true→G1 なので、
					// G1指示子(ESC ) …)の時だけtrueを渡す
					bool g_num = (ctx->bash_parser_required_memb.mode == G1_SP_MODE);
					if(buff[i] == '0'){
						set_char_set(g_num,ruled_lines);
					}
					else if(buff[i] == 'B'){
						set_char_set(g_num,ASCII);
					}
					set_parse_state(ctx,GROUND);
					set_mode(ctx,IDK);
					break;
				}
				case IDK:
						set_parse_state(ctx,GROUND);
						ctx->bash_parser_required_memb.mode = IDK;
						continue;
						break;
				default:
						break;
			}
		}
		else if (ctx->bash_parser_required_memb.state == GROUND) {
			// ESC(0x1b)ならSQE_STARTへ遷移してこの文字自体は消費するだけ。
			// それ以外は制御文字(\b \r \n \t \a)か、画面に書き込む通常の文字。
			enum parse_state tmp_state = 
					buff_state_check(buff[i],ctx->bash_parser_required_memb.state);
			set_parse_state(ctx,tmp_state);
			//stateがgroundになるとmodeをIDKに初期化する
			set_mode(ctx,IDK);

			if (ctx->bash_parser_required_memb.state == SQE_START) continue;
			if (buff[i] == '\b') {
					if (ctx->cur->cur_pos.w > 0) ctx->cur->cur_pos.w--;
					continue;
			} else if (buff[i] == '\x0e') {
					ctx->use_g_charset = true;
					continue;
			} else if (buff[i] == '\x0f') {
					ctx->use_g_charset = false;
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
				// UTF-8 マルチバイト文字を 1 つのコードポイントへデコードする。
				// 罫線素片(U+2500〜)など非ASCII文字を 1 セルに収めるために必要。
				int cp = (unsigned char)buff[i];
				if (cp >= 0x80) {
						int adv = utf8_decode((const unsigned char *)&buff[i], (int)(size - i), &cp);
						i += adv - 1;   // 残り 1 バイトはループの i++ で進む
				} else if (get_char_set(ctx->use_g_charset) == ruled_lines){
						cp = dec_special_graphics_cp(cp);
				}
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

						ctx->term_cell[idx].character = cp;

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
			if (ctx->cur->cur_pos.h >= ctx->term_size.h)
			{
				if (ctx->fixrd_cur_scr_range.decstbm_state)
				{
					ctx->cur->cur_pos.h = ctx->term_size.h - 1;
				} else
				{
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

// ls_chr_parse(): CSIシーケンス("ESC [ パラメータ群 終端文字")の終端文字
// (buff)に応じて処理を振り分ける。パラメータはctx->palms[]に
// palms_counter個格納されている(数値は';'区切りで複数指定可能)。
// is_privateは"ESC[?"のように'?'付きで送られたDEC private シーケンスかどうか。
void ls_chr_parse(struct term_context *ctx, char buff, Color *now_fg_color, Color *now_bg_color, bool is_private) {
	int palms_counter = *(ctx->palms_counter);
	int *palms = ctx->palms;

	switch(buff)
	{
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
			if (mode == 2 || mode == 3) {
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
		// カーソル位置の保存/復元
		case 's':
			*(ctx->save_cur) = *(ctx->cur);
			break;
		case 'u':
			*(ctx->cur) = *(ctx->save_cur);
			break;
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

// osc_mode(): OSCシーケンス("ESC ] Ps ; Pt BEL/ST")を処理する。
// Ps(OSC番号)はctx->palms[0]に入っており、Pt(文字列引数)はosc_pal_chrに
// "Ps;Pt"の形でそのまま格納されている。OSC番号によってウィンドウタイトル
// 変更・カレントディレクトリ通知・色設定・クリップボード操作などを行う。
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

// buff_state_check(): GROUND状態でESC(0x1b)を受け取ったらSQE_START
// (エスケープシーケンス開始)に遷移させる。それ以外は状態を変えない。
enum parse_state buff_state_check(char buff, enum parse_state now_state){
	enum parse_state return_state = now_state;
	if (buff == '\x1b' && return_state == GROUND) return_state = SQE_START;
	return return_state;
}

// check_visible_chr(): 文字が画面に描画すべき文字かどうかを判定する
// （現在この関数を呼び出している箇所は無い、未使用のユーティリティ）
enum visiavle_chr check_visible_chr(char buff){
	enum visiavle_chr vis_state;
	if (buff == '\b') vis_state = BS_ST1;
	else if (buff == '\r') vis_state = NO;
	else vis_state = YES;
	return vis_state;
}

// get_mode(): buff[*i]が'['ならCSI_MODE、']'ならOSC_MODE、それ以外はIDKを返す
// （現在この関数を呼び出している箇所は無く、同等の判定はbash_str_parse()内に
// 直接書かれている。未使用のユーティリティ）
enum mode_state get_mode(char *buff, int *i, int size){
	enum mode_state return_state;
	if (buff[*i] == ']') return_state = OSC_MODE;
	else if (buff[*i] == '[') return_state = CSI_MODE;
	else return_state = IDK;
	return return_state;
}

// base64_decoder(): OSC 52 (クリップボード設定) のペイロード
// "52;c;<base64文字列>" を受け取り、";c;"以降をBase64デコードして
// 復号後のバイト列(NUL終端)を新規ヒープ領域に返す。
// 内部でchar_conbert_binary_arry()/conbert_chr_to_binary_table()を使い、
// Base64文字列を1文字=6bitのビット列に展開してから8bit単位に詰め直す。
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

// char_conbert_binary_arry(): osc_pal_chr内のBase64文字を1文字ずつ
// conbert_chr_to_binary_table()に渡し、6bitのビット配列に展開した結果を
// return_binary構造体(ビット配列+ビット数)として返す
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

// conbert_chr_to_binary_table(): Base64の1文字を6bitに変換してビット配列へ追記する。
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

// change_fg_color/change_bg_color(): 以後に描画する文字の前景色/背景色
// (OSC 10/11で指定された色)を更新する
void change_fg_color(struct term_context *ctx,Color c_col){
	ctx->bash_parser_required_memb.now_fg_color=c_col;
}
void change_bg_color(struct term_context *ctx,Color c_col){
	ctx->bash_parser_required_memb.now_bg_color=c_col;
}

// conbert_num_to_color(): OSC 10/11で渡される rgb:rrrr/gggg/bbbb または #RRGGBB をColorへ変換する。
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

//現在のステートを取得する関数
enum parse_state *get_parse_state(struct term_context *ctx){
	if(ctx == NULL)return NULL;
	return &ctx->bash_parser_required_memb.state;
}

//現在のステートを変更する関数
int set_parse_state(struct term_context *ctx,enum parse_state state){
	if(ctx == NULL)return -1;
	ctx->bash_parser_required_memb.state = state;
	return 0;
}

enum mode_state check_esc_mode(char esc_char){
	if (esc_char == '[') {
		return CSI_MODE;
	} else if (esc_char == ']') {
		return OSC_MODE;
	} else {
		if (esc_char == '('){
			return G0_SP_MODE;
		}
		else if(esc_char == ')') {
			return G1_SP_MODE;
		} else {
			return SINGLE_MODE;
		}
	}
}


int set_mode(struct term_context *ctx,enum mode_state mode){
	if(ctx == NULL)return -1;
	if(ctx->bash_parser_required_memb.mode == mode)return 0;
	else ctx->bash_parser_required_memb.mode = mode;
	return 0;
}


void set_char_set(bool num,enum chr_set chr_set){
	enum chr_set *tmp_chr_set_data = chr_data(num);
	if(tmp_chr_set_data == NULL)return;
	*tmp_chr_set_data = chr_set;
	return;
}
enum chr_set get_char_set(bool num){
	enum chr_set *tmp_chr_set_data = chr_data(num);
	return *tmp_chr_set_data;
}

static enum chr_set *chr_data(bool num){
	static enum chr_set g0 = ASCII;
	static enum chr_set g1 = ASCII;
	if(num == false)return &g0;
	else return &g1;	
}

void set_use_g_char_set(struct term_context *ctx,bool num){
	if(ctx == NULL)return;
	ctx->use_g_charset = num;
}
