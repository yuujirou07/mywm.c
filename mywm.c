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
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include<linux/input-event-codes.h>
#include <libinput.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_drm.h> 
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

    struct wlr_compositor *compositor; // コンポジタの構造体

    struct wlr_seat *seat; // シートの構造体

    struct wlr_output *outputs; // 出力デバイスのリスト

    struct wlr_decoration_manager *decoration_manager; // ウィンドウの装飾を管理する構造体

    struct view *grabbed_view; // ドラッグ中のウィンドウを保持する構造体

    struct view *resizing_view; //リサイズ中のウィンドウを保持する構造体

    struct wl_listener key_modifier; // キーの修飾キーが変化したときのリスナー

    char *cursor_image; // カーソルの画像名を保持する変数

    int window_side;
    };

//マウスの構造体
struct my_pointer {
    struct wlr_input_device *device;
    struct wl_listener motion;
    struct wl_listener button;
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
//ウィンドウの構造体
struct view{
    struct wl_list link;
    struct wlr_xdg_toplevel *toplevel;
    double x, y; // 画面上のどこに置くか
    struct wl_listener map;   // アプリのwindowの描画要求時に呼ぶリスナー
    struct wl_listener unmap; // アプリが消去要求を出したときのためのリスナー
    struct wl_listener destroy; // アプリが閉じる要求を出したときのためのリスナー
    struct wl_listener commit;  //初期化が完了しているかのリスナー
};


//面倒くさいからグローバル変数として扱う
//サーバ構造体の定義と初期化
struct server s1 = {0};
//タスクバーの構造体の定義と初期化
struct taskbar taskbar_v1 ={0};

//ハードウェアカーソルの許容個数
struct my_pointer *ptr[10] ={0};

//マウスの個数を数える変数
int a=0;      

//マウスのボタンが押されているかのフラグ
bool bottunpressed = false;

struct mouce_taskbar_pos {
    double x;
    double y;
};
struct mouce_taskbar_pos mtb_pos = {0};

//キーの修飾キーが変化したときに呼ばれる関数のプロトタイプ宣言
void modifire_key(struct wl_listener *listener, void *data);

//マウスのボタンイベントが発生したときに呼ばれる関数のプロトタイプ宣言
void newinput_moucebotton(struct wl_listener *listener,void *data);

//commitの確認のためのリスナーのプロトタイプ宣言
void checkcomit(struct wl_listener *listener, void *data);

// windowの描画要求時に呼ばれる関数のプロトタイプ宣言
void displaypush(struct wl_listener *listener, void *data);

// windowの消去要求時に呼ばれる関数のプロトタイプ宣言
void displaypull(struct wl_listener *listener, void *data);

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
//newinput_mouceのプロトタイプ宣言
void newinput_mouce(struct wl_listener *listener,void *data);
//server_new_xdg_toplevelのプロトタイプ宣言
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);


