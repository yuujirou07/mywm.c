#ifndef MYWM_CORE_H
#define MYWM_CORE_H

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr/util/box.h"
#include "wlr/util/edges.h"
#include "wlr/xcursor.h"
#include <assert.h>
#include <bits/types/locale_t.h>
#include <drm_fourcc.h>
#include <getopt.h>
#include <gdk-pixbuf-2.0/gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <libinput.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_POINTER_DEVICES 10

struct server;

struct pos_double
{
	double absolute_x;
	double absolute_y;
};

struct pos_int
{
	int pos_x;
	int pos_y;
};

struct my_decoration
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

/*
ドラッグ開始時の基準値をまとめた構造体
「差」のメンバはドラッグ中もその差を保つために使い、
「絶対座標」のメンバは掴んでいない側の辺を固定するための基準として使う
*/
struct diff_pos
{
	struct pos_int diff_cur_surf_pos;//ウィンドウ左上 - カーソル の差(移動時と上辺リサイズ時に使う)
	struct pos_int left_bottom_pos;//pos_x: 左辺X - カーソルX の差
	struct pos_int right_bottom_pos;//pos_x: 右辺X - カーソルX の差 / pos_y: 下辺Y - カーソルY の差
	struct pos_int left_bottom_absolute_pos;//pos_x: 左辺Xの絶対座標 / pos_y: 下辺Yの絶対座標
	struct pos_int right_bottom_absolute_pos;//pos_x: 右辺Xの絶対座標
};

//マウスの構造体
struct my_pointer
{
	struct server *server;
	struct wlr_input_device *device;
	struct wl_listener motion;
	struct wl_listener button;
	struct wl_listener mouce_scroll_listener;
};

//ウィンドウの構造体
struct view
{
	struct server *server;
	struct wl_list list;
	struct wlr_xdg_toplevel *toplevel;
	struct wl_listener map;     // アプリのwindowの描画要求時に呼ぶリスナー
	struct wl_listener unmap;   // アプリが消去要求を出したときのためのリスナー
	struct wl_listener destroy; // アプリが閉じる要求を出したときのためのリスナー
	struct wl_listener commit;  //初期化が完了しているかのリスナー
	struct wl_listener request_resize_listener;//クライアントがリサイズモードに入ったときに発生するシグナルを検知するリスナー
	struct wl_listener request_move_surface;//サーフェス移動監視用リスナー
	struct wl_listener xdg_decoration;
	struct wl_listener toplevel_destroy_listener;
	struct wlr_box *surface_box;//サーフェスの座標と大きさのデータが入る構造体
	struct wlr_box temporary_set_resize_box;//リサイズ用の仮のwlr_box構造体
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_rect *sceme_rect;
	struct wlr_scene_tree *xdg_toplevel_sarface_tree;//各サーフェスを紐づけるツリー
	struct diff_pos diff_resize_cur_surf_pos;//リサイズ中のサーフェス座標とマウスカーソルの絶対座標さをいれる
	struct
	{//サーフェスのサイズの規定
		int max_height;
		int min_height;
		int max_width;
		int min_width;
	} surface_size_value;
	uint32_t pending_serial;    //set_sizeが返したシリアル番号で、フレームのずれによるウィンドウ画面のサイズ変更時のがたつきを直すための同期に使う
};

struct wallpaper_listner
{
	struct wl_listener wallpaper_listner;//wlr_layer_shell_v1のクライアントシグナルのリスナー
	struct wl_listener commit_listener;  //描画要求リスナー
};

//壁紙管理構造体
struct wallpaper_manage
{
	struct server *server;
	struct wlr_layer_surface_v1 *surface_v1;
	struct wlr_layer_shell_v1 *wallpaper_shell;//レイヤ構造体
	struct wlr_scene_layer_surface_v1 *scene_layer_surface_v1;
	struct wallpaper_listner wallpaper_manage_listner;
	//壁紙の生成関数使用制限
	bool wallpaper_create_sucsess;
};

struct server_output
{
	struct wlr_output_layout *output_layout;
	struct wlr_output *output;      // 出力デバイスのリスト
};

struct mybar_data
{
	struct server *server;
	struct wlr_layer_surface_v1 *surface_v1;
	struct wlr_layer_shell_v1 *wallpaper_shell;//レイヤ構造体
	struct wlr_scene_layer_surface_v1 *scene_layer_surface_v1;
	struct wallpaper_listner mybar_listner;
	bool mybar_started;
};

