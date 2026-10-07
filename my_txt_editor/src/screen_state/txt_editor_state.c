#include "txt_editor_screen.h"

// 現在のscreen_stateに応じて入力処理を各state関数へ振り分ける。
// 引数: ctx=描画対象や共有状態をまとめた入力context、input_result=get_wch()の結果、ch=入力文字またはKEY_*。 返り値: 入力ループを続けるならtrue、終了要求ならfalse。
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch){
    switch (editor_get_screen_state(ctx->state)){
        case start_menu_screen:
            return handle_start_menu_input(ctx, ch);
        case edit_screen:
            return handle_edit_screen_input(ctx, input_result, ch);
        case file_browse_screen:
            return handle_file_browse_screen_input(ctx, input_result, ch);
        case line_jump_mode:
            return handle_line_jump_mode_input(ctx, ch);
        case error_screen:
            return handle_error_screen_input(ctx, ch);
        case ask_make_file_mode:
            return handle_ask_make_file_mode_input(ctx, input_result, ch);
        case setting_screen:
            return handle_settings_screen_input(ctx,ch,input_result);
        case filetree_screen:
            return handle_filetree_screen_input(ctx,ch,input_result);
            // 未実装。入力を処理せずそのまま継続する。
            return true;
        case screen_state_log_error:
            return true;
    }
    return true;
}

// stateの有効行数に合わせて、0始まりのtarget_lineを範囲内へ丸める。
// 返り値: 有効な論理行番号。有効行がなければ0。
static long clamp_editor_target_line(struct editor_state *state, long target_line){
    int line_limit = editor_line_limit(state);

    if(line_limit <= 0){
        return 0;
    }
    if(target_line >= line_limit){
        return line_limit - 1;
    }
    if(target_line < 0){
        return 0;
    }
    return target_line;
}

// 指定行を少し下に表示するための表示開始行を返す。
// 引数: state=表示領域の高さを持つエディタ状態、target_line=画面内に表示したい論理行番号。 返り値: scr_start_numへ設定する表示開始行。
static int draw_start_line_for_target(struct editor_state *state, long target_line){
    int line_limit = get_line_limit();
    int context_lines = state->write_area.h - 1;
    if(context_lines > 15) context_lines = 15;
    if(context_lines < 0) context_lines = 0;

    target_line = (target_line + state->write_area.h > line_limit)
        ? line_limit - state->write_area.h + context_lines : target_line;
    return (target_line > context_lines) ? (target_line - context_lines) : 0;
}

