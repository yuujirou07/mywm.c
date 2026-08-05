#include "mywm_core.h"

//キーの修飾キーが変化したときに呼ばれる関数
void modifire_key(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, key_modifier);
	//キーボード構造体を取得。グローバル変数よりdataのほうが新しい可能性があるため、dataから取得する
	struct wlr_keyboard *keyboard = data;
	// xkbが直接何を「押された」と認識しているか確認する
	xkb_state_serialize_mods(keyboard->xkb_state, XKB_STATE_MODS_DEPRESSED);
	// クライアントに修飾キーの状態を通知する関数
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->modifiers);
}
