#include "mywm_core.h"

//関数ポインタを構造体のメンバにセットする関数
void function_set(struct server *server) {
	//server->key.notifyにh_key関数アドレスを代入している
	server->key.notify = h_key;
	//上記と同じ
	server->new_input.notify = newinput_device;
	//new_output関数のアドレスを代入
	server->new_output.notify = new_output;
	//描画可能シグナルとoutput_frame関数を実行する
	server->frame.notify = output_frame;
	// 新しいウィンドウが要求された時に呼ばれる関数を登録
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	//修飾キー発火時実行する関数の登録
	server->key_modifier.notify = modifire_key;
	//レイヤーシェルサーフェス発火時実行する関数の登録
	server->wallpaper.wallpaper_manage_listner.wallpaper_listner.notify = layer_shell_create;
	//描画可能時に実行左折関数の登録
	server->wallpaper.wallpaper_manage_listner.commit_listener.notify = wallpaper_commit;
	//ステータスバーの描画要求時に実行する関数
	server->mybar.mybar_listner.commit_listener.notify = request_mybar;


	//クライアントのマウスカーソル変更シグナル発火時のリスナー
	server->mouce_structure.set_cliant_cursor_image_listener.notify = get_cliant_cursor_image;
	//タイトルバーなどのデコレーションをの設定をサーフェス生成時にする関数
	server->xdg_decoration_listener.notify = server_new_xdg_decoration;
}

//終了時にメモリを開放する関数
void server_destroy(struct server *server) {
	// 1. 全てのクライアント（foot や swaybg など）に終了要求を送り、強制切断する
	wl_display_destroy_clients(server->display);

	// 【追加】Seat が破壊される前に、自分で登録したリスナーの「耳」を取り外す
	// これを行わないと、wlroots の安全装置(Assertion)が作動して abort します
	wl_list_remove(&server->mouce_structure.set_cliant_cursor_image_listener.link);

	// （もし現在のコードで wlr_seat_destroy(server->seat); を明記している場合は、この位置に書きます）

	// 2. カーソルマネージャーとカーソル自体の解放
	wlr_xcursor_manager_destroy(server->mouce_structure.cursor_mgr);
	wlr_cursor_destroy(server->mouce_structure.cursor);

	// 3. アロケータとレンダラーの解放
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);

	// 4. ディスプレイ（大元）の破壊
	wl_display_destroy(server->display);

	// 5. 最後に配列で確保していたマウスのメモリと、コンポジタ構造体自身を解放
	for(size_t i = 0; i < MAX_POINTER_DEVICES; i++)
	{
		if(server->mouce_structure.pointers[i] != NULL)
		{
			free(server->mouce_structure.pointers[i]);
			server->mouce_structure.pointers[i] = NULL;
		}
	}
	free(server);
}