//マウス関係構造体
struct mouce_structure_mem_mgr
{
	struct server *server;
	struct wl_listener set_cliant_cursor_image_listener;
	struct wlr_pointer pointer;     //ハードウェアカーソル
	struct wlr_cursor *cursor;      //論理カーソル
	struct wlr_xcursor *xcursor; //wlr_xcursor_manager構造体から取り出される実際表示されるマウス画像などの見た目の部分を管理する構体
	struct wlr_xcursor_manager *cursor_mgr;//マウスカーソル状態管理構造体この構造体に直接書き込まない
	struct pos_double cursor_risize_basis_pos;//リサイズ時の基準となるカーソル座標を保持する
	struct diff_pos diff_cursor_surface_pos;//タイトルバーをドラッグするときのサーフェス移動時のマウス座標とサーフェス座標の差をいれる構造体
	struct wlr_pointer_button_event *botton_state;//マウスのボタン状態を保持する構造体
	//ハードウェアカーソルの許容個数
	struct my_pointer *pointers[MAX_POINTER_DEVICES];
	//マウスの個数を数える変数
	size_t pointer_count;
	enum wlr_edges resize_edges;
	const char *now_cursor_name;//現在のマウスカーソル画像名
	int cursor_scale; //マウスカーソルの大きさ
	bool surface_move_state;//クライアントからの移動要求(request_move)が来ると1になる
	bool mouce_inside_resize_pos;//カーソルがリサイズ帯の上にいるとtrue(クライアントのカーソル変更要求を却下する判定に使う)
	bool surface_move_range;//マウスがサーフェスを動かすときの座標内にいるときにtrueになる
	bool surface_move;// surface_move_rangeがtrueの時に左クリックされたときにtrueになる
	int mouse_move_local_abs_x;
	int mouse_move_local_abs_y;
};

struct server
{
	struct wlr_backend *backend;    //バックエンド構造体の定義
	struct wl_listener new_input;   //デバイス検知判定用のリスナーを定義
	struct wlr_keyboard *keyboard;  //キーイベントの構造体を定義
	struct wl_listener key;         //キーイベント時のリスナーの定義
	struct server_output s_output_struct;
	struct wl_listener new_output;  //アウトプットリスナーの定義
	struct wl_display *display;    //サーバ本体の構造体の定義
	struct wlr_renderer *renderer;  //レンダラーの定義（描画バッファ構造体）
	struct wlr_allocator *allocator;//GPUメモリ確保（確保した領域にレンダラーで描画バッファを保持することができる）
	struct wl_listener frame;       //フレームリスナー
	struct wlr_texture *taskbar_tex;//タスクバーテクスチャ(vramに移すためにtaskbar_pixを生のピクセルデータに変換したものをいれる変数)
	struct wlr_xdg_shell *xdg_shell;//xdg_shell: wlrootsがそのリクエストを受け取り、struct wlr_xdg_toplevel という「型」のデータを作る
	struct wl_list views;           // 表示するウィンドウ（view）のリスト
	struct wl_listener new_xdg_toplevel;// 新しいウィンドウが作られた時のリスナー
	struct wlr_compositor *compositor; // コンポジタの構造体
	struct wlr_seat *seat;          // シートの構造体
	struct wlr_decoration_manager *decoration_manager; // ウィンドウの装飾を管理する構造体
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener xdg_decoration_listener;
	struct view *grabbed_view;      // ドラッグ中のウィンドウを保持する構造体
	struct view *resizing_view;     //リサイズ中のウィンドウを保持する構造体
	struct wl_listener key_modifier;// キーの修飾キーが変化したときのリスナー
	struct wl_listener new_wlr_layer_shell_v1_listener;//layer_shellのクライアント要求を監視するリスナー
	struct wlr_pointer_button_event *button; //マウスのボタンイベントを保持する構造体
	struct view *focus_view;        // フォーカス中のウィンドウを保持する構造体
	struct wlr_scene *scene;        //シーン構造
	struct wlr_scene_output *scene_output; //シーン用のアウトプットデバイスなどの情報の構造体
	struct wallpaper_manage wallpaper;//壁紙管理構造体
	struct mouce_structure_mem_mgr mouce_structure;
	struct wlr_scene_node_ranking_mgr *wlr_scene_tree_node_rankin_mgr;
	struct mybar_data mybar;
	struct view *moving_view;//サーフェス座標変更時のview構造体を保持する関数
	bool now_surface_request_resize;//リサイズ中か判断する(1でリサイズ中)
	bool bool_resizing_node; //マウスカーソルがサーフェス内にいるか
	//wiinキーとenterキーの同時押し判定
	bool super_pressed;
	FILE *debug;
	struct
	{
		struct wlr_xdg_toplevel_decoration_v1 *wlr_deco;
		struct wl_listener request_mode;
		struct wl_listener destroy;
	} my_decoration;
};

