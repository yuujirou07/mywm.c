#include <string.h>
#include "pty_drawing.h"
#include "mouse_io.h"
#include"pty_make.h"

// restore_copy_cells(): 選択中に白黒反転したセルを、保存しておいた元の色へ戻す。
static void restore_copy_cells(struct windata *wd)
{
	for(int i = 0; i < wd->copy_data.copy_cell_counter; i++)
	{
		wd->copy_data.copy_cell[i]->bg_color = wd->copy_data.copy_cell_orig_bg[i];
		wd->copy_data.copy_cell[i]->fg_color = wd->copy_data.copy_cell_orig_fg[i];
	}

	wd->copy_data.copy_cell_counter = 0;
	memset(wd->copy_data.copy_cell, 0, wd->ctx->total_cells * sizeof(struct term_cell *));
}


// update_copy_selection(): マウス座標をセル位置へ変換し、ドラッグ開始セルから
// 現在セルまでの範囲をコピー選択として白黒反転する。
static void update_copy_selection(struct windata *wd, double xpos, double ypos)
{
	if (xpos < 0 || ypos < 0) return;
	struct pos on_cell_mouse_pos;

	on_cell_mouse_pos.h = ypos/wd->ctx->cell_h;
	on_cell_mouse_pos.w = xpos/wd->ctx->cell_w;

	if(!(on_cell_mouse_pos.h >= 0 && on_cell_mouse_pos.h < wd->ctx->term_size.h &&
	   on_cell_mouse_pos.w >= 0 && on_cell_mouse_pos.w < wd->ctx->term_size.w))
		return;

	int cell_idx = (on_cell_mouse_pos.h * wd->ctx->term_size.w) + on_cell_mouse_pos.w;

	if(wd->copy_data.copy_cell_idx_data.start_idx_block == false)
	{
		wd->copy_data.copy_cell_idx_data.start_idx = cell_idx;
		wd->copy_data.copy_cell_idx_data.start_idx_block = true;
	}

	wd->copy_data.copy_cell_idx_data.end_idx = cell_idx;

	restore_copy_cells(wd);

	int start_idx = wd->copy_data.copy_cell_idx_data.start_idx;
	int end_idx = wd->copy_data.copy_cell_idx_data.end_idx;
	if(start_idx > end_idx)
	{
		int temp = start_idx;
		start_idx = end_idx;
		end_idx = temp;
	}

	Color bg_color = WHITE;
	Color fg_color = BLACK;

	for(int i = start_idx; i <= end_idx; i++)
	{
		struct term_cell *cell = &wd->ctx->term_cell[i];
		if(memcmp(&cell->bg_color, &bg_color, sizeof(Color)) != 0 ||
		   memcmp(&cell->fg_color, &fg_color, sizeof(Color)) != 0)
		{
			wd->copy_data.copy_cell_orig_bg[wd->copy_data.copy_cell_counter] = cell->bg_color;
			wd->copy_data.copy_cell_orig_fg[wd->copy_data.copy_cell_counter] = cell->fg_color;
			wd->copy_data.copy_cell[wd->copy_data.copy_cell_counter++] = cell;
			cell->bg_color = bg_color;
			cell->fg_color = fg_color;
		}
	}

}

void process_mouse(struct windata *wd) {
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
		if (wd->copy_data.start_copy) {
			restore_copy_cells(wd);
			wd->copy_data.start_copy = false;
		} else {
			wd->copy_data.start_copy = true;
			wd->copy_data.copy_cell_idx_data.start_idx = 0;
			wd->copy_data.copy_cell_idx_data.end_idx = 0;
		}
		wd->copy_data.copy_cell_idx_data.start_idx_block = false;
	}
	if (wd->copy_data.start_copy && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
		Vector2 position = GetMousePosition();
		update_copy_selection(wd, position.x, position.y);
	}
}
