#include "mywm_core.h"

int main(int argc, char *argv[]) {
	// 子プロセスの終了ステータスを破棄し、ゾンビ化を防ぐ
	signal(SIGCHLD, SIG_IGN);
	//サーバ構造体の定義と初期化
	struct server *server = calloc(1, sizeof(struct server));
	server->wallpaper.server = server;
	server->mybar.server = server;
	server->mouce_structure.server = server;

	//ログレベルと出力先を指定する関数
	wlr_log_init(WLR_INFO, NULL);
	// 最初に初期化
	wl_list_init(&server->new_input.link);
	wl_list_init(&server->new_output.link);
	wl_list_init(&server->frame.link);
	wl_list_init(&server->key.link);
	wl_list_init(&server->new_xdg_toplevel.link);
	wl_list_init(&server->views);
	wl_list_init(&server->key_modifier.link);

	//waylandサーバ(display)構造体の定義と初期化
	server->display = wl_display_create();
	server->resizing_view = NULL;
	server->now_surface_request_resize = 0;
	server->moving_view = NULL;
	server->mouce_structure.botton_state = calloc(1, sizeof(struct wlr_pointer_button_event));
	server->mouce_structure.cursor_scale = 40;

	//waylandサーバ構造体からloopのメンバ部分だけ抜き取り
	// 構造体としてloopを定義する
	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	//バックエンド生成
	server->backend = wlr_backend_autocreate(loop, NULL);
	//取得したバックエンドから適切なレンダラーを生成する
	server->renderer = wlr_renderer_autocreate(server->backend);
	//Wayland ディスプレイ (wl_display) にレンダラーを登録する
	wlr_renderer_init_wl_display(server->renderer, server->display);
	//allocater（フレームバッファ）の初期化
	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	//アプリたちの要求を受け付け、管理するための窓口（帳簿）であるコンポジタを作成する
	server->compositor = wlr_compositor_create(server->display, 6, server->renderer);

	//サブコンポジタの作成。サブコンポジタは、クライアントがサブサーフェスを作成するためのインターフェースを提供します。
	//これを呼び出すことで、クライアント（アプリケーション）は一つのウィンドウの中に、
	// 独立して更新・制御できる複数の「子画面（サブサーフェス）」を持てるようになる
	struct wlr_subcompositor *sub_compositor = wlr_subcompositor_create(server->display);
	//データデバイスマネージャの作成。データデバイスマネージャは、
	// クリップボードやドラッグアンドドロップなどのデータ転送機能を提供します。
	wlr_data_device_manager_create(server->display);
	//xdg_shellの作成。xdg_shellは、クライアントがウィンドウを作成・管理するためのプロトコルを提供します。
	server->xdg_shell = wlr_xdg_shell_create(server->display, 1);
	//出力レイアウトの作成。出力レイアウトは、複数の物理モニターを仮想的なキャンバス上に配置するための構造体です。
	server->s_output_struct.output_layout = wlr_output_layout_create(server->display);
	//シーンの作成。シーンは、ウィンドウやレイヤーなどの描画要素を管理するための構造体です。
	server->scene = wlr_scene_create();
	// カーソル画像（NULLならデフォルト）とサイズ（24など）を指定
	server->mouce_structure.cursor_mgr =
		wlr_xcursor_manager_create(NULL, server->mouce_structure.cursor_scale);
	//カーソル画像をロード
	wlr_xcursor_manager_load(server->mouce_structure.cursor_mgr, 1);
	//カーソルの作成。カーソルは、マウスポインタの位置や画像を管理するための構造体です。
	server->mouce_structure.cursor = wlr_cursor_create();
	//カーソルと出力レイアウトを紐付ける関数。これにより、カーソルの位置がどの出力にあるかを管理できるようになる
	wlr_cursor_attach_output_layout(
		server->mouce_structure.cursor,
		server->s_output_struct.output_layout);
	//
	server->wallpaper.wallpaper_shell = wlr_layer_shell_v1_create(server->display, 1);

	const char *socket_name;
	//もしdisplayに接続するソケットが自動で追加できなければ終了する
	if((socket_name = wl_display_add_socket_auto(server->display)) == NULL)
	{
		fprintf(stderr, "socket error\n");
		return 0;
	}
	setenv("WAYLAND_DISPLAY", socket_name, 1);
	function_set(server);//メンバに関数ポインタをセット
	server->seat = wlr_seat_create(server->display, "seat0");
	server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->display);

	//クライアントのマウスカーソルイメージ発火時のリスナーをセット
	wl_signal_add(
		&server->seat->events.request_set_cursor,
		&server->mouce_structure.set_cliant_cursor_image_listener);
	//server->backend->events.new_inputが発火したらserver->new_input.notifyを実行する(関数)
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	//出力デバイスが追加されたらnew_outputの関数を実行する
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	//クライアントが新しいxdg_toplevelウィンドウを要求したときに発火する
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	//レイヤシェル構造体のサーフェス発火時実行する関数の紐ずけ
	wl_signal_add(
		&server->wallpaper.wallpaper_shell->events.new_surface,
		&server->wallpaper.wallpaper_manage_listner.wallpaper_listner);
	//クライアントがCSD(クライアントサイドデコレーションかSSD(サーバーサイドデコレーション)か聞いてくるのでその際に実行する関数
	wl_signal_add(
		&server->xdg_decoration_manager->events.new_toplevel_decoration,
		&server->xdg_decoration_listener);

	//バックエンドのイベント監視ループ開始
	if(!wlr_backend_start(server->backend))
	{
		fprintf(stderr, "Failed to start backend\n");
		return 1;
	}

	//無限ループでクライアントからのイベントを処理する
	wl_display_run(server->display);
	//メモリ解放
	server_destroy(server);
	return 0;
}
