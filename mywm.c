#include "glib.h"
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <gdk-pixbuf-2.0/gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <drm_fourcc.h>
#include <wlr/render/pass.h>
#include <signal.h>
  struct server {
    
    //バックエンド構造体の定義
    struct wlr_backend *backend;

    //リスナーを定義
    struct wl_listener new_input;

    //キーイベントの構造体を定義
    struct wlr_keyboard *keyboard;

    //キーイベントの構造体を定義
    struct wl_listener key;

    //アウトプットリスナーの定義
    struct wl_listener new_output;
    //サーバ本体の構造体の定義
    struct wl_display  *display;

    //レンダラーの定義（描画バッファ構造体）
    struct wlr_renderer *renderer;
    //GPUメモリ確保（確保した領域にレンダラーで描画バッファを保持することができる）
    struct wlr_allocator *allocator;

    struct wlr_output_layout *output_layout;

    //フレームリスナー
    struct wl_listener frame;

    //テクスチャ構造体
    struct wlr_texture *background_tex;

    //ハードウェアカーソル
    struct wlr_pointer pointer;

    //論理カーソル
    struct wlr_cursor *cursor;

    //マウスリスナー
    struct wl_listener  mouce_listener;

    struct wlr_xcursor_manager *cursor_mgr ;

    //タスクバーピクセル
    uint32_t *taskbar_pix;

    //タスクバーテクスチャ
    // (vramに移すためにtaskbar_pixを生のピクセルデータに変換したものをいれる変数)
    struct wlr_texture *taskbar_tex;

    //xdg_shell: wlrootsがそのリクエストを受け取り、
    // struct wlr_xdg_toplevel という「型」のデータを作る。
    struct wlr_xdg_shell *xdg_shell;

    struct wl_list views; // 表示するウィンドウ（view）のリスト
    
    struct wl_listener new_xdg_toplevel; // 新しいウィンドウが作られた時のリスナー

    };


struct my_pointer {
    struct wlr_input_device *device;
    struct wl_listener motion;
};

//タスクバー関係
struct taskbar {
    bool taskbar_rend;
    bool firsttaskbar;
    //タスクバーの透明度
    float taskbar_alpha;
    //タスクバーの高さ
    int taskbar_height;
};

struct view{
    struct wl_list link;
    struct wlr_xdg_toplevel *toplevel;
    double x, y; // 画面上のどこに置くか
};


//面倒くさいからグローバル変数として扱う
struct server s1 = {0};

struct taskbar taskbar_v1 ={0};

//ハードウェアカーソルの許容個数
struct my_pointer *ptr[10] ={0};

int a=0;

//h_keyのプロトタイプ宣言
void h_key(struct wl_listener *listener,void *data);

//newinput_keyboardのプロトタイプ宣言
void newinput_device(struct wl_listener *listener,void *data);

//new_outputのプロトタイプ宣言
void new_output(struct wl_listener *listener,void *data);

//function_setのプロトタイプ宣言
void function_set();

//描画用関数のプロトタイプ宣言
void output_frame(struct wl_listener *listener,void *data);

void newinput_mouce(struct wl_listener *listener,void *data);

void server_new_xdg_toplevel(struct wl_listener *listener, void *data);


