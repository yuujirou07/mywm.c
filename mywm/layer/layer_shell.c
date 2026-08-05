#include "mywm_core.h"

bool new_wallpaper_criant_create(struct server *server) {
	pid_t pid = fork();
	if(pid == 0)
	{
		char *const args[] = {
			"swaybg",           // 第1引数: 実行するバイナリ名
			"-i", "wp.jpg",    // 第2,3引数: 画像ファイルへのパス
			"-m", "fill",      // 第4,5引数: 画像の配置モード
			NULL                // 第6引数: 配列の終端を示す
		};
		// 第1引数: 検索して実行するファイル名 ("swaybg")
		// 第2引数: プログラムに渡す引数の配列
		execvp(args[0], args);
		exit(EXIT_FAILURE);
		return 0;
	}
	if(pid > 0)
	{//親の処理
		return 1;
	}

	server->debug = fopen("debug_error_code.txt", "r");
	fputs("fork error", server->debug);
	fclose(server->debug);
	return 0;
}

//ステータスバー(mybar)を子プロセスとして起動する関数
bool new_mybar_start(struct server *server) {
	pid_t pid = fork();
	if(pid == 0)
	{
		//mybarはmywm本体と同じディレクトリに置かれる前提なので、
		// 自分自身の実行ファイルパス(/proc/self/exe)からディレクトリを取り出して探す
		//こうするとビルドディレクトリを移動しても、そのままのパスで起動できる
		char self_path[PATH_MAX];
		//readlinkは終端の'\0'を書かないので、1バイト残して読み自分で終端する
		ssize_t self_len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
		if(self_len > 0)
		{
			self_path[self_len] = '\0';
			char *last_slash = strrchr(self_path, '/');
			if(last_slash != NULL)
			{
				//最後の'/'を終端にしてファイル名を切り捨て、ディレクトリ部分だけにする
				*last_slash = '\0';
				char bar_path[PATH_MAX];
				size_t dir_len = strlen(self_path);
				//sizeof("/mybar")は'\0'込みの長さなので、これで終端まで収まるか確認できる
				if(dir_len + sizeof("/mybar") <= sizeof(bar_path))
				{
					memcpy(bar_path, self_path, dir_len);
					memcpy(bar_path + dir_len, "/mybar", sizeof("/mybar"));
					char *const build_args[] = {
						bar_path,
						NULL
					};
					execv(build_args[0], build_args);
				}
			}
		}

		//execvは成功すると戻ってこないので、ここに来るのは上の起動に失敗したときだけ
		//その場合の保険として開発環境の固定パスから起動を試みる
		char *const args[] = {
			"/home/yuujirou07/vscode_proj/mywm_proj/mybar_proj/mybar",// 第1引数: 実行するバイナリ名
			NULL
		};
		execvp(args[0], args);
		exit(EXIT_FAILURE);
		return 0;
	}
	if(pid > 0)
	{//親の処理
		return 1;
	}

	server->debug = fopen("debug_error_code.txt", "r");
	fputs("fork error", server->debug);
	fclose(server->debug);
	return 0;
}

//layer_shellのサーフェスが要求されたときに実行する関数
//クライアントが希望するレイヤー(背景・最前面など)ごとに置き場所を変える
//このWMではBACKGROUNDを壁紙(swaybg)、TOPをステータスバー(mybar)として扱う
void layer_shell_create(struct wl_listener *listener, void *data) {
	struct wallpaper_listner *listeners =
		wl_container_of(listener, listeners, wallpaper_listner);
	struct wallpaper_manage *wallpaper =
		wl_container_of(listeners, wallpaper, wallpaper_manage_listner);
	struct server *server = wallpaper->server;
	struct wlr_layer_surface_v1 *layer_surface = data;

	switch(layer_surface->pending.layer)
	{
		case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		{
			server->wallpaper.surface_v1 = layer_surface;
			//server->sceneに壁紙用のツリーを生成する
			struct wlr_scene_tree *wallpaper_tree =
				wlr_scene_tree_create(&server->scene->tree);
			//ツリーにserver->sceneのlayer_surface_v1を作って紐ずける
			server->wallpaper.scene_layer_surface_v1 =
				wlr_scene_layer_surface_v1_create(
					wallpaper_tree,
					server->wallpaper.surface_v1);
			struct wlr_box full_area;
			wlr_output_layout_get_box(
				server->s_output_struct.output_layout,
				server->s_output_struct.output,
				&full_area);
			//コンポジタが指定した最終的なサイズをクライアント（壁紙やパネルなど）に通知するための関数
			wl_signal_add(
				&server->wallpaper.surface_v1->surface->events.commit,
				&server->wallpaper.wallpaper_manage_listner.commit_listener);
			break;
		}
		case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		{
			break;
		}
		case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		{
			//case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:と同じ仕組み
			server->mybar.surface_v1 = layer_surface;
			struct wlr_scene_tree *bar = wlr_scene_tree_create(&server->scene->tree);
			server->mybar.scene_layer_surface_v1 =
				wlr_scene_layer_surface_v1_create(bar, server->mybar.surface_v1);
			struct wlr_box bar_area;
			wlr_output_layout_get_box(
				server->s_output_struct.output_layout,
				server->s_output_struct.output,
				&bar_area);
			wl_signal_add(
				&server->mybar.surface_v1->surface->events.commit,
				&server->mybar.mybar_listner.commit_listener);
			break;
		}
		case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		{
			break;
		}
	}
}

//壁紙の表示設定をし描画可能な状態する関数
void wallpaper_commit(struct wl_listener *listener, void *data) {
	struct wallpaper_listner *listeners =
		wl_container_of(listener, listeners, commit_listener);
	struct wallpaper_manage *wallpaper =
		wl_container_of(listeners, wallpaper, wallpaper_manage_listner);
	struct server *server = wallpaper->server;
	struct wlr_box full_area;
	//モニターの横縦取得
	wlr_output_layout_get_box(
		server->s_output_struct.output_layout,
		server->s_output_struct.output,
		&full_area);
	struct wlr_box usable_area = full_area;
	wlr_scene_layer_surface_v1_configure(
		server->wallpaper.scene_layer_surface_v1,
		&full_area,
		&usable_area);
}

void request_mybar(struct wl_listener *listener, void *data) {
	struct wallpaper_listner *listeners =
		wl_container_of(listener, listeners, commit_listener);
	struct mybar_data *mybar = wl_container_of(listeners, mybar, mybar_listner);
	struct server *server = mybar->server;
	struct wlr_box full_area;
	wlr_output_layout_get_box(
		server->s_output_struct.output_layout,
		server->s_output_struct.output,
		&full_area);
	struct wlr_box usable_area = full_area;
	wlr_scene_layer_surface_v1_configure(
		server->mybar.scene_layer_surface_v1,
		&full_area,
		&usable_area);
}