int main(int argc,char *argv[]){
    //ログレベルと出力先を指定する関数
    wlr_log_init(WLR_INFO,NULL);

    // 最初に初期化
    wl_list_init(&s1.new_input.link);
    wl_list_init(&s1.new_output.link);
    wl_list_init(&s1.frame.link);
    wl_list_init(&s1.key.link);
    wl_list_init(&s1.new_xdg_toplevel.link);  
    wl_list_init(&s1.views);

    //waylandサーバ(display)構造体の定義と初期化
    s1.display = wl_display_create();

    //モニターのデータを定義
    s1.output_layout = wlr_output_layout_create(s1.display);

    s1.cursor = wlr_cursor_create();

    wlr_cursor_attach_output_layout(s1.cursor, s1.output_layout);

    //waylandサーバ構造体からloopのメンバ部分だけ抜き取り
    // 構造体としてloopを定義する
    struct wl_event_loop *loop =wl_display_get_event_loop(s1.display);
    GError *img_error = NULL;
    //壁紙の画像を読み込む。失敗したらimg_errorにエラー情報が入る
    GdkPixbuf  *pixbuf = gdk_pixbuf_new_from_file("wp.jpg",&img_error);

    // 1. xdg_shellの作成
    s1.xdg_shell = wlr_xdg_shell_create(s1.display,3);

    // 2. 壁紙の読み込み
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
    
    const char *socket_name;
    //もしdisplayに接続するソケットが自動で追加できなければ終了する
    if((socket_name = wl_display_add_socket_auto(s1.display)) == NULL){
        fprintf(stderr,"socket error\n");
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

    //アプリたちの要求を受け付け、管理するための窓口（帳簿）であるコンポジタを作成する
    s1.compositor = wlr_compositor_create(s1.display,6,s1.renderer);

    //サブコンポジタの作成。サブコンポジタは、クライアントがサブサーフェスを作成するためのインターフェースを提供します。
    //これを呼び出すことで、クライアント（アプリケーション）は一つのウィンドウの中に、
    // 独立して更新・制御できる複数の「子画面（サブサーフェス）」を持てるようになる
    wlr_subcompositor_create(s1.display);

    //データデバイスマネージャの作成。データデバイスマネージャは、クリップボードやドラッグアンドドロップなどのデータ転送機能を提供します。
    wlr_data_device_manager_create(s1.display);

    s1.seat = wlr_seat_create(s1.display, "seat0");



    //メンバに関数ポインタをセット
    function_set();
    
    //s1.backend->events.new_inputが発火したらs1.new_input.notifyを実行する(関数)
    wl_signal_add(&s1.backend->events.new_input,&s1.new_input);

    //出力デバイスが追加されたらnew_outputの関数を実行する
    wl_signal_add(&s1.backend->events.new_output,&s1.new_output);

    //クライアントが新しいxdg_toplevelウィンドウを要求したときに発火する
    wl_signal_add(&s1.xdg_shell->events.new_toplevel,&s1.new_xdg_toplevel);


    //バックエンドのイベント監視ループ開始
    if(!wlr_backend_start(s1.backend)){
        fprintf(stderr, "Failed to start backend\n");
        return 1;
    }

    setenv("WAYLAND_DISPLAY",socket_name,1);
    pid_t pid = fork();

    if (pid == 0) {
    // 子プロセスの処理
        execlp("foot", "foot", NULL);
        perror("exec failed");
        _exit(1);
    }
    else if (pid > 0) {
    // 親プロセスの処理

    }
    else {
        //エラー
        printf("forkerror/n");
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
    // 2. 次に、リスナーが紐付いている構造体を破棄する
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


//キーイベントが発生したときに呼ばれる関数
void h_key(struct wl_listener *listener,void *data){
      printf("h_key keyboard address: %p\n", s1.keyboard);

    //キーボード構造体を取得（グローバル変数s1を使っている前提）
    struct wlr_keyboard *keyboard = s1.keyboard;

    //送られてきたデータをeventに代入する
    struct wlr_keyboard_key_event *event = data;

     //フォーカス中のクライアントにキーイベントを転送する関数です。引数は、シート、イベントの時間、キーコード、キーの状態（押されたか離されたか）です。
    wlr_seat_keyboard_notify_key(s1.seat, event->time_msec, event->keycode, event->state);

    //もしキーが離されたときのイベントだったら、以降の処理をしない
    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
        }


    //libxkbcommonの仕様上メンバのキーコードの値＋８をする
    uint32_t keycode = event->keycode + 8;

    //文字コードをいれる変数
    const xkb_keysym_t *syms;

    //デバイスのキーボードのデータが入っているkeybord構造体の
    // 押された物理キー番号からどのキーが押されたかを計算し、
    //symsポインタにいれる。（返り値は押されたキーの個数
    int nsyms = xkb_state_key_get_syms(keyboard->xkb_state,keycode,&syms);

    
    //押されたキーの個数回ループする
    for(int i= 0; i<nsyms;i++){
        //キーネームを格納する変数
        char name[64];

        //syms[i]から数値を取り出し数値に対応する文字列を
        // name[64]にいれる(文字コードのような概念)
        xkb_keysym_get_name(syms[i],name,sizeof(name));
        printf("%s\n",name);
        if(strcmp(name,"Escape") == 0){
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
        ptr[a]->button.notify = newinput_moucebotton;

        //libinputデバイスか確認
        if (wlr_input_device_is_libinput(ptr[a]->device)) {
             //生のlibinput_deviceのポインタ取得
            struct libinput_device *ldev =wlr_libinput_get_device_handle(ptr[a]->device);
            //ポインタの中身のポインタデバイスのタップ機能を有効にする
            libinput_device_config_tap_set_enabled(ldev,LIBINPUT_CONFIG_TAP_ENABLED);
        }

        //カーソルと物理マウスを紐付ける関数
        wlr_cursor_attach_input_device(s1->cursor,ptr[a]->device);

        //マウスが動いたときに発火する
        wl_signal_add(&s1->cursor->events.motion,&ptr[a]->motion);

        //マウスのボタンが押されたときに発火する
        wl_signal_add(&s1->cursor->events.button,&ptr[a]->button);
        a++;
        return;
    }

    if(device->type == WLR_INPUT_DEVICE_KEYBOARD){
    
        //インプットデバイスからキーボード構造体を受け取る
        s1->keyboard = wlr_keyboard_from_input_device(device);

        //コンテキスト（キーボードの状態などを扱うための作業領域）を確保する
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        //キーボードのレイアウトをjpにする
        struct xkb_rule_names rules = {
        .layout = "jp",
        };

        //キーマップを作成する。物理キーコード(キーID（数値）)を論理キー（例えば A や Enter）に変換る
        //OS がどのキーを押したかを解釈するために必要
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

        //キーボードの状態を管理する構造体を作成する。これがないと、OSはどのキーが押されているかを把握できない
        xkb_state_new(keymap);

        //XKB keymap をキーボードに紐付ける関数
        wlr_keyboard_set_keymap(s1->keyboard, keymap);

        //context構造体は使わないのでメモリを解放する
        xkb_context_unref(context);
        xkb_keymap_unref(keymap);

        // s1->key_event は signal に listener を登録すwlr_keyboard_set_keymap(s1.keyboard, keymap);る関数でアクセス
        wl_signal_add(&s1->keyboard->events.key,&s1->key);

        s1->key_modifier.notify = modifire_key; 
        wl_signal_add(&s1->keyboard->events.modifiers, &s1->key_modifier);

        // クライアントにこのseatはキーボードとマウスが使えるよ」と宣言する関数
        //Seat とは、マウスやキーボードなどの入力デバイスをひとまとめにした論理的なグループを指します。
        wlr_seat_set_capabilities(s1->seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

        // seatの内部に「実際に使うキーボードはこれ」と登録する関数
        wlr_seat_set_keyboard(s1->seat, s1->keyboard);
    }
}
void modifire_key(struct wl_listener *listener, void *data) {
    printf("modifire_key keyboard address: %p\n", s1.keyboard);
    printf("modifiers updated!\n"); 

    //キーボード構造体を取得グローバル変数よりdataのほうが新しい可能性があるため、dataから取得する
    struct wlr_keyboard *keyboard = data;
    // クライアントに修飾キーの状態を通知する関数
    wlr_seat_keyboard_notify_modifiers(s1.seat, &keyboard->modifiers);
}
    

//新しい出力デバイスが接続されたら実行する
void new_output(struct wl_listener *listener,void *data){
    //生成されたoutput構造体にdataを代入する
    struct wlr_output *output = data;
    s1.outputs = output;
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
        //output->events.frameが発火したらoutput_frame関数を実行する
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

    //sateの設定をoutputに反映させる
    wlr_output_commit_state(output,&state);

    //state構造体はもう使わないのでリソースを解放させる
    wlr_output_state_finish(&state);
    
}

//関数ポインタを構造体のメンバにセットする関数
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

    //透明度の指定1.0は不透明、0.0は完全に透明
    float alpha =1.0;
    options.alpha = &alpha;

    // これを指定しないと、サイズ 0 で描画されるため何も見えません。
    options.dst_box.x = 0;
    options.dst_box.y = 0;
    options.dst_box.width = output->width;   // モニタの横幅いっぱい
    options.dst_box.height = output->height; // モニタの縦幅いっぱい

    //passにモニタに描画する物を追加する(これは壁紙の追加関数)
    wlr_render_pass_add_texture(pass,&options);

    //描画するウィンドウをリストから順番に取り出して描画する
    struct view *v;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now); // 現在時刻を取得
    wl_list_for_each(v, &s1.views, link) {//リストから順番に取り出す関数
        if (!v->toplevel->base->surface->mapped) {//surfaceが描画可能かの条件分岐
        continue; 
    }

        struct wlr_surface *surface = v->toplevel->base->surface;//surface構造体を定義して、toplevel->base->surfaceを代入する

        struct wlr_render_texture_options opts = {0};//描画する物の指定をする構造体を定義して初期化する
        opts.texture = wlr_surface_get_texture(surface);//surfaceからテクスチャを取得してopts.textureに代入する
        if (!opts.texture) continue;
        opts.dst_box.x = v->x;//どこに描くかの指定。今回はv->x,v->yに描く
        opts.dst_box.y = v->y;//surfaceの幅と高さをopts.dst_boxに代入する
        opts.dst_box.width = surface->current.width;//surfaceの幅をopts.dst_box.widthに代入する
        opts.dst_box.height = surface->current.height;//surfaceの高さをopts.dst_box.heightに代入する
        opts.alpha = &alpha;//透明度の指定

        wlr_render_pass_add_texture(pass, &opts);//passに描画する物を追加する関数
        wlr_surface_send_frame_done(surface, &now);
    }


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

    //passをgpuに送る
    wlr_render_pass_submit(pass);

    //passのメモリを解放する
    wlr_output_commit_state(output,&state);
    wlr_output_state_finish(&state);
    wlr_output_schedule_frame(output);
}

//マウスイベントが発生したときに呼ばれる関数
void newinput_mouce(struct wl_listener *listener,void *data){

    //dataをローカル変数に渡す
    struct wlr_pointer_motion_event *mouce = data;


    //前フレームからのマウスの移動量をカーソルの座標に反映させる
    wlr_cursor_move(s1.cursor,&mouce->pointer->base,mouce->delta_x,mouce->delta_y);

     if(s1.grabbed_view != NULL){
        s1.grabbed_view->x += s1.cursor->x - mtb_pos.x - s1.grabbed_view->x; // ドラッグ開始位置からの移動量を計算してウィンドウの位置に反映
        s1.grabbed_view->y += s1.cursor->y - mtb_pos.y - s1.grabbed_view->y;
    }
    struct view *v;
    //リストから順番に取り出す関数
    wl_list_for_each(v, &s1.views, link) {
        if(s1.cursor->x >= v->x && s1.cursor->x <= v->x + v->toplevel->base->surface->current.width && 
            s1.cursor->y >= v->y && s1.cursor->y <= v->y + 15){
            s1.cursor_image = "sb_v_double_arrow";
            s1.window_side=1;
            s1.resizing_view = v;
            break;
        }
        else if(s1.cursor->x >= v->x && s1.cursor->x <= v->x + v->toplevel->base->surface->current.width && 
            s1.cursor->y >= v->y + v->toplevel->base->surface->current.height - 15 && 
            s1.cursor->y <= v->y + v->toplevel->base->surface->current.height){
            s1.cursor_image = "sb_v_double_arrow";
            s1.window_side=2;
            s1.resizing_view = v;
            break;
        }
        else if(s1.cursor->x >= v->x && s1.cursor->x <= v->x + 15 && 
            s1.cursor->y >= v->y && s1.cursor->y <= v->y + v->toplevel->base->surface->current.height){
            s1.cursor_image = "sb_h_double_arrow";
            s1.resizing_view = v;
            s1.window_side=3;
            break;
        }
        else if(s1.cursor->x >= v->x + v->toplevel->base->surface->current.width - 15 
            && s1.cursor->x <= v->x + v->toplevel->base->surface->current.width && 
            s1.cursor->y >= v->y && s1.cursor->y <= v->y + v->toplevel->base->surface->current.height){
            s1.cursor_image = "sb_h_double_arrow";
            s1.window_side=4;
            s1.resizing_view = v;
            break;
        }
        else{
            s1.cursor_image = "left_ptr";
            continue;
        }
    }

    if(bottunpressed && strcmp(s1.cursor_image,"left_ptr")!=0){
        if(s1.window_side==1){
            //マウスボタン押下（リサイズ開始）
            wlr_xdg_toplevel_set_resizing(v->toplevel, true);

            wlr_xdg_toplevel_set_size(s1.resizing_view->toplevel,s1.resizing_view->toplevel->base->surface->current.width,
            s1.resizing_view->toplevel->base->surface->current.height+mouce->delta_x);
            s1.resizing_view->x += mouce->delta_x; 

            //リサイズ終了
            wlr_xdg_toplevel_set_resizing(v->toplevel, false);
        }
    }
    //論理カーソルを描画する
    wlr_cursor_set_xcursor(s1.cursor,s1.cursor_mgr,s1.cursor_image);  
}
//マウスのボタンイベントが発生したときに呼ばれる関数
void newinput_moucebotton(struct wl_listener *listener,void *data){
    struct wlr_pointer_button_event *button = data;

    //マウスのボタンイベントをクライアントに転送する関数
    wlr_seat_pointer_notify_button(
        s1.seat,
        button->time_msec,
        button->button,
        button->state
    );
    if(button->state == WL_POINTER_BUTTON_STATE_PRESSED){
        bottunpressed = true;
    }
    else if(button->state == WL_POINTER_BUTTON_STATE_RELEASED){
        bottunpressed = false;
        s1.grabbed_view = NULL; // ドラッグを終了するために grabbed_view をリセット
    }
     if(bottunpressed){
        struct view *v;
        //リストから順番に取り出す関数
        wl_list_for_each(v, &s1.views, link) {
            //カーソルがウィンドウのタイトルバーの範囲内にあるかの条件分岐。ここではタイトルバーの高さを40と仮定している
            // タイトルバーの範囲をウィンドウの上部15ピクセルから40ピクセルまでと仮定
            //15ピクセルはウィンドウを伸縮するための範囲とする
            if(s1.cursor->x >= v->x && s1.cursor->x <= v->x + v->toplevel->base->surface->current.width &&
                s1.cursor->y >= v->y+15 && s1.cursor->y <= v->y +40) {// タイトルバーの高さを30と仮定
                mtb_pos.x = s1.cursor->x - v->x; // ドラッグ開始位置のオフセットを保存  
                mtb_pos.y = s1.cursor->y - v->y;
                s1.grabbed_view = v; // ドラッグ中のウィンドウを保持
                break;
            }
            if(s1.cursor->x >= v->x && s1.cursor->x <= v->x + v->toplevel->base->surface->current.width &&
                s1.cursor->y >= v->y && s1.cursor->y <= v->y + v->toplevel->base->surface->current.height) {
                // seat のキーボードフォーカスをこのサーフェスに設定する
                struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(s1.seat);

                if(keyboard){
                    //マウスクリックしたときにマウスが乗っているウィンドウにキーボードのフォーカスを当てる関数
                    wlr_seat_keyboard_notify_enter(
                    s1.seat,
                    v->toplevel->base->surface,// フォーカスを当てるサーフェス
                    keyboard->keycodes,      // 現在押されているキーの配列
                    keyboard->num_keycodes,  // 押されているキーの数
                    &keyboard->modifiers     // Shift/Ctrl などの修飾キーの状態
                    );
                }
            }
        }
    }
}
//新しいウィンドウが要求された時に呼ばれる関数
void server_new_xdg_toplevel(struct wl_listener *listener, void *data){
    struct wlr_xdg_toplevel *toplevel = data;
    struct view *v = calloc(1, sizeof(struct view));

    v->toplevel = toplevel;

    // --- ここではまだ configure や size 指定はしない ---

    // 1. リストへの挿入などはOK
    wl_list_insert(&s1.views, &v->link);
    v->x = 50;
    v->y = 50;

    // 2. マップ（表示開始）時のハンドラを登録
    v->map.notify = displaypush; // ここでサイズ指定などを行うように変更する
    wl_signal_add(&toplevel->base->surface->events.map, &v->map);

    // 3. アンマップ（非表示）時のハンドラ
    v->unmap.notify = displaypull;
    wl_signal_add(&toplevel->base->surface->events.unmap, &v->unmap);

    v->commit.notify = checkcomit;
    wl_signal_add(&toplevel->base->surface->events.commit, &v->commit);

    printf("new toplevel (initialized but not yet mapped)\n");
}

//ウィンドウの描画要求が来たときに呼ばれる関数
void checkcomit(struct wl_listener *listener, void *data){
   // リスナーから view 構造体を取り出す
    struct view *v = wl_container_of(listener, v, commit);
    if (v->toplevel->base->initial_commit) {
    wlr_xdg_toplevel_set_size(v->toplevel, 1000, 1000);
    wlr_xdg_surface_schedule_configure(v->toplevel->base);
    wlr_output_schedule_frame(s1.outputs);
    printf("window requested\n");
    }
}


void displaypush(struct wl_listener *listener, void *data){
    // リスナーから view 構造体を逆算して取り出す
    struct view *v = wl_container_of(listener, v, map);

    //取り出したview構造体からsurface構造体を取り出す
    struct wlr_surface *surface = v->toplevel->base->surface;

    // seat のキーボードフォーカスをこのサーフェスに設定する
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(s1.seat);

     if(keyboard){
        wlr_seat_keyboard_notify_enter(
            s1.seat,
            surface,// フォーカスを当てるサーフェス
            keyboard->keycodes,      // 現在押されているキーの配列
            keyboard->num_keycodes,  // 押されているキーの数
            &keyboard->modifiers     // Shift/Ctrl などの修飾キーの状態
        );
     }


    printf("window mapped and configured!\n");
}



 void displaypull(struct wl_listener *listener, void *data){

 }