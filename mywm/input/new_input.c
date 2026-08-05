#include "mywm_core.h"

//新しい入力デバイスが接続されたら実行する
void newinput_device(struct wl_listener *listener, void *data) {
	//第一引数から、第一引数が含まれているポインタのアドレスを逆算する
	struct server *server = wl_container_of(listener, server, new_input);
	//第二引数のアドレスをローカルポインタに代入する
	struct wlr_input_device *device = data;

	//もしデバイスタイプがポインターだったら
	if(device->type == WLR_INPUT_DEVICE_POINTER)
	{
		size_t index = server->mouce_structure.pointer_count;
		if(index >= MAX_POINTER_DEVICES)
		{
			return;
		}

		struct my_pointer *pointer = calloc(1, sizeof(*pointer));
		server->mouce_structure.pointers[index] = pointer;
		pointer->server = server;
		pointer->device = device;
		pointer->motion.notify = newinput_mouce;
		pointer->button.notify = newinput_moucebotton;

		//libinputデバイスか確認
		if(wlr_input_device_is_libinput(pointer->device))
		{
			//生のlibinput_deviceのポインタ取得
			struct libinput_device *ldev = wlr_libinput_get_device_handle(pointer->device);
			libinput_device_config_accel_set_profile(ldev, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
			libinput_device_config_accel_set_speed(ldev, 0.1);
			//ポインタの中身のポインタデバイスのタップ機能を有効にする
			libinput_device_config_tap_set_enabled(ldev, LIBINPUT_CONFIG_TAP_ENABLED);
		}

		//カーソルと物理マウスを紐付ける関数
		wlr_cursor_attach_input_device(server->mouce_structure.cursor, pointer->device);
		//マウスが動いたときに発火する
		wl_signal_add(&server->mouce_structure.cursor->events.motion, &pointer->motion);
		//マウスのボタンが押されたときに発火する
		wl_signal_add(&server->mouce_structure.cursor->events.button, &pointer->button);
		pointer->mouce_scroll_listener.notify = mouse_scroll_func;
		//マウススクロールリスナー
		wl_signal_add(
			&server->mouce_structure.cursor->events.axis,
			&pointer->mouce_scroll_listener);
		server->mouce_structure.pointer_count++;
		return;
	}

	if(device->type == WLR_INPUT_DEVICE_KEYBOARD)
	{
		//インプットデバイスからキーボード構造体を受け取る
		server->keyboard = wlr_keyboard_from_input_device(device);
		//コンテキスト（キーボードの状態などを扱うための作業領域）を確保する
		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		//キーボードのレイアウトをjpにする
		struct xkb_rule_names rules = {
			.layout = "jp",
		};
		//キーマップを作成する。物理キーコード(キーID（数値）)を論理キー（例えば A や Enter）に変換る
		//OS がどのキーを押したかを解釈するために必要
		struct xkb_keymap *keymap =
			xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

		//キーボードの状態を管理する構造体を作成する。これがないと、OSはどのキーが押されているかを把握できない
		xkb_state_new(keymap);
		//XKB keymap をキーボードに紐付ける関数
		wlr_keyboard_set_keymap(server->keyboard, keymap);
		//context構造体は使わないのでメモリを解放する
		xkb_context_unref(context);
		xkb_keymap_unref(keymap);

		//キーボードのキーイベントをリスナーに登録する
		wl_signal_add(&server->keyboard->events.key, &server->key);
		//修飾キーを登録する
		wl_signal_add(&server->keyboard->events.modifiers, &server->key_modifier);
		// クライアントにこのseatはキーボードとマウスが使えるよ」と宣言する関数
		//Seat とは、マウスやキーボードなどの入力デバイスをひとまとめにした論理的なグループを指します。
		wlr_seat_set_capabilities(
			server->seat,
			WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
		// seatの内部に「実際に使うキーボードはこれ」と登録する関数
		wlr_seat_set_keyboard(server->seat, server->keyboard);
	}
}
