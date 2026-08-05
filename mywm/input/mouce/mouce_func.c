
#include"mywm_core.h"

//マウス移動イベントが発生したときに呼ばれる関数
void newinput_mouce(struct wl_listener *listener, void *data) {
		struct my_pointer *pointer = wl_container_of(listener, pointer, motion);
	struct server *server = pointer->server;
	//dataをローカル変数に渡す
	struct pos_double in_surface_mouce_pos;
	//newinput_mouce関数のの引数dataにはマウスの前フレームとの相対的な座標が入っていて絶対座標ではない
	//絶対座標は、wlr_cursor 構造体が常に最新の絶対値を持っています
	struct wlr_pointer_motion_event *mouce = data;
	//前フレームからのマウスの移動量をカーソルの座標に反映させる
	wlr_cursor_move(server->mouce_structure.cursor,
		&mouce->pointer->base,
		mouce->delta_x,
		mouce->delta_y);

	//もしマウスカーソル下にサーフェスがあった場合ノードとしてこの構造体で受け取る
	struct wlr_scene_node *resizing_scene_node = NULL;
	bool is_in_resize_pos = false;
	enum wlr_edges mouce_resize_img_name = WLR_EDGE_NONE;

	double local_x = 0;
	double local_y = 0;
	server->mouce_structure.surface_move_range = false;

	if(server->now_surface_request_resize == 0){
		resizing_scene_node = wlr_scene_node_at(&server->scene->tree.node,
			server->mouce_structure.cursor->x, server->mouce_structure.cursor->y,
			&in_surface_mouce_pos.absolute_x, &in_surface_mouce_pos.absolute_y);

		//カーソル画像をリサイズ用にするかの変数
		if(resizing_scene_node != NULL){
			//移動リクエストが来ていたら、座標を動かす
			if(server->mouce_structure.surface_move_state){
                                wlr_scene_node_set_position(
                                        &server->moving_view->xdg_toplevel_sarface_tree->node,
                                        server->mouce_structure.cursor->x + 
                                                server->mouce_structure.diff_cursor_surface_pos.diff_cur_surf_pos.pos_x,
                                        server->mouce_structure.cursor->y + 
                                                server->mouce_structure.diff_cursor_surface_pos.diff_cur_surf_pos.pos_y);
			}
			//もしカーソルの下にあるサーフェスとフォーカス中のサーフェスが違ったらフォーカスするサーフェスを更新する
			struct wlr_xdg_toplevel *check_pointer_inside_surface =
				conversion_wlr_scene_node_to_wlr_xdg_toplevel(resizing_scene_node);
			if(check_pointer_inside_surface != NULL){
					struct wlr_xdg_toplevel *resize_xd_toplevel = check_pointer_inside_surface;

				//CSDの影/装飾分のオフセットを除いた、可視ウィンドウ基準の座標に変換する
				struct wlr_box *geo = &resize_xd_toplevel->base->current.geometry;
				local_x = in_surface_mouce_pos.absolute_x - geo->x;
				local_y = in_surface_mouce_pos.absolute_y - geo->y;

				//上端から20〜25pxの帯はタイトルバー相当の「掴んで動かす」領域とみなす
				//my_set_cursor_imgがsurface_move_rangeもtrueにするので、
				// この帯にいる間は下のリサイズ判定が行われない（移動とリサイズの競合防止）
				if(local_y > 20){
					//カーソルイメージを変更
					if(local_y < 25){
						my_set_cursor_img(&server->mouce_structure,
							"hand2");
					}
				}

				//もしマウスカーソル座標とリサイズ開始座標の判定
				//ウィンドウの各辺から内側20pxの帯をリサイズ用の当たり判定にする
				//横方向(w)と縦方向(h)を別々に判定するので、角では両方が立ち
				// あとのh|wで斜めリサイズ（例: 左上）になる
				enum wlr_edges w = 0;
				enum wlr_edges h = 0;
				if(!server->mouce_structure.surface_move_range){
					if(local_x < 20){
						is_in_resize_pos = true;
						w = WLR_EDGE_LEFT;
					}
					else if(local_x > geo->width - 20){
						is_in_resize_pos = true;
						w = WLR_EDGE_RIGHT;
					}

					if(local_y < 20){
						is_in_resize_pos = true;
						h = WLR_EDGE_TOP;
					}
					else if(local_y > geo->height - 20){
						is_in_resize_pos = true;
						h= WLR_EDGE_BOTTOM;
					}
				}
				mouce_resize_img_name = h|w;
				server->mouce_structure.resize_edges = mouce_resize_img_name;
				server->mouce_structure.mouce_inside_resize_pos = is_in_resize_pos;
				if(mouce_resize_img_name != WLR_EDGE_NONE){
					//立っている辺のビットから対応するカーソル画像名（例: "se-resize"）を得る
					server->mouce_structure.now_cursor_name = wlr_xcursor_get_resize_name(mouce_resize_img_name);
					//リサイズ帯の上ではクライアントからポインタを外す
					//外さないとクライアント側が指定したカーソル画像で上書きされてしまう
					wlr_seat_pointer_notify_clear_focus(server->seat);
					//リサイズ開始はボタンイベントではなく、移動イベント中のボタン状態で判定する
					//（ボタンを押したままリサイズ帯に入った場合もそこから開始できるようにするため）
					//ウィンドウ移動中(surface_move)は移動を優先してリサイズを始めない
					if(server->mouce_structure.botton_state->state == WL_POINTER_BUTTON_STATE_PRESSED &&
						!server->mouce_structure.surface_move){

						surface_request_resize(resize_xd_toplevel->base->data,
							server->mouce_structure.resize_edges);
					}
				}
				else{
					wlr_seat_pointer_notify_motion(
						server->seat,
						mouce->time_msec,
						in_surface_mouce_pos.absolute_x,
						in_surface_mouce_pos.absolute_y);

					if(check_pointer_inside_surface->base->surface != 
						server->seat->pointer_state.focused_surface){

						focus_mouce(
						check_pointer_inside_surface->base->surface,
						server->seat,
						server->mouce_structure.cursor,
						&in_surface_mouce_pos);
						focus_keyboard(
						check_pointer_inside_surface->base->surface,
						server->seat);
					}
				}
			}
		}else{
			server->mouce_structure.now_cursor_name = "left_ptr";
			wlr_seat_pointer_notify_clear_focus(server->seat);
		}
	}
	//リサイズ要求が来ていて、リサイズ要求がまだ送られていないときの条件分岐
	//念の為server.resizing=viewが
	// surface_request_resize関数内で正しく判定されているか判定する
	if(server->now_surface_request_resize){
		//サーフェスの位置情報と大きさ
		//プライベート変数に代入することでデータをレジスタに乗せて高速化する
		struct view *temporary_set_resize_view = server->resizing_view;

		//temporary_set_resize_viewを更新する
		temporary_set_resize_view->temporary_set_resize_box.x = server->resizing_view->scene_tree->node.x;
		temporary_set_resize_view->temporary_set_resize_box.y = server->resizing_view->scene_tree->node.y;
		temporary_set_resize_view->temporary_set_resize_box.width = server->resizing_view->surface_box->width;
		temporary_set_resize_view->temporary_set_resize_box.height = server->resizing_view->surface_box->height;

		//リサイズ処理
		uint32_t resize_num = server->mouce_structure.resize_edges;

		int bottom_y;
		int left_x;
		int left_y;
		int new_x;
		int new_y;
		int new_height;
		int new_width;
		if(resize_num & WLR_EDGE_TOP){
				//上辺
				//座標の定義
				//もしクライアントが決めた指定できるサイズ内だだったら
			bottom_y = temporary_set_resize_view->diff_resize_cur_surf_pos.left_bottom_absolute_pos.pos_y;
			new_y =
				server->mouce_structure.cursor->y +
				temporary_set_resize_view->diff_resize_cur_surf_pos.diff_cur_surf_pos.pos_y;
			new_height = bottom_y - new_y;

			if(new_height < temporary_set_resize_view->surface_size_value.min_height){
				new_height = temporary_set_resize_view->surface_size_value.min_height;
			}
			else if(new_height > temporary_set_resize_view->surface_size_value.max_height){
				new_height = temporary_set_resize_view->surface_size_value.max_height;
			}
			new_y = bottom_y - new_height;

					//仮変数を使うことで誤差をなくす
				// 最後に構造体へ代入する
			temporary_set_resize_view->temporary_set_resize_box.height = new_height;
		}
		else if(resize_num & WLR_EDGE_BOTTOM){
			//下辺
			left_y = temporary_set_resize_view->scene_tree->node.y;
			new_y = server->mouce_structure.cursor->y + temporary_set_resize_view->diff_resize_cur_surf_pos.right_bottom_pos.pos_y;
			new_height = new_y - left_y;

			if(new_height < temporary_set_resize_view->surface_size_value.min_height){
				new_height = temporary_set_resize_view->surface_size_value.min_height;
			}
			else if(new_height > temporary_set_resize_view->surface_size_value.max_height){
				new_height = temporary_set_resize_view->surface_size_value.max_height;
			}
			temporary_set_resize_view->temporary_set_resize_box.height = new_height;
		}
		if(resize_num & WLR_EDGE_RIGHT){
				//右辺
			left_x = temporary_set_resize_view->scene_tree->node.x;

			new_x =
				server->mouce_structure.cursor->x +
				temporary_set_resize_view->diff_resize_cur_surf_pos.right_bottom_pos.pos_x;
			new_width = new_x - left_x;

			if(new_width < temporary_set_resize_view->surface_size_value.min_width){
				new_width = temporary_set_resize_view->surface_size_value.min_width;
			}
			else if(new_width > temporary_set_resize_view->surface_size_value.max_width){
				new_width = temporary_set_resize_view->surface_size_value.max_width;
			}
			temporary_set_resize_view->temporary_set_resize_box.x = new_x;
			temporary_set_resize_view->temporary_set_resize_box.width = new_width;
		}
		else if(resize_num & WLR_EDGE_LEFT){
				//左辺
			new_x = server->mouce_structure.cursor->x +
				temporary_set_resize_view->diff_resize_cur_surf_pos.left_bottom_pos.pos_x;
			new_width = temporary_set_resize_view->diff_resize_cur_surf_pos.right_bottom_absolute_pos.pos_x - new_x;

			if(new_width < temporary_set_resize_view->surface_size_value.min_width){
				new_width = temporary_set_resize_view->surface_size_value.min_width;
				new_x =
					temporary_set_resize_view->diff_resize_cur_surf_pos.right_bottom_absolute_pos.pos_x -
					new_width;
			}
			else if(new_width > temporary_set_resize_view->surface_size_value.max_width){
				new_width = temporary_set_resize_view->surface_size_value.max_width;
			}
			temporary_set_resize_view->temporary_set_resize_box.width = new_width;
		}
		server->resizing_view->surface_box->width = temporary_set_resize_view->temporary_set_resize_box.width;
		server->resizing_view->surface_box->height = temporary_set_resize_view->temporary_set_resize_box.height;

		//左辺・上辺を掴んだときは反対側の辺（右辺・下辺）を固定したいので、
		// 記録しておいた反対側の絶対座標から新しい幅・高さを引いて左上座標を求め直す
		//右辺・下辺を掴んだときは左上が動かないので現在のノード座標のまま
		//クライアントがackする前（checkcomit前）に先にノードを動かすことで、
		// ドラッグ中の追従の遅れを減らしている
		int preview_x = server->resizing_view->scene_tree->node.x;
		int preview_y = server->resizing_view->scene_tree->node.y;
		if(server->mouce_structure.resize_edges & WLR_EDGE_LEFT)
		{
			preview_x = server->resizing_view->diff_resize_cur_surf_pos.right_bottom_absolute_pos.pos_x - server->resizing_view->temporary_set_resize_box.width;
		}
		if(server->mouce_structure.resize_edges & WLR_EDGE_TOP)
		{
			preview_y = server->resizing_view->diff_resize_cur_surf_pos.left_bottom_absolute_pos.pos_y - server->resizing_view->temporary_set_resize_box.height;
		}
		wlr_scene_node_set_position(
			&server->resizing_view->scene_tree->node,
			preview_x,
			preview_y);

		  //サーフェスに設定後のウィンドウサイズ情報を送る
		server->resizing_view->pending_serial = 
			wlr_xdg_toplevel_set_size(server->resizing_view->toplevel,
				temporary_set_resize_view->temporary_set_resize_box.width,
				temporary_set_resize_view->temporary_set_resize_box.height);
	
		if(server->mouce_structure.surface_move_range && 
			server->mouce_structure.botton_state->button == BTN_LEFT){

			if(server->mouce_structure.botton_state->state == WL_POINTER_BUTTON_STATE_PRESSED){

				if(server->mouce_structure.surface_move == false && resizing_scene_node != NULL){
					struct wlr_xdg_toplevel *move_toplevel =
						conversion_wlr_scene_node_to_wlr_xdg_toplevel(resizing_scene_node);
					if(move_toplevel != NULL){
						struct view *move_view = move_toplevel->base->data;
						server->moving_view = move_view;
						//掴んだ瞬間の「カーソル - ウィンドウ左上」のずれを覚えておく
						//以降はカーソル座標からこのずれを引くことで、
						// ウィンドウの掴んだ点がカーソルに吸い付いたまま移動する
						server->mouce_structure.mouse_move_local_abs_x =
							server->mouce_structure.cursor->x - move_view->xdg_toplevel_sarface_tree->node.x;
						server->mouce_structure.mouse_move_local_abs_y =
							server->mouce_structure.cursor->y - move_view->xdg_toplevel_sarface_tree->node.y;
						server->mouce_structure.surface_move = true;
					}
				}
			}
			else if(server->mouce_structure.botton_state->state == 
					WL_POINTER_BUTTON_STATE_RELEASED){

				server->mouce_structure.mouse_move_local_abs_x = 0;
				server->mouce_structure.mouse_move_local_abs_y = 0;
				server->mouce_structure.surface_move = false;
			}

		}
	}
	
	if(server->mouce_structure.surface_move){
		if(server->moving_view != NULL){ 
			int move_surface_pos_x = server->mouce_structure.cursor->x - server->mouce_structure.mouse_move_local_abs_x;
			int move_surface_pos_y = server->mouce_structure.cursor->y - server->mouce_structure.mouse_move_local_abs_y;
			
			wlr_scene_node_set_position(&server->moving_view->xdg_toplevel_sarface_tree->node,
				move_surface_pos_x,move_surface_pos_y);
		}
	}

	//カーソル画像をコンポジタ側で描き換えるのは
	// 「カーソル下にサーフェスがない（＝デスクトップ上）」か「リサイズ帯の上」のときだけ
	//それ以外はクライアントが設定したカーソル画像をそのまま尊重する
	if(resizing_scene_node == NULL || is_in_resize_pos == true){
		wlr_cursor_set_xcursor(
			server->mouce_structure.cursor,
			server->mouce_structure.cursor_mgr,
			server->mouce_structure.now_cursor_name);
	}
}

