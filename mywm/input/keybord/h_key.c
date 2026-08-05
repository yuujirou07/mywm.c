#include "mywm_core.h"

//キーイベントが発生したときに呼ばれる関数
void h_key(struct wl_listener *listener, void *data) {
	//リスナーからサーバ構造体を取得する
	struct server *server = wl_container_of(listener, server, key);
	//キーボード構造体を取得
	struct wlr_keyboard *keyboard = server->keyboard;
	//送られてきたデータをeventに代入する
	struct wlr_keyboard_key_event *event = data;
	//libxkbcommonの仕様上メンバのキーコードの値＋８をする
	uint32_t keycode = event->keycode + 8;
	//文字コードをいれる変数
	const xkb_keysym_t *syms;

	//デバイスのキーボードのデータが入っているkeybord構造体の
	// 押された物理キー番号からどのキーが押されたかを計算し、
	//symsポインタにいれる。（返り値は押されたキーの個数
	int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);
	for(int i = 0; i < nsyms; i++)
	{
		if(syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R)
		{
			server->super_pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
		}
	}

	//修飾キーを取り出す
	wlr_keyboard_get_modifiers(keyboard);
	//フォーカス中のクライアントにキーイベントを転送する関数です。引数は、シート、イベントの時間、キーコード、キーの状態（押されたか離されたか）です。
	wlr_seat_keyboard_notify_key(
		server->seat,
		event->time_msec,
		event->keycode,
		event->state);
	//もしキーが離されたときのイベントだったら、以降の処理をしない
	if(event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
	{
		return;
	}

	//キーネームを格納する変数
	char name[64];
	//押されたキーの個数回ループする
	for(int i = 0; i < nsyms; i++)
	{
		//syms[i]から数値を取り出し数値に対応する文字列を
		// name[64]にいれる(文字コードのような概念)
		xkb_keysym_get_name(syms[i], name, sizeof(name));
		printf("%s\n", name);
		//もし押されたキーがEscapeだったら、サーバを終了する
		if(strcmp(name, "Escape") == 0)
		{
			wl_display_terminate(server->display);
		}

		//もし押されたキーがSuper_LとReturnだったら、起動ソフト選択画面を表示する
		if(server->super_pressed && syms[i] == XKB_KEY_Return)
		{
			pid_t pid = fork();
			if(pid == 0)
			{
				// 子プロセスの処理
				//Vulkanローダーが存在しないNVIDIAのICDをプローブして起動が遅くなるのを防ぐため、Radeonのみに限定する
				setenv("VK_ICD_FILENAMES", "/usr/share/vulkan/icd.d/radeon_icd.json", 1);
				if(execlp(
					"/home/yuujirou07/vscode_proj/mywm_proj/cui_proj/pty_make_v1",
					"pty_make_v1",
					NULL) == -1)
				{
					perror("execlp");
					exit(1);
				}
			}
			else if(pid < 0)
			{
				//エラー
				printf("forkerror/n");
				wl_display_terminate(server->display);
			}
		}
	}
}
