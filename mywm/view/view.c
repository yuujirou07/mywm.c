#include "mywm_core.h"

//新しいウィンドウが要求された時に呼ばれる関数
void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	//新しくツリーを生成する
	struct wlr_scene_tree *xdg_toplevel_sarface_tree =
		wlr_scene_tree_create(&server->scene->tree);
	//後で描画優先順位管理などでtoplevel構造体を使うため紐ずける
	xdg_toplevel_sarface_tree->node.data = toplevel;
	//xdg_surfaceをsceneのツリーに紐ずける
	struct wlr_scene_tree *new_wlr_scene_tree =
		wlr_scene_xdg_surface_create(xdg_toplevel_sarface_tree, toplevel->base);
	//vに要求されたトップレベル構造体の代入とwlr_scene_tree構造体の描画優先順位ロジックをする関数
	struct view *view = view_create(server, toplevel, new_wlr_scene_tree);
	view->xdg_toplevel_sarface_tree = xdg_toplevel_sarface_tree;
	//親ツリーの座標を変更（子である scene_surface も自動的に移動する）
	wlr_scene_node_set_position(&xdg_toplevel_sarface_tree->node, 100, 200);

	// マップ（表示開始）時のハンドラを登録
	// ここでサイズ指定などを行うように変更する
	view->map.notify = displaypush;
	wl_signal_add(&toplevel->base->surface->events.map, &view->map);
	// 3. アンマップ（非表示）時のハンドラ
	view->unmap.notify = displaypull;
	wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = checkcomit;
	wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
	//サーフェスを移動するときに使うリスナー
	view->request_move_surface.notify = request_surface_move;
	wl_signal_add(&toplevel->events.request_move, &view->request_move_surface);
	view->toplevel_destroy_listener.notify = toplevel_destroy;
	wl_signal_add(&view->toplevel->events.destroy, &view->toplevel_destroy_listener);
	//ウィンドウの初期化後にinitialized = trueにしないとだめ
	view->toplevel->base->initialized = true;
}

//ウィンドウの描画要求が来たときに呼ばれる関数
void checkcomit(struct wl_listener *listener, void *data) {
	// リスナーから view 構造体を取り出す
	struct view *view = wl_container_of(listener, view, commit);
	struct server *server = view->server;
	if(view->toplevel->base->initial_commit){
		wlr_xdg_toplevel_set_size(view->toplevel, 1000, 1000);
		wlr_xdg_surface_schedule_configure(view->toplevel->base);
		printf("window requested\n");
	}

	//もしリサイズ要求が完了したら実際にoutput_frameで描画に使われているウィンドウ情報に代入する
	//wlr_xdg_toplevel_set_sizeは要求を送るだけで即座には反映されず、
	// クライアントがその要求をackしてcommitして初めて新しいサイズの絵が届く
	//そこで返ってきたserialがこちらの要求番号(pending_serial)以上になった時点を
	// 「要求したサイズが実際に反映された」とみなし、左上座標を確定させる
	//この待ち合わせをしないと、ドラッグ中に古いサイズの絵と新しい座標がずれて表示がちらつく
	if(view->pending_serial != 0 &&
		view->toplevel->base->current.configure_serial >= view->pending_serial){
		if(server->now_surface_request_resize && server->resizing_view == view){
			int preview_x = view->scene_tree->node.x;
			int preview_y = view->scene_tree->node.y;
			if(server->mouce_structure.resize_edges & WLR_EDGE_LEFT){
				preview_x =
					view->diff_resize_cur_surf_pos.right_bottom_absolute_pos.pos_x -
					view->surface_box->width;
			}
			if(server->mouce_structure.resize_edges & WLR_EDGE_TOP){
				preview_y =
					view->diff_resize_cur_surf_pos.left_bottom_absolute_pos.pos_y -
					view->surface_box->height;
			}
			wlr_scene_node_set_position(&view->scene_tree->node, preview_x, preview_y);
		}
		view->pending_serial = 0;
	}
	wlr_output_schedule_frame(server->s_output_struct.output);
}

//ウィンドウの最初の表示準備完了時に実行する関数
void displaypush(struct wl_listener *listener, void *data) {
	// リスナーから view 構造体を逆算して取り出す
	struct view *view = wl_container_of(listener, view, map);
	struct server *server = view->server;
	//取り出したview構造体からsurface構造体を取り出す
	struct wlr_surface *surface = view->toplevel->base->surface;
	view->surface_box = &view->toplevel->base->current.geometry;
	// seat のキーボードフォーカスをこのサーフェスに設定する
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);

	if(keyboard)
	{
		wlr_seat_keyboard_notify_enter(
			server->seat,
			surface, // フォーカスを当てるサーフェス
			keyboard->keycodes, // 現在押されているキーの配列
			keyboard->num_keycodes, // 押されているキーの数
			&keyboard->modifiers); // Shift/Ctrl などの修飾キーの状態
	}
	printf("window mapped and configured!\n");
}

// windowの消去要求時に呼ばれる関数のプロトタイプ宣言
void displaypull(struct wl_listener *listener, void *data) {
}