//マウスのボタンイベントが発生したときに呼ばれる関数
void newinput_moucebotton(struct wl_listener *listener, void *data){
	struct my_pointer *pointer = wl_container_of(listener, pointer, button);
	struct server *server = pointer->server;
	struct wlr_pointer_button_event *button = data;
	//サーバ構造体にマウスのボタンイベントを代入する
	*server->mouce_structure.botton_state = *button;

	if(button->state == WL_POINTER_BUTTON_STATE_PRESSED){
		//リストが空ではなかった場合、最上位のノードを取得する
		if(wl_list_empty(&server->scene->tree.children) == 0){
			struct wlr_scene_node *resize_node_top =
				wlr_scene_node_at(
					&server->scene->tree.node,
					server->mouce_structure.cursor->x,
					server->mouce_structure.cursor->y,
					NULL,
					NULL);
			if(resize_node_top != NULL){
				// ウィンドウの大元（server->scene->tree の直下）まで親ノードを遡る
				while(resize_node_top->parent != NULL &&
					resize_node_top->parent != &server->scene->tree){
					resize_node_top = &resize_node_top->parent->node;
				}
				if(resize_node_top != NULL){
					wlr_scene_node_raise_to_top(resize_node_top);
				}
			}
		}
	}
	else if(button->state == WL_POINTER_BUTTON_STATE_RELEASED)
	{
		server->mouce_structure.surface_move = false;
		server->mouce_structure.surface_move_range = false;
		server->mouce_structure.mouse_move_local_abs_x = 0;
		server->mouce_structure.mouse_move_local_abs_y = 0;


		//ボタンを離すとリサイズ状態を解除する
		if(server->mouce_structure.surface_move_state == 1)
			server->mouce_structure.surface_move_state = 0;

		server->moving_view = NULL;
		server->grabbed_view = NULL; // ドラッグを終了するために grabbed_view をリセット
		//リサイズ開始判定はあるが、終了判定がないのでマウスボタンが戻されたときにリサイズ状態をを更新する
		if(server->now_surface_request_resize)
		{
			//リサイズ終了
			wlr_xdg_toplevel_set_resizing(server->resizing_view->toplevel, false);
			server->now_surface_request_resize = 0;
			server->resizing_view->pending_serial = 0;
		}
	}

	//もしフォーカス中のクライアントがなければしない
	if(server->seat->pointer_state.focused_client != NULL)
	{
		//マウスのボタンイベントをクライアントに転送する関数
		wlr_seat_pointer_notify_button(
			server->seat,
			button->time_msec,
			button->button,
			button->state);
	}
}

