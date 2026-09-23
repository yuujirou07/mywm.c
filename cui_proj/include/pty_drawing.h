#ifndef PTY_DRAWING_H
#define PTY_DRAWING_H

#include "pty_make.h"

#define TERMINAL_FONT_SIZE 10

struct windata {
	int master_fd;
	struct term_context *ctx;
	bool should_close;

	struct {
		struct {
			int start_idx;
			int end_idx;
			bool start_idx_block;
		} copy_cell_idx_data;
		int copy_cell_counter;
		struct term_cell **copy_cell;
		Color *copy_cell_orig_bg;
		Color *copy_cell_orig_fg;
		bool start_copy;
	} copy_data;

	struct {
		char *paste;
		size_t paste_offset;
	} kbd_data;
};

int window_init(void);
void render_cells(struct windata *wd);
void destroy_data(struct windata *wd);

#endif