//サーフェスにキーボードの状態を転送させる関数
//第一引数にフォーカスさせたいサーフェス、第二引数にwlr_seat構造体を。
//もし第一引数とフォーカスされてるサーフェスが同じなら何もしない
void focus_keyboard(struct wlr_surface *focus_surface, struct wlr_seat *focus_seat) {
	// seat からキーボード構造体をを抜き出す
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(focus_seat);
	//すでにフォーカスされてるウィンドウ
	struct wlr_surface *focused_surface = focus_seat->keyboard_state.focused_surface;
	//フォーカスするサーフェスがすでにフォーカスされてたら戻る
	if(focus_surface == focused_surface)
	{
		return;
	}
	if(keyboard)
	{
		//マウスクリックしたときにマウスが乗っているウィンドウにキーボードのフォーカスを当てる関数
		wlr_seat_keyboard_notify_enter(
			focus_seat,
			focus_surface, // フォーカスを当てるサーフェス
			keyboard->keycodes, // 現在押されているキーの配列
			keyboard->num_keycodes, // 押されているキーの数
			&keyboard->modifiers); // Shift/Ctrl などの修飾キーの状態
	}
}

//サーフェスにマウスが入ったことを伝える関数
void focus_mouce(struct wlr_surface *focus_surface, struct wlr_seat *focus_seat,
	struct wlr_cursor *cursor, struct pos_double *focus_surface_pos) {
	//フォーカスされているサーフェスの取得
	struct wlr_surface *focused_surface = focus_seat->pointer_state.focused_surface;
	/*もしすでにフォーカスされているウィンドウと
	これからするウィンドウが同じなら戻る*/
	if(focus_surface == focused_surface)
	{
		return;
	}
	/*マウスカーソルがサーフェスに入ったことをクライアントに通知する。*/
	wlr_seat_pointer_notify_enter(
		focus_seat,
		focus_surface,
		focus_surface_pos->absolute_x,
		focus_surface_pos->absolute_y);
}

//ウィンドウの縁をクリックした時に実行される関数
void surface_request_resize(struct view *resize_view, uint32_t data) {
	struct server *server = resize_view->server;
	//リサイズ開始のbool値更新
	server->now_surface_request_resize = 1;
	uint32_t resize_data = data;
	server->resizing_view = resize_view;
	printf("surface_resize_requested\n");
	//サーフェスの大きさを設定
	put_surface_size_value(server->resizing_view);

	//リサイズ中は掴んだ辺だけを動かし、反対側の辺は固定したい
	//そのために掴んだ瞬間の「カーソル座標と辺との差」を記録しておき、
	// ドラッグ中はこの差を保ったまま新しいサイズを計算する
	//あわせて下の方で反対側の辺の絶対座標も控えておく（固定する基準になる）
	if(resize_data & WLR_EDGE_TOP)
	{
		//上辺とマウスカーソルとの差
		server->resizing_view->diff_resize_cur_surf_pos.diff_cur_surf_pos.pos_x =
			server->resizing_view->scene_tree->node.x -
			server->mouce_structure.cursor->x;
		server->resizing_view->diff_resize_cur_surf_pos.diff_cur_surf_pos.pos_y =
			server->resizing_view->scene_tree->node.y -
			server->mouce_structure.cursor->y;
	}
	else if(resize_data & WLR_EDGE_BOTTOM)
	{
		//下辺のマウスカーソルとの差
		server->resizing_view->diff_resize_cur_surf_pos.right_bottom_pos.pos_y =
			server->resizing_view->scene_tree->node.y +
			server->resizing_view->surface_box->height -
			server->mouce_structure.cursor->y;
	}

	if(resize_data & WLR_EDGE_RIGHT)
	{
		//右辺とマウスカーソルとの差
		server->resizing_view->diff_resize_cur_surf_pos.right_bottom_pos.pos_x =
			server->resizing_view->scene_tree->node.x +
			server->resizing_view->surface_box->width -
			server->mouce_structure.cursor->x;
	}
	else if(resize_data & WLR_EDGE_LEFT)
	{
		//左辺とマウスカーソルとの差
		server->resizing_view->diff_resize_cur_surf_pos.left_bottom_pos.pos_x =
			server->resizing_view->scene_tree->node.x -
			server->mouce_structure.cursor->x;
	}

	//右辺の絶対座標（左辺を掴んだときに固定する基準）
	server->resizing_view->diff_resize_cur_surf_pos.right_bottom_absolute_pos.pos_x =
		server->resizing_view->scene_tree->node.x +
		server->resizing_view->surface_box->width;
	//下辺の絶対座標（上辺を掴んだときに固定する基準）
	server->resizing_view->diff_resize_cur_surf_pos.left_bottom_absolute_pos.pos_y =
		server->resizing_view->scene_tree->node.y +
		server->resizing_view->surface_box->height;
	server->resizing_view->diff_resize_cur_surf_pos.left_bottom_absolute_pos.pos_x =
		server->resizing_view->scene_tree->node.x;
	wlr_xdg_toplevel_set_resizing(server->resizing_view->toplevel, true);
}