// stateの編集画面の本文と固定要素を再描画するよう要求し、標準画面を消去する。
// 返り値: なし。実際の再描画はupdate_screenで行う。
static void redraw_edit_screen(struct editor_state *state){
    clear();
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// ファイルブラウザやジャンプ入力から編集画面へ戻す。
// 引数: state=復帰させる状態。 返り値: なし。
void restore_edit_screen(struct editor_state *state){
    editor_set_screen_state(state, edit_screen);
    my_cur_set(state,true);
    redraw_edit_screen(state);
    // 編集位置はstate->cursorに残っているため、退避しておいた画面座標は要らない。
    editor_sync_cursor(state);
}

// 指定行が見える位置へ表示開始行とカーソルを移動する。
// 引数: state=表示位置とカーソル行、target_line=移動先論理行、col=移動後の桁数。 返り値: なし。
void move_view_to_line(struct editor_state *state, long target_line, int col){
    target_line = clamp_editor_target_line(state, target_line);
    int draw_start_line = draw_start_line_for_target(state,target_line);
    state->scr.scr_start_num = draw_start_line;
    editor_set_cursor(state, (int)target_line, col);
    redraw_edit_screen(state);
    editor_sync_cursor(state);
}

// browse_boxの枠と、その上に表示するパス2行を含む消去矩形を返す。
// 引数: 画面座標のブラウザ枠。上端は0で止め、入力自体は変更しない。
static struct box browser_clear_area(struct box browse_box){
    struct box area = browse_box;

    area.pos.y = (browse_box.pos.y >= 2) ? browse_box.pos.y - 2 : 0;
    area.h     = browse_box.h + (browse_box.pos.y - area.pos.y);
    return area;
}

// 画面座標のboxをstateの画面範囲へ切り詰めた矩形を返す。
// 画面外の部分を幅・高さから除き、残らない寸法は0にする。stateと入力boxは変更しない。
static struct box clamp_box_to_screen(struct editor_state *state, struct box box){
    if(box.pos.x < 0){
        box.w += box.pos.x;
        box.pos.x = 0;
    }
    if(box.pos.y < 0){
        box.h += box.pos.y;
        box.pos.y = 0;
    }
    if(box.pos.x + box.w > state->scr.scr_size.x){
        box.w = state->scr.scr_size.x - box.pos.x;
    }
    if(box.pos.y + box.h > state->scr.scr_size.y){
        box.h = state->scr.scr_size.y - box.pos.y;
    }
    if(box.w < 0){
        box.w = 0;
    }
    if(box.h < 0){
        box.h = 0;
    }
    return box;
}

// ctxの現在画面に応じて枠・配置基準・表示位置を新しい画面寸法へ合わせ、再描画を要求する。
// 返り値: 更新処理後1、ctxまたはstateがNULLなら0。実際の描画はupdate_screenで行う。
int update_screen_ratio(struct editor_input_context *ctx){
    if(ctx == NULL || ctx->state == NULL)return 0;

    struct editor_state *state = ctx->state;

    // 中央寄せ基準はどの画面でも使うため先に更新する
    ctx->ask_make_file_mode.screen_center_y = state->scr.scr_size.y / 2;
    ctx->ask_make_file_mode.screen_center_pos = (struct pos){
        state->scr.scr_size.x / 2,
        ctx->ask_make_file_mode.screen_center_y
    };

    enum screen_state now_state = editor_get_screen_state(ctx->state);
    switch(now_state){
        case file_browse_screen: {
            // 枠を作り直すと縮小時に古い枠が残る。start menu側のロゴを消さないよう、
            // clear()ではなく「前の枠+上のパス表示2行」の範囲だけを消去予約する。
            struct box old_box = state->file_browse.box;

            resize_file_browser(state);

            struct box clear_area = clamp_box_to_screen(state, browser_clear_area(old_box));
            bool is_box_moved = (old_box.pos.x != state->file_browse.box.pos.x ||
                                 old_box.pos.y != state->file_browse.box.pos.y ||
                                 old_box.w     != state->file_browse.box.w ||
                                 old_box.h     != state->file_browse.box.h);

            if(is_box_moved && clear_area.w > 0 && clear_area.h > 0){
                request_clear_box(state, clear_area);
            }
            state->render_flags |= RENDER_FILE_BROWSE;
            break;
        }
        case ask_make_file_mode:
            // 確認枠と入力欄は新しい中央基準で置き直す。下の編集画面ごと描き直す。
            clear();
            show_make_file_prompt(ctx->win, state, &state->ask_make_file_box,
                                  ctx->ask_make_file_mode.screen_center_y,
                                  ctx->ask_make_file_mode.screen_center_pos);
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            state->render_flags |= RENDER_MAKE_FILE;
            break;
        case line_jump_mode:
            // 入力欄の位置はステータスバー基準で毎回計算されるので描き直すだけでよい
            clear();
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            state->render_flags |= RENDER_LINE_JUMP;
            break;
        case error_screen:
            // エラー文言を保持していないため描き直せない。表示位置の基準になる
            // file_browse.areaだけ新サイズへ合わせ、戻ったときにずれないようにする。
            resize_file_browser(state);
            break;
        case start_menu_screen:
            // start menu pluginが次のループで新しい画面サイズを見て描き直す
            break;
        case setting_screen: {
            //枠の大きさは項目の内容で決まる。縮小時に古い枠が残らないよう、
            //前の枠の範囲だけを消去予約してから作り直す。
            struct box old_box = state->settings_screen_data.box;

            set_settings_screen_box(state);

            struct box clear_area = clamp_box_to_screen(state, old_box);
            bool is_box_moved =
                (old_box.pos.x != state->settings_screen_data.box.pos.x ||
                 old_box.pos.y != state->settings_screen_data.box.pos.y ||
                 old_box.w     != state->settings_screen_data.box.w ||
                 old_box.h     != state->settings_screen_data.box.h);

            if(is_box_moved && clear_area.w > 0 && clear_area.h > 0){
                request_clear_box(state, clear_area);
            }
            state->render_flags |= RENDER_SETTINGS;
            break;
        }
        case filetree_screen:
            // ツリーの枠を新しい画面幅の比率で作り直し、編集領域の左端も作り直す。
            // show_filetree()が編集画面ごと描き直しを要求するので、
            // リサイズで残る古い枠は先に消しておく。
            clear();
            show_filetree(ctx,state->file_tree_data.ft_box);
            editor_sync_cursor(state);
            break;
        case edit_screen:
        default: {
            // 高さが縮むとカーソル行が編集領域の外へ出るため、はみ出したときだけ
            // 表示開始行を取り直す。収まっているならスクロール位置は動かさない。
            if(!editor_cursor_is_visible(state)){
                //内部でclear()と再描画要求、カーソル移動まで行う
                move_view_to_line(state, state->cursor.file_pos.y, state->cursor.file_pos.x);
                break;
            }

            clear();
            state->render_flags |= RENDER_EDIT_SCREEN_BASE;
            state->render_flags |= RENDER_FILE_DATA;
            editor_sync_cursor(state);
            break;
        }
    }

    return 1;
}
