#ifndef TXT_EDITOR_SCREEN_H
#define TXT_EDITOR_SCREEN_H

#include "txt_editor.h"

bool handle_edit_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);
bool handle_file_browse_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch);
bool handle_line_jump_mode_input(struct editor_input_context *ctx, wint_t ch);
bool handle_error_screen_input(struct editor_input_context *ctx, wint_t ch);
bool handle_ask_make_file_mode_input(struct editor_input_context *ctx, int input_result, wint_t ch);
bool handle_start_menu_input(struct editor_input_context *ctx, wint_t ch);
bool handle_settings_screen_input(struct editor_input_context *ctx,wint_t ch,int input_result);

void reset_jump_mode(struct editor_state *state);
void show_make_file_prompt(WINDOW *win, struct editor_state *state, struct box *file_box,
                           int screen_center_y, struct pos screen_center_pos);

#endif