//クライアントがカーソルイメージを変更したいときに実行される関数
void get_cliant_cursor_image(struct wl_listener *listener, void *data) {
	struct mouce_structure_mem_mgr *mouce_structure =
		wl_container_of(listener, mouce_structure, set_cliant_cursor_image_listener);
	struct server *server = mouce_structure->server;
	struct wlr_seat_pointer_request_set_cursor_event *set_cursor_data = data;
	//カーソル画像の変更要求は、フォーカス中のクライアント以外からは受け付けない
	//（裏のアプリが勝手にカーソルを書き換えるのを防ぐ）
	//またリサイズ帯の上ではコンポジタ側のリサイズカーソルを優先するため却下する
	if(server->seat->pointer_state.focused_client != set_cursor_data->seat_client ||
		server->mouce_structure.mouce_inside_resize_pos == true ||
		set_cursor_data == NULL)
	{
		return;
	}

	wlr_cursor_set_surface(
		server->mouce_structure.cursor,
		set_cursor_data->surface,
		set_cursor_data->hotspot_x,
		set_cursor_data->hotspot_y);
}

//マウススクロール関数
void mouse_scroll_func(struct wl_listener *listener, void *data) {
	struct my_pointer *pointer =
		wl_container_of(listener, pointer, mouce_scroll_listener);
	struct server *server = pointer->server;
	struct wlr_pointer_axis_event *axis_event = data;
	double axis_delta_value = check_axis_delta_value(1);
	wlr_seat_pointer_notify_axis(
		server->seat,
		axis_event->time_msec,
		axis_event->orientation,
		axis_event->delta,
		axis_event->delta_discrete,
		axis_event->source,
		axis_event->relative_direction);
	//スクロール更新通知
	wlr_seat_pointer_notify_frame(server->seat);
}

//スクロール量変数チェック
double check_axis_delta_value(int8_t scroll_device) {
	//仮で1を返しているが、後々設定機能を実装する場合マウススクロース量
	// パラーメータの値をこの関数の返り値にすれば反映されるようにする
	return 1;
}


//カーソル画像を差し替える関数
//この関数はタイトルバー帯に入ったときに呼ばれる前提なので、
// 画像の変更と同時にsurface_move_range（移動可能な領域にいる）もtrueにする
void my_set_cursor_img(struct mouce_structure_mem_mgr *mouce_structure, const char *img_name){
	mouce_structure->now_cursor_name = img_name;
	mouce_structure->surface_move_range = true;
	wlr_cursor_set_xcursor(mouce_structure->cursor,
		mouce_structure->cursor_mgr,
		mouce_structure->now_cursor_name);

}