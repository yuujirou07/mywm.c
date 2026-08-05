#include "mywm_core.h"

//新しい出力デバイスが接続されたら実行する
void new_output(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_output);
	//生成されたoutput構造体にdataを代入する
	struct wlr_output *output = data;
	server->s_output_struct.output = output;
	//outputに構造体を紐づけて描画可能にする
	wlr_output_init_render(output, server->allocator, server->renderer);
	//取得したoutput（物理モニター）構造体とserver->scene構造体を紐ずける
	server->scene_output = wlr_scene_output_create(server->scene, output);

	// 状態（stat）構造体を定義する
	struct wlr_output_state state;
	//stateを初期化する
	wlr_output_state_init(&state);
	// 画面を有効化
	wlr_output_state_set_enabled(&state, true);
	//output->modeにデータが入っているかの条件分岐
	if(!wl_list_empty(&output->modes))
	{
		//output->events.frameが発火したらoutput_frame関数を実行する
		wl_signal_add(&output->events.frame, &server->frame);
		//modeに推奨されているモードを代入する
		struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
		//もしmodeに推奨設定があるなら
		if(mode)
		{
			//stateに推奨設定をいれる
			wlr_output_state_set_mode(&state, mode);
		}
	}

	//swaybgへの壁紙要求
	server->wallpaper.wallpaper_create_sucsess = new_wallpaper_criant_create(server);
	if(!server->mybar.mybar_started)
	{
		server->mybar.mybar_started = new_mybar_start(server);
	}
	//出力レイアウトにoutputを追加する関数。これにより、出力レイアウトはこのoutputを管理できるようになる
	wlr_output_layout_add_auto(server->s_output_struct.output_layout, output);
	//sateの設定をoutputに反映させる
	wlr_output_commit_state(output, &state);
	//state構造体はもう使わないのでリソースを解放させる
	wlr_output_state_finish(&state);
}

//描画関数
void output_frame(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, frame);
	wlr_scene_output_commit(server->scene_output, NULL);
	//現在時刻の取得
	struct timespec now;
	// 描画が終わった時刻をアプリ（Waylandクライアント）に通知する
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(server->scene_output, &now);
}