//wlr_scene_node構造体からwlr_toplevel構造体を抜き出す関数抜き出せない場合NULLを返す
struct wlr_xdg_toplevel *conversion_wlr_scene_node_to_wlr_xdg_toplevel(
	struct wlr_scene_node *same_pos_mouce_scene_node) {
	if(!same_pos_mouce_scene_node)
	{
		return NULL;
	}

	// data (toplevel) が見つかるまで親ツリーを上に辿る
	while(!same_pos_mouce_scene_node->data)
	{
		if(!same_pos_mouce_scene_node->parent)
		{
			return NULL;
		}
		same_pos_mouce_scene_node = &same_pos_mouce_scene_node->parent->node;
	}
	// 見つかった toplevel を返す
	return same_pos_mouce_scene_node->data;
}

//ウィンドウ要求時にview構造体にデータを配置する関数
struct view *view_create(struct server *server, struct wlr_xdg_toplevel *xdg_toplevel,
	struct wlr_scene_tree *new_scene_tree) {
	struct view *view = calloc(1, sizeof(struct view));
	view->server = server;
	view->toplevel = xdg_toplevel;
	wl_list_insert(&server->views, &view->list); //サーフェスリストの先頭に置く
	view->surface_box = malloc(sizeof(struct wlr_box));
	view->scene_tree = new_scene_tree;
	xdg_toplevel->base->data = (void *)view;
	return view;
}

//タイトルバーを掴んだときに実行する関数
void request_surface_move(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, request_move_surface);
	struct server *server = view->server;
	server->mouce_structure.surface_move_state = 1;
	server->moving_view = view;
	//タイトルバーを掴んだ瞬間のマウス座標とサーフェス座標の差を保持し、
	// マウス座標とサーフェス座標の差を下にサーフェスの座標を決定する
	server->mouce_structure.diff_cursor_surface_pos.diff_cur_surf_pos.pos_x =
		view->xdg_toplevel_sarface_tree->node.x - server->mouce_structure.cursor->x;
	server->mouce_structure.diff_cursor_surface_pos.diff_cur_surf_pos.pos_y =
		view->xdg_toplevel_sarface_tree->node.y - server->mouce_structure.cursor->y;
}

//サーフェスの規定サイズを設定します
void put_surface_size_value(struct view *put_size_view) {
	struct server *server = put_size_view->server;
	struct wlr_box sr_box;
	wlr_output_layout_get_box(
		server->s_output_struct.output_layout,
		server->s_output_struct.output,
		&sr_box);

	//縦の大きさ制限設定
	//後からモニターの大きさを取得してそれにそろえる
	if(put_size_view->toplevel->current.max_height <= 0)
	{
		put_size_view->surface_size_value.max_height = sr_box.height;
	}
	else
	{
		put_size_view->surface_size_value.max_height =
			put_size_view->toplevel->current.max_height;
	}

	if(put_size_view->toplevel->current.min_height <= 0)
	{
		put_size_view->surface_size_value.min_height = 100;
	}
	else
	{
		put_size_view->surface_size_value.min_height =
			put_size_view->toplevel->current.min_height;
	}

	if(put_size_view->toplevel->current.max_width <= 0)
	{
		put_size_view->surface_size_value.max_width = sr_box.width;
	}
	else
	{
		put_size_view->surface_size_value.max_width =
			put_size_view->toplevel->current.max_width;
	}

	if(put_size_view->toplevel->current.min_width <= 0)
	{
		put_size_view->surface_size_value.min_width = 300;
	}
	else
	{
		put_size_view->surface_size_value.min_width =
			put_size_view->toplevel->current.min_width;
	}
}

//新しいデコレーションが作られたときの処理
void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, xdg_decoration_listener);
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	struct my_decoration *deco = calloc(1, sizeof(*deco));
	deco->wlr_deco = wlr_deco;
	deco->request_mode.notify = deco_request_mode;
	wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);
	// destroy イベント（破棄）をリッスンする
	deco->destroy.notify = deco_destroy;
	wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
	// 初回の返事をする
	wlr_xdg_toplevel_decoration_v1_set_mode(
		wlr_deco,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

//起動後に確認してくるモードの応答
void deco_request_mode(struct wl_listener *listener, void *data) {
	struct my_decoration *deco = wl_container_of(listener, deco, request_mode);
	// 「コンポジタ側で枠を描くよ（SSD）」と再度アプリに返事をする
	wlr_xdg_toplevel_decoration_v1_set_mode(
		deco->wlr_deco,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

//クライアント終了時に呼ばれるデコレーションの消去関数
void deco_destroy(struct wl_listener *listener, void *data) {
	struct my_decoration *deco = wl_container_of(listener, deco, destroy);
	wl_list_remove(&deco->request_mode.link);
	wl_list_remove(&deco->destroy.link);
	free(deco);
}

//toplevel構造体の画面推移時などに破棄するときに発火する関数
void toplevel_destroy(struct wl_listener *listener, void *data) {
	struct view *view = wl_container_of(listener, view, toplevel_destroy_listener);
	wl_list_remove(&view->toplevel_destroy_listener.link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->request_move_surface.link);
	wl_list_remove(&view->commit.link);
	free(view);
}
