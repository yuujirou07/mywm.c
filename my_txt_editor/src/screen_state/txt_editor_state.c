#include "txt_editor_screen.h"

// editor_handle_screen_input(): 現在のscreen_stateに応じて入力処理を各state関数へ振り分ける。
// 引数: ctx=描画対象や共有状態をまとめた入力context、input_result=get_wch()の結果、ch=入力文字またはKEY_*。
// 返り値: 入力ループを続けるならtrue、終了要求ならfalse。
bool editor_handle_screen_input(struct editor_input_context *ctx, int input_result, wint_t ch){
    switch (editor_get_screen_state(ctx->state)) {
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
        case screen_state_log_error:
            return true;
    }
    return true;
}

// clamp_editor_target_line(): 移動先行番号を編集可能な範囲へ丸める。
// 引数: state=有効行数を持つエディタ状態、target_line=移動したい論理行番号。
// 返り値: 有効範囲内の論理行番号。
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

// draw_start_line_for_target(): 指定行を少し下に表示するための表示開始行を返す。
// 引数: state=表示領域の高さを持つエディタ状態、target_line=画面内に表示したい論理行番号。
// 返り値: scr_start_numへ設定する表示開始行。
static int draw_start_line_for_target(struct editor_state *state, long target_line){
    int line_limit = get_line_limit();
    target_line = ( target_line + state->write_area.h > line_limit ) ? line_limit -  state->write_area.h + 15 : target_line;
    return (target_line > 15) ? (target_line - 15) : 0;
}

// redraw_edit_screen(): 編集画面の再描画を要求する。
// 実際に描くのはupdate_screen()で、区切り線の座標もそちらがctxから読む。
// 引数: state=描画要求を積むエディタ状態。
// 返り値: なし。
static void redraw_edit_screen(struct editor_state *state){
    clear();
    state->render_flags |= RENDER_EDIT_SCREEN_BASE;
    state->render_flags |= RENDER_FILE_DATA;
}

// restore_edit_screen(): ファイルブラウザやジャンプ入力から編集画面へ戻す。
// 引数: state=復帰させる状態。
// 返り値: なし。
void restore_edit_screen(struct editor_state *state){
    editor_set_screen_state(state, edit_screen);
    state->is_cur_show = true;
    curs_set(true);
    redraw_edit_screen(state);
    // 編集位置はstate->cursorに残っているため、退避しておいた画面座標は要らない。
    editor_sync_cursor(state);
}

// move_view_to_line(): 指定行が見える位置へ表示開始行とカーソルを移動する。
// 引数: state=表示位置とカーソル行、target_line=移動先論理行、col=移動後の桁数。
// 返り値: なし。
void move_view_to_line(struct editor_state *state, long target_line, int col){
    target_line = clamp_editor_target_line(state, target_line);
    int draw_start_line = draw_start_line_for_target(state,target_line);
    state->scr.scr_start_num = draw_start_line;
    editor_set_cursor(state, (int)target_line, col);
    redraw_edit_screen(state);
    editor_sync_cursor(state);
}

// browser_clear_area(): ファイルブラウザが実際に塗っていた範囲を返す。
// 枠線を含む外枠と、枠の上に出るパス表示2行分を含める。
// 引数: browse_box=消去したいファイルブラウザ外枠。
// 返り値: 消去対象の矩形。
static struct box browser_clear_area(struct box browse_box){
    struct box area = browse_box;

    area.pos.y = (browse_box.pos.y >= 2) ? browse_box.pos.y - 2 : 0;
    area.h     = browse_box.h + (browse_box.pos.y - area.pos.y);
    return area;
}

// clamp_box_to_screen(): 矩形を現在の画面内へ収める。
// リサイズ前の矩形は新しい画面からはみ出すことがあり、clear_box()は
// box.w分の一時バッファを取るため、負値や画面外を渡さないようにする。
// 引数: state=現在の画面サイズ、box=丸める矩形。
// 返り値: 画面内に収めた矩形。収まる部分が無ければw/hが0。
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

// update_screen_ratio(): 画面サイズが変わったあと、今表示している画面の配置を
// 新しい画面サイズの比率で作り直し、必要な再描画要求をrender_flagsへ積む。
// 実際の描画はupdate_screen()が行うため、ここでは配置更新と要求だけを行う。
// 引数: ctx=画面状態・各領域・中央寄せ基準を持つ入力context。
// 返り値: 配置を更新したら1、ctxが無効で何もしなかったら0。
int update_screen_ratio(struct editor_input_context *ctx){
    if(ctx == NULL || ctx->state == NULL)return 0;

    struct editor_state *state = ctx->state;

    // 中央寄せ基準はどの画面でも使うため先に更新する
    ctx->ask_make_file_mode.screen_center_y = state->scr.scr_size.y / 2;
    ctx->ask_make_file_mode.screen_center_pos = (struct pos){
        state->scr.scr_size.x / 2,
        ctx->ask_make_file_mode.screen_center_y
    };

    enum now_screen_state now_state = editor_get_screen_state(ctx->state);
    switch(now_state){
        case file_browse_screen: {
            // 枠を作り直すと縮小時に古い枠が残る。start menu側のロゴを消さないよう、
            // clear()ではなく「前の枠+上のパス表示2行」の範囲だけを消去予約する。
            struct box old_box = ctx->file_browse_screen.box;

            resize_file_browser(ctx);

            struct box clear_area = clamp_box_to_screen(state, browser_clear_area(old_box));
            bool is_box_moved = (old_box.pos.x != ctx->file_browse_screen.box.pos.x ||
                                 old_box.pos.y != ctx->file_browse_screen.box.pos.y ||
                                 old_box.w     != ctx->file_browse_screen.box.w ||
                                 old_box.h     != ctx->file_browse_screen.box.h);

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
            // file_browser_areaだけ新サイズへ合わせ、戻ったときにずれないようにする。
            resize_file_browser(ctx);
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
        case edit_screen:
        default: {
            // 高さが縮むとカーソル行が編集領域の外へ出るため、はみ出したときだけ
            // 表示開始行を取り直す。収まっているならスクロール位置は動かさない。
            if(!editor_cursor_is_visible(state)){
                //内部でclear()と再描画要求、カーソル移動まで行う
                move_view_to_line(state, state->cursor.line, state->cursor.col);
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