int main(int argc,char *argv[]){
    wlr_log_init(WLR_DEBUG,NULL);

    // 最初に初期化
    wl_list_init(&s1.new_input.link);
    wl_list_init(&s1.new_output.link);
    wl_list_init(&s1.frame.link);
    wl_list_init(&s1.key.link);

    s1.cursor = wlr_cursor_create();

    //waylandサーバ(display)構造体の定義と初期化
    s1.display = wl_display_create();

    //モニターのデータを定義
    s1.output_layout = wlr_output_layout_create(s1.display);

    //waylandサーバ構造体からloopのメンバ部分だけ抜き取り
    // 構造体としてloopを定義する
    struct wl_event_loop *loop =wl_display_get_event_loop(s1.display);
    GError *img_error = NULL;

    GdkPixbuf  *pixbuf = gdk_pixbuf_new_from_file("wp.jpg",&img_error);

    // 1. xdg_shellの作成
    s1.xdg_shell = wlr_xdg_shell_create(s1.display,3);
    
    if(!pixbuf){
        printf("NOWALLPAPER");
        if(img_error){
            fprintf(stderr,
            "domain=%s\ncode=%d\nmessage=%s\n",
            g_quark_to_string(img_error->domain),
            img_error->code,
            img_error->message);

            g_error_free(img_error);
        }
        return 0;
    }
    // 1. Pixbufから情報の「抽出」
    // GdkPixbufは「ただのメモリの塊」なので、幅、高さ、1行のデータサイズ(stride)を聞き出します
    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    int stride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    // 2. フォーマットの「翻訳」
    // GdkPixbufのメモリ並び順 (R, G, B, A) を、wlrootsが理解できる「DRMフォーマット」に変換します
    // ※リトルエンディアン環境では、バイト順 R,G,B,A は 0xAABBGGRR となり、これは ABGR8888 に相当します
    uint32_t drm_format = has_alpha ? DRM_FORMAT_ABGR8888 : DRM_FORMAT_BGR888;
    

    //もしdisplayに接続するソケットが自動で追加できなければ終了する
    if(wl_display_add_socket_auto(s1.display)<0){
        fprintf(stderr,"ソケットの作成に失敗しました");
        return 0;
    }
        //バックエンド生成
        s1.backend = wlr_backend_autocreate(loop, NULL);

        //取得したバックエンドから適切なレンダラーを生成する
        s1.renderer = wlr_renderer_autocreate(s1.backend);

        //Wayland ディスプレイ (wl_display) にレンダラーを登録する
        wlr_renderer_init_wl_display(s1.renderer, s1.display);

        //allocater（フレームバッファ）の初期化
        s1.allocator = wlr_allocator_autocreate(s1.backend,s1.renderer);

    //display構造体の共有メモリ(SHM)を初期化し、クライアントがSHMに書き込む
    //描画バッファを扱えるようにする
    wl_display_init_shm(s1.display);


    //メンバに関数ポインタをセット
    function_set();
    
    //s1.backend->events.new_inputが発火したらs1.new_input.notifyを実行する(関数)
    wl_signal_add(&s1.backend->events.new_input,&s1.new_input);

    //出力デバイスが追加されたらnew_outputの関数を実行する
    wl_signal_add(&s1.backend->events.new_output,&s1.new_output);

    //バックエンドのイベント監視ループ開始
    if(!wlr_backend_start(s1.backend)){
        fprintf(stderr, "Failed to start backend\n");
        return 1;
    }

    // 3. GPUへの「アップロード」
    // ここで初めて CPU(RAM) -> GPU(VRAM) のコピーが行われます
    // ※ s1.renderer は事前に作成されている必要があります

        s1.background_tex = wlr_texture_from_pixels(
        s1.renderer,
        drm_format,
        stride,
        width,
        height,
        pixels
    );

     // 失敗チェック（GPUのメモリ不足などで失敗する場合があるため）
    if (s1.background_tex == NULL) {
        fprintf(stderr, "Failed to create texture from pixbuf\n");
        return 0; // または適切なエラー処理
    }
    
    // 4. 後始末
    // GPUにコピーし終わったので、CPU側のデータ（pixbuf）はもう不要です
    g_object_unref(pixbuf);

 
     //無限ループでクライアントからのイベントを処理する
    wl_display_run(s1.display);


    // 1. まずリスナーを「登録した場所」から確実に外す
    wl_list_remove(&s1.new_input.link);
    
    wl_list_remove(&s1.new_output.link);
    wl_list_remove(&s1.frame.link);
    
    // キーボードとマウスは「デバイス」に紐付いているため、
    wl_list_remove(&s1.key.link);
    for(int h=0;h<10;h++){
        if (ptr[h] == NULL) {
            continue; // マウスが登録されていない枠はスキップ
        }
        // リストに繋がっているなら外す
        if(!wl_list_empty(&ptr[h]->motion.link)){
            wl_list_remove(&ptr[h]->motion.link);
        }
        free(ptr[h]);
        ptr[h]=NULL;
    }
    wlr_xcursor_manager_destroy(s1.cursor_mgr);
    wlr_cursor_destroy(s1.cursor);
    wl_display_destroy_clients(s1.display);

    // 3. 残りのリソース解放
    wlr_texture_destroy(s1.background_tex);
    wlr_renderer_destroy(s1.renderer);
    wlr_allocator_destroy(s1.allocator);
    wlr_backend_destroy(s1.backend);
    wl_display_destroy(s1.display);

    return 0;
}



