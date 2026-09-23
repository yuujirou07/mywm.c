#include<stdlib.h>
#include<string.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include"pty_drawing.h"
#include"keybord.h"
#include"error_log_output.h"
#include"pty_make.h"
void process_keyboard(struct windata *wd) {
	bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
		IsKeyReleased(KEY_LEFT_CONTROL) || IsKeyReleased(KEY_RIGHT_CONTROL);
	int codepoint;
	while ((codepoint = GetCharPressed()) != 0) {
		character_callback(wd, codepoint);
	}
	int key;
	while ((key = GetKeyPressed()) != 0) {
		// 同じフレーム内でCtrlを押して離した場合も、押下キューから拾う。
		if (key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL) control = true;
		key_callback(wd, key, false, control);
	}
	const int repeat_keys[] = {KEY_BACKSPACE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN};
	for (size_t i = 0; i < sizeof(repeat_keys) / sizeof(repeat_keys[0]); i++) {
		if (IsKeyPressedRepeat(repeat_keys[i]))
			key_callback(wd, repeat_keys[i], true, control);
	}

	if (wd->kbd_data.paste) {
		char *pending = wd->kbd_data.paste + wd->kbd_data.paste_offset;
		ssize_t written = write(wd->master_fd, pending, strlen(pending));
		if (written > 0) wd->kbd_data.paste_offset += (size_t)written;
		if (!wd->kbd_data.paste[wd->kbd_data.paste_offset] ||
			(written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
			if (written < 0) error_log_write("clipboard write error");
			free(wd->kbd_data.paste);
			wd->kbd_data.paste = NULL;
			wd->kbd_data.paste_offset = 0;
		}
	}
}

void key_callback(struct windata *wd, int key, bool repeat, bool control) {
	if (key == KEY_E && !repeat && control) {
		wd->should_close = true;
	}
	else if (key == KEY_V && !repeat && control) {
		if (wd->kbd_data.paste) return;
		const char *clipboard = GetClipboardText();
		if (!clipboard) return;
		wd->kbd_data.paste = malloc(strlen(clipboard) + 1);
		if (!wd->kbd_data.paste) {
			error_log_write("clipboard allocation error");
			return;
		}
		size_t count = 0;
		for (size_t i = 0; clipboard[i]; i++) {
			unsigned char c = (unsigned char)clipboard[i];
			if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t')
				wd->kbd_data.paste[count++] = c;
		}
		wd->kbd_data.paste[count] = '\0';
		wd->kbd_data.paste_offset = 0;
	}
	else if(!repeat && control && key >= KEY_A && key <= KEY_Z)
	{
		char ctrl_key = (char)(key - KEY_A + 1);
		write(wd->master_fd, &ctrl_key, 1);
		wd->ctx->cur->now_writing = true;
	}
	else if(key == KEY_ENTER && !repeat)
	{
		char enter_key = 13;
		write(wd->master_fd, &enter_key, 1);
	}
	else if(key == KEY_BACKSPACE)
	{
		char c = 0x7f;
		write(wd->master_fd, &c, 1);
		wd->ctx->cur->now_writing = true;
	}
	else if(key == KEY_ESCAPE && !repeat)
	{
		write(wd->master_fd, "\x1b", 1);
	}
	//fnキー入力処理
	else if(!repeat && key >= KEY_F1 && key <= KEY_F12)
	{
		switch(key)
		{
			case KEY_F1:  write(wd->master_fd, "\x1bOP",   3); break;
			case KEY_F2:  write(wd->master_fd, "\x1bOQ",   3); break;
			case KEY_F3:  write(wd->master_fd, "\x1bOR",   3); break;
			case KEY_F4:  write(wd->master_fd, "\x1bOS",   3); break;
			case KEY_F5:  write(wd->master_fd, "\x1b[15~", 5); break;
			case KEY_F6:  write(wd->master_fd, "\x1b[17~", 5); break;
			case KEY_F7:  write(wd->master_fd, "\x1b[18~", 5); break;
			case KEY_F8:  write(wd->master_fd, "\x1b[19~", 5); break;
			case KEY_F9:  write(wd->master_fd, "\x1b[20~", 5); break;
			case KEY_F10: write(wd->master_fd, "\x1b[21~", 5); break;
			case KEY_F11: write(wd->master_fd, "\x1b[23~", 5); break;
			case KEY_F12: write(wd->master_fd, "\x1b[24~", 5); break;
		}
	}
	//ナビゲーションキー入力処理
	else if(key == KEY_HOME      && !repeat) write(wd->master_fd, "\x1b[1~", 4);
	else if(key == KEY_INSERT    && !repeat) write(wd->master_fd, "\x1b[2~", 4);
	else if(key == KEY_DELETE    && !repeat) write(wd->master_fd, "\x1b[3~", 4);
	else if(key == KEY_END       && !repeat) write(wd->master_fd, "\x1b[4~", 4);
	else if(key == KEY_PAGE_UP   && !repeat) write(wd->master_fd, "\x1b[5~", 4);
	else if(key == KEY_PAGE_DOWN && !repeat) write(wd->master_fd, "\x1b[6~", 4);
	else if(key == KEY_TAB && !repeat)
	{
		write(wd->master_fd, "\t", 1);
	}
	else if(key == KEY_RIGHT)
	{
		//右のセルが空白ならカーソルをブロックする
		if(wd->ctx->cur->cur_pos.w + 1 < wd->ctx->term_size.w)
		{
			if(wd->ctx->cur->cur_pos.w < wd->ctx->temp_cur_pos.w + 1 ||
			   wd->ctx->term_cell[wd->ctx->cur->cur_pos.h * wd->ctx->term_size.w + wd->ctx->cur->cur_pos.w + 1].character != ' ')
			{
				cur_allow_write(wd->ctx->cur->allow_mode, wd->master_fd, key);
				wd->ctx->cur->now_writing = true;
			}
		}
	}
	else if(key == KEY_LEFT || key == KEY_UP || key == KEY_DOWN)
	{
		cur_allow_write(wd->ctx->cur->allow_mode, wd->master_fd, key);
		wd->ctx->cur->now_writing = true;
	}
}

void character_callback(struct windata *wd, unsigned int codepoint) {
	if(codepoint < 32 || codepoint > 127)
		return;

	char utf8[4] = {0};
	int len = 0;
	unicode_utf8_encoder(utf8, codepoint, &len);

	wd->ctx->cur->now_writing = true;
	write(wd->master_fd, utf8, len);
}
