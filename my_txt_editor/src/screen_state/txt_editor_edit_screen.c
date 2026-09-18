#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include "filetree.h"
#include "txt_editor.h"
#include "txt_editor_syntax.h"
#include "txt_editor_screen.h"

static void send_lsp_did_change(struct editor_input_context *ctx);

// handle_edit_screen_input(): 通常編集画面のキー入力を処理する。
// 引数: ctx=編集画面の描画・状態更新に必要なcontext、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue、qで終了するならfalse。
bool handle_edit_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch){
    struct editor_state *state = ctx->state;
    WINDOW *win = ctx->win;

    if(state->file_data.now_open_path_name[0] != '\0' && state->settings_data->show_status_bar){
        state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    }
    if (input_result == KEY_CODE_YES && ch == KEY_BACKSPACE && state->is_cur_show) {
        handle_backspace(ctx);
        send_lsp_did_change(ctx);
        return true;
    }
    else if(ch == CTRL('h')){
        editor_set_screen_state(state, line_jump_mode);
        reset_jump_mode(state);
        state->render_flags |= RENDER_LINE_JUMP;
        return true;
    }
    else if (ch == CTRL('f')) {
        my_cur_set(state,false);
        editor_set_screen_state(state, file_browse_screen);
        state->render_flags |= RENDER_FILE_BROWSE;
        return true;
    }
    else if(ch == CTRL('s')){
        // 編集位置はstate->cursorが持ち続けるため、画面を離れる前の退避は不要。
        save_file(state);
        flushinp();
        if(editor_get_screen_state(state) == ask_make_file_mode){
            show_make_file_prompt(
                win,
                state,
                &state->ask_make_file_box,
                ctx->ask_make_file_mode.screen_center_y,
                ctx->ask_make_file_mode.screen_center_pos
            );
        }
        return true;
    }
    else if(ch == CTRL('n')){
        editor_set_screen_state(state,filetree_screen);
        // 編集領域をツリーの幅だけ右へ寄せる。
        show_filetree(ctx);
    }
    else if(ch == CTRL(' ')){
       lsp_send_completion(ctx->lsp_data->to_server_fd,state);
    }
    if (ch == KEY_MOUSE) {
        handle_mouse(ctx, 0);
        state->render_flags |= RENDER_LINE;

        return true;
    }
    if (state->is_cur_show) {
        if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
            handle_newline(ctx);
        } else if (ch == '\t') {
            handle_tab(win, state);
        } else if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            handle_input_allow(ctx,ch);

        } else if (input_result == OK && iswprint((wint_t)ch)) {
            if (ch == 'q') {
                return false;

            }
            handle_char_input(win, (wchar_t)ch, state);
            send_lsp_did_change(ctx);
        }
        struct pos write_area_pos = editor_cursor_write_area_pos(state);
        update_line_syntax_data(ctx,write_area_pos.y);
        state->render_flags |= RENDER_LINE_STATUS;
    } else {
        if (input_result == OK && iswprint((wint_t)ch)) {
            // カーソル非表示中は、論理行を見える位置へ戻してから入力する。
            if(editor_line_limit(state) == 0){
                return true;
            }
            move_view_to_line(state, state->cursor.file_pos.y, state->cursor.file_pos.x);
            my_cur_set(state,true);
            handle_char_input(win, (wchar_t)ch, state);
            send_lsp_did_change(ctx);

            state->render_flags |= RENDER_LINE_STATUS;
        }
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN){
            // nは現在の論理行。move_view_to_line()がstateを書き換える前に保持する。
            int n = editor_cursor_logical_line_pos(state);
            my_cur_set(state,true);
            syntax syntax_data = {0};
            move_view_to_line(state, n - 1, 0);

            if(state->settings_data->built_in_syntax){
                now_usint_syntax_ptr_ctl(&syntax_data,get);
                set_syntax_data(&syntax_data,ctx);
            }
        }
    }
    if(ch == 'q') {
        return false;
    }
    return true;
}

// send_lsp_did_change(): 編集バッファ全体をUTF-8化してLSPへ変更通知を送る。
// 引数: ctx=編集状態とLSP通信状態を持つ入力context。
// 返り値: なし。LSP未使用・未初期化・送信準備失敗時は通知しない。
// 所有権: 送信成功時のUTF-8文字列はstateが保持し、失敗時はこの関数が解放する。
static void send_lsp_did_change(struct editor_input_context *ctx){


    struct editor_state *state = ctx->state;
    struct lsp_process *lsp = ctx->lsp_data;
    char uri[(PATH_MAX * 3) + sizeof("file://")];
    char *text;
    int next_version;

    if(!state->settings_data->lsp.lsp_use || lsp == NULL ||
       !lsp->initialized || lsp->to_server_fd < 0 ||
       lsp->update_data.file_update_counter < 1 ||
       strcmp(lsp->update_data.path_name, state->file_data.now_open_path_name) != 0 ||
       lsp_path_to_file_uri(uri, sizeof(uri), state->file_data.now_open_path_name) == -1){
        return;
    }

    // 危険: 文字入力ごとに全文を確保・UTF-8化し、blockingなpipeへ同期送信する。
    // ファイルやLSP応答が重いと入力処理そのものが長時間停止する。
    text = editor_buffer_to_utf8(state);
    if(text == NULL){
        return;
    }

    next_version = lsp->update_data.file_update_counter + 1;
    if(lsp_send_did_change(lsp->to_server_fd, uri, next_version, text) == 0){
        lsp->update_data.file_update_counter = next_version;
        free(state->str.chr_file_all_str_data);
        state->str.chr_file_all_str_data = text;
        return;
    }

    free(text);
}