//function_setのプロトタイプ宣言
void function_set(struct server *server);
//終了時にメモリを開放する関数
void server_destroy(struct server *server);

//h_keyのプロトタイプ宣言
void h_key(struct wl_listener *listener, void *data);
//キーの修飾キーが変化したときに呼ばれる関数のプロトタイプ宣言
void modifire_key(struct wl_listener *listener, void *data);
//newinput_keyboardのプロトタイプ宣言
void newinput_device(struct wl_listener *listener, void *data);
//newinput_mouceのプロトタイプ宣言
void newinput_mouce(struct wl_listener *listener, void *data);
//マウスのボタンイベントが発生したときに呼ばれる関数のプロトタイプ宣言
void newinput_moucebotton(struct wl_listener *listener, void *data);
//クライアントがカーソルイメージを変更したいときに実行される関数
void get_cliant_cursor_image(struct wl_listener *listener, void *data);
//マウススクロール関数
void mouse_scroll_func(struct wl_listener *listener, void *data);
//スクロール量変数チェック
double check_axis_delta_value(int8_t scroll_device);

//new_outputのプロトタイプ宣言
void new_output(struct wl_listener *listener, void *data);
//描画用関数のプロトタイプ宣言
void output_frame(struct wl_listener *listener, void *data);

//server_new_xdg_toplevelのプロトタイプ宣言
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
//commitの確認のためのリスナーのプロトタイプ宣言
void checkcomit(struct wl_listener *listener, void *data);
// windowの描画要求時に呼ばれる関数のプロトタイプ宣言
void displaypush(struct wl_listener *listener, void *data);
// windowの消去要求時に呼ばれる関数のプロトタイプ宣言
void displaypull(struct wl_listener *listener, void *data);
/*
サーフェスにキーボードの状態を転送させる関数
第一引数にフォーカスさせたいサーフェス、第二引数にwlr_seat構造体を。
もし第一引数とフォーカスされてるサーフェスが同じなら何もしない
*/
void focus_keyboard(struct wlr_surface *focus_surface, struct wlr_seat *focus_seat);
//特定のウィンドウにマウスをフォーカスする関数
void focus_mouce(struct wlr_surface *focus_surface, struct wlr_seat *focus_seat,
	struct wlr_cursor *cursor, struct pos_double *focus_surface_pos);
//クライアントのリサイズ要求時に発火するシグナルを検知したときに実行する関数
void surface_request_resize(struct view *resize_view, uint32_t data);
//scene_nodeからsurfaceに変換する関数
struct wlr_xdg_toplevel *conversion_wlr_scene_node_to_wlr_xdg_toplevel(
	struct wlr_scene_node *same_pos_mouce_scene_node);
//ウィンドウ要求時にview構造体にデータを配置する関数
struct view *view_create(struct server *server, struct wlr_xdg_toplevel *xdg_toplevel,
	struct wlr_scene_tree *new_scene_tree);
//サーフェス移動時に実行する関数
void request_surface_move(struct wl_listener *listener, void *data);
//サーフェスの規定サイズを設定します
void put_surface_size_value(struct view *put_size_view);
//最初のデコレーションモード確認の関数
void server_new_xdg_decoration(struct wl_listener *listener, void *data);
//新しいデコレーションが作られたときの処理
void deco_request_mode(struct wl_listener *listener, void *data);
//クライアント終了時に呼ばれるデコレーションの消去関数
void deco_destroy(struct wl_listener *listener, void *data);
//toplevel構造体の画面推移時などに破棄するときに発火する関数
void toplevel_destroy(struct wl_listener *listener, void *data);

//壁紙の生成関数の実装
bool new_wallpaper_criant_create(struct server *server);
bool new_mybar_start(struct server *server);
//壁紙のサーフェス設定
void layer_shell_create(struct wl_listener *listener, void *data);
//壁紙の描画要求時に実行する関数
void wallpaper_commit(struct wl_listener *listener, void *data);
//ステータスバーなど、常に前に表示されるサーフェスが要求されたときに実行する関数
void request_mybar(struct wl_listener *listener, void *data);

//カーソルの画像変更関数
void my_set_cursor_img(struct mouce_structure_mem_mgr *mouce_structure, const char *img_name);
#endif