void h_key(struct wl_listener *listener,void *data){

    //キーボード構造体を取得（グローバル変数s1を使っている前提）
    struct wlr_keyboard *keyboard = s1.keyboard;

    //送られてきたデータをeventに代入する
    struct wlr_keyboard_key_event *event = data;

    //libxkbcommonの仕様上メンバのキーコードの値＋８をする
    uint32_t keycode = event->keycode + 8;

    //文字コードをいれる変数
    const xkb_keysym_t *syms;

    //デバイスのキーボードのデータが入っているkeybord構造体の
    // 押された物理キー番号からどのキーが押されたかを計算し、
    //symsポインタにいれる。（返り値は押されたキーの個数
    int nsyms = xkb_state_key_get_syms(keyboard->xkb_state,keycode,&syms);

    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
        }

    //押されたキーの個数回ループする
    for(int i= 0; i<nsyms;i++){
        //キーネームを格納する変数
        char name[64];

        //syms[i]から数値を取り出し数値に対応する文字列を
        // name[64]にいれる(文字コードのような概念)
        xkb_keysym_get_name(syms[i],name,sizeof(name));
        printf("%s\n",name);
        if(strcmp(name,"p") == 0){
            wl_display_terminate(s1.display);
        }
        
    }
}

//新しい入力デバイスが接続されたら実行する
void newinput_device(struct wl_listener *listener,void *data){

    //第一引数から、第一引数が含まれているポインタのアドレスを逆算する
    struct server *s1= wl_container_of(listener, s1,new_input);

    //第二引数のアドレスをローカルポインタに代入する
    struct wlr_input_device *device = data;

    //もしデバイスタイプがキーボードだったら
    if(device->type == WLR_INPUT_DEVICE_POINTER){
        if(a>=10)return;

        ptr[a] = calloc(1,sizeof(*ptr[0]));
        ptr[a]->device=device;
        ptr[a]->motion.notify = newinput_mouce;
        wlr_cursor_attach_input_device(s1->cursor,device);
        wl_signal_add(&s1->cursor->events.motion,&ptr[a]->motion);
        a++;
        return;
    }

    if(device->type == WLR_INPUT_DEVICE_KEYBOARD){
    
        //インプットデバイスからキーボード構造体を受け取る
        s1->keyboard = wlr_keyboard_from_input_device(device);

        //コンテキスト（キーボードの状態などを扱うための作業領域）を確保する
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        //キーマップを作成する。物理キーコード(キーID（数値）)を論理キー（例えば A や Enter）に変換る
        //OS がどのキーを押したかを解釈するために必要
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

        //XKB keymap をキーボードに紐付ける関数
        wlr_keyboard_set_keymap(s1->keyboard, keymap);

        //context構造体は使わないのでメモリを解放する
        xkb_context_unref(context);
        xkb_keymap_unref(keymap);

        // s1->key_event は signal に listener を登録すwlr_keyboard_set_keymap(s1.keyboard, keymap);る関数でアクセス
        wl_signal_add(&s1->keyboard->events.key,&s1->key);
    }
    
}


void new_output(struct wl_listener *listener,void *data){
    //生成されたoutput構造体にdataを代入する
    struct wlr_output *output = data;

    //outputに構造体を紐づけて描画可能にする
    wlr_output_init_render(output,s1.allocator,s1.renderer);

    // 状態（stat)構造体を定義する
    struct wlr_output_state state;

    //stateを初期化する
    wlr_output_state_init(&state);
    

    // 画面を有効化
    wlr_output_state_set_enabled(&state, true);

    //output->modeにデータが入っているかの条件分岐
    if(!wl_list_empty(&output->modes)){

        wl_signal_add(&output->events.frame,&s1.frame);

        //modeに推奨されているモードを代入する
        struct wlr_output_mode *mode = wlr_output_preferred_mode(output); 

        //もしmodeに推奨設定があるなら
        if(mode){

            //stateに推奨設定をいれる
            wlr_output_state_set_mode(&state,mode);
        }  

    }

    wlr_output_layout_add_auto(s1.output_layout,output);
     // カーソル画像（NULLならデフォルト）とサイズ（24など）を指定
    s1.cursor_mgr = wlr_xcursor_manager_create(NULL,32);

    //カーソル画像をロード
    wlr_xcursor_manager_load(s1.cursor_mgr,2.0);

    wlr_cursor_attach_output_layout(s1.cursor,s1.output_layout);
    //sateの設定をoutputに反映させる
    wlr_output_commit_state(output,&state);

    //state構造体はもう使わないのでリソースを解放させる
    wlr_output_state_finish(&state);
    
}


