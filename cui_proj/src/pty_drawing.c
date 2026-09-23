#include <stdlib.h>
#include "pty_drawing.h"

int window_init(void) {
	SetConfigFlags(FLAG_WINDOW_HIGHDPI);
	InitWindow(800, 600, "cui");
	if (!IsWindowReady()) return -1;
	SetExitKey(KEY_NULL);
	int refresh_rate = GetMonitorRefreshRate(GetCurrentMonitor());
	SetTargetFPS(refresh_rate > 0 ? refresh_rate : 60);
	return 0;
}

void render_cells(struct windata *wd) {
	struct term_context *ctx = wd->ctx;
	Font font = GetFontDefault();
	BeginDrawing();
	ClearBackground(BLACK);
	for (int row = 0; row < ctx->term_size.h; row++) {
		for (int col = 0; col < ctx->term_size.w; col++) {
			struct term_cell *cell = &ctx->term_cell[row * ctx->term_size.w + col];
			Color bg = cell->bg_color;
			Color fg = cell->fg_color;
			if (col == ctx->cur->cur_pos.w && row == ctx->cur->cur_pos.h) {
				bg = (Color){255 - bg.r, 255 - bg.g, 255 - bg.b, 255};
				fg = (Color){255 - fg.r, 255 - fg.g, 255 - fg.b, 255};
			}
			bg.a = fg.a = 255;
			int x = col * ctx->cell_w;
			int y = row * ctx->cell_h;
			DrawRectangle(x, y, ctx->cell_w, ctx->cell_h, bg);
			if (cell->character >= 32)
				DrawTextCodepoint(font, cell->character, (Vector2){x, y},
					(float)ctx->cell_h, fg);
		}
	}
	EndDrawing();
}

void destroy_data(struct windata *wd) {
	free(wd->copy_data.copy_cell);
	free(wd->copy_data.copy_cell_orig_bg);
	free(wd->copy_data.copy_cell_orig_fg);
	free(wd->kbd_data.paste);
	CloseWindow();
}