void function_set(){
    //s1.key.notifyにh_key関数アドレスを代入している
    s1.key.notify = h_key;

    //上記と同じ
    s1.new_input.notify = newinput_device;

    //new_output関数のアドレスを代入
    s1.new_output.notify = new_output;

    //描画可能シグナルとoutput_frame関数を実行する
    s1.frame.notify = output_frame;

    // 新しいウィンドウが要求された時に呼ばれる関数を登録
    s1.new_xdg_toplevel.notify = server_new_xdg_toplevel;
}


//描画関数
void output_frame(struct wl_listener *listener,void *data){
    //outputに代入する 
    struct wlr_output *output = data;

    //statの定義
    struct wlr_output_state state;
    //statの初期化
    wlr_output_state_init(&state);

    //モニタに対して、今から絵を描くための専用レーン（パス）を確保する
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(output,&state,NULL);

    struct wlr_render_texture_options options = {0};
    
    //何を描くか指定 
    options.texture=s1.background_tex;

    float alpha =1.0;
    options.alpha = &alpha;

    // これを指定しないと、サイズ 0 で描画されるため何も見えません。
    options.dst_box.x = 0;
    options.dst_box.y = 0;
    options.dst_box.width = output->width;   // モニタの横幅いっぱい
    options.dst_box.height = output->height; // モニタの縦幅いっぱい

    //passにモニタに描画する物を追加する(これは壁紙の追加関数)
    wlr_render_pass_add_texture(pass,&options);

    if(taskbar_v1.firsttaskbar == 0){
        //ピクセルのデータをいれる変数を初期化
        s1.taskbar_pix = calloc(options.dst_box.width*70,sizeof(uint32_t));
        //タスクバーを表示したい範囲分forを回す
        for(int i=0;i<options.dst_box.width*70;i++){
            //黒色を代入
            s1.taskbar_pix[i] = 0xFF000000;
        } 
        //一回しかif(firsttaskbar == 0)が回らないようにする
        taskbar_v1.firsttaskbar = 1;
        //メモリからvramに渡す
        s1.taskbar_tex = wlr_texture_from_pixels(
            s1.renderer,
            DRM_FORMAT_ABGR8888,
            output->width * sizeof(uint32_t),
            output->width,
            70,
            s1.taskbar_pix
        );
        //メモリにあるピクセルデータを解放する
        free(s1.taskbar_pix);
    }

    //タスクバーの出現設定
    if(s1.cursor->y <= output->height-180){
         if(taskbar_v1.taskbar_height>=0){
            taskbar_v1.taskbar_height-=5;
        }

    }
    else if(taskbar_v1.taskbar_height<=70){
            taskbar_v1.taskbar_height+=5;
    }

    taskbar_v1.taskbar_alpha=0.4;

    if (s1.taskbar_tex) {
        //テクスチャをどの座標のピクセルから描くか
        struct wlr_render_texture_options bar_opt = {
            .texture = s1.taskbar_tex,//タスクバーのテクスチャ
            .dst_box= {//座標
                .x = 0, 
                .y = output->height - taskbar_v1.taskbar_height, // 画面の下端に配置
                .width = output->width, 
                .height =  taskbar_v1.taskbar_height,
            },
            .alpha = &taskbar_v1.taskbar_alpha//透明度
        };
        //タスクバーの追加関数
        wlr_render_pass_add_texture(pass, &bar_opt);
    }

    //passをgpに送る
    wlr_render_pass_submit(pass);

    //passのメモリを解放する
    wlr_output_commit_state(output,&state);
    wlr_output_state_finish(&state);

}

void newinput_mouce(struct wl_listener *listener,void *data){

    //dataをローカル変数に渡す
    struct wlr_pointer_motion_event *mouce = data;

    //前フレームからのマウスの移動量をカーソルの座標に反映させる
    wlr_cursor_move(s1.cursor,&mouce->pointer->base,mouce->delta_x,mouce->delta_y);

    //論理カーソルを描画する
    wlr_cursor_set_xcursor(s1.cursor,s1.cursor_mgr,"left_ptr");  

}
 void server_new_xdg_toplevel(struct wl_listener *listener, void *data);