#include <bits/time.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<time.h>
#include <pango-1.0/pango/pangocairo.h>
#include <sys/syscall.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include<wayland-client.h>
#include <linux/memfd.h>
#include <sys/mman.h> 
#include <syscall.h>
#include<sys/stat.h>
#include<sys/epoll.h>
#include<sys/timerfd.h> 
#include <math.h>
#include"wlr-layer-shell-unstable-v1-client-protocol.h"
#include"mybar.h"

#define RADIUS 150
#define N_WORDS 10
#define FONT "Sans Bold 27"
#define SCALE 2  // HiDPI scale factor (1 = normal, 2 = 2x resolution)




typedef struct{
    struct wl_output* output;
    struct wl_shm* shm;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct{
      int w;
      int h;
    }surface_size;
}App;

void draw_wifi(cairo_t *cr, double x, double y, int bars) {
    double cx = x + 12, cy = y + 16;
    double radii[] = {4, 7, 10, 14};
    cairo_set_line_width(cr, 2.0);

    // 全アークを暗く描いてWiFiの輪郭を表示
    cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
    for (int i = 0; i < 4; i++) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, radii[i], -M_PI * 0.75, -M_PI * 0.25);
        cairo_stroke(cr);
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, 2, 0, 2 * M_PI);
    cairo_fill(cr);

    // 強度分だけ白で上書き
    cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
    for (int i = 0; i < bars; i++) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, radii[i], -M_PI * 0.75, -M_PI * 0.25);
        cairo_stroke(cr);
    }
    if (bars > 0) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 2, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

void redraw(cairo_t *cr, cairo_surface_t *cs,
            struct wl_surface *surface, struct wl_buffer *buffer,
            int w, int h)
{
    // 背景クリア
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1);
    cairo_paint(cr);

    // 時刻取得・描画
    time_t t = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, buf, -1);
    PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 14");

  
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, 10, 3);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    // バッテリー残量表示処理
    static int capacity = -1;
    static int battery_tick = 0;
    if (capacity < 0 || ++battery_tick >= 15) {
        battery_tick = 0;
        char bbuf[8] = {0};
        int fd = open("/sys/class/power_supply/BAT1/capacity", O_RDONLY);
        if (fd >= 0) {
            int len = read(fd, bbuf, sizeof(bbuf) - 1);
            if (len > 0) { bbuf[len] = '\0'; capacity = atoi(bbuf); }
            close(fd);
        }
    }

    int bat_text_w = 0;
    if (capacity >= 0) {
        char bat_str[16];
        snprintf(bat_str, sizeof(bat_str), "BAT %d%%", capacity);
        PangoLayout *bat_layout = pango_cairo_create_layout(cr);
        PangoFontDescription *bat_desc = pango_font_description_from_string("Sans Bold 12");
        pango_layout_set_text(bat_layout, bat_str, -1);
        pango_layout_set_font_description(bat_layout, bat_desc);
        pango_font_description_free(bat_desc);

        int text_h;
        pango_layout_get_pixel_size(bat_layout, &bat_text_w, &text_h);

        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_move_to(cr, w - bat_text_w - 10, 3);
        pango_cairo_show_layout(cr, bat_layout);
        g_object_unref(bat_layout);
    }

    // WiFi強度表示処理 (dBm: -999=未初期化, 0=未接続, 負値=実測値)
    static int wifi_dbm = -999;
    static int wifi_tick = 0;
    if (wifi_dbm == -999 || ++wifi_tick >= 5) {
        wifi_tick = 0;
        wifi_dbm = 0;
        FILE *wf = popen("iw dev wlan0 link 2>/dev/null", "r");
        if (wf) {
            char line[256];
            while (fgets(line, sizeof(line), wf)) {
                int sig;
                if (sscanf(line, " signal: %d dBm", &sig) == 1) {
                    wifi_dbm = sig;
                    break;
                }
            }
            pclose(wf);
        }
    }

    int bars = wifi_dbm >= 0   ? 0 :
               wifi_dbm <= -80 ? 1 :
               wifi_dbm <= -70 ? 2 :
               wifi_dbm <= -60 ? 3 : 4;
    draw_wifi(cr, w - bat_text_w - 40, 2, bars);

    // コミット
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, w * SCALE, h * SCALE);
    wl_surface_commit(surface);
}





static void handle_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width, uint32_t height)
{
    App *app = data;
    if (app != NULL) {
        if (width > 0) {
            app->surface_size.w = (int)width;
        }
        if (height > 0) {
            app->surface_size.h = (int)height;
        }
    }
    printf("configure: %ux%u\n", width, height);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void handle_layer_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_configure,
    .closed = handle_layer_closed,
};


static void handle_global(void* data, struct wl_registry* registry,
                          uint32_t name, const char *interface, uint32_t version)
{
  App* app = (App*)data;
  if (strcmp(interface, wl_output_interface.name) == 0 && version >= 2) {
    printf("bind wl_output\n");
    app->output = (struct wl_output*)(wl_registry_bind(registry, name, &wl_output_interface, 2));
  }
  else if(strcmp(interface,wl_compositor_interface.name) == 0){
    printf("bind compositor\n");
    app->compositor = (struct wl_compositor*)(wl_registry_bind(registry, name, &wl_compositor_interface,version));
  }
  else if(strcmp(interface,wl_shm_interface.name) == 0){
    printf("bind shm\n");
    app->shm = (struct wl_shm*)(wl_registry_bind(registry, name, &wl_shm_interface,version));
  }
  else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    printf("bind layer_shell\n");
    app->layer_shell = (struct zwlr_layer_shell_v1*)(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version));
  }


}

static void handle_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    
}


int mybar_start(void)
{

    struct wl_display* display = wl_display_connect(NULL);
    if (display == NULL) {
        return 1;
    }
    static const struct wl_registry_listener registry_listener = {
    handle_global,
    handle_global_remove,
  };

  App cliant = {0};

  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &cliant);

  wl_display_roundtrip(display); // ① bind用のroundtrip → ここでcompositor等が埋まる
  if (cliant.compositor == NULL || cliant.shm == NULL || cliant.layer_shell == NULL) {
    return 1;
  }

  struct wl_surface *surface = wl_compositor_create_surface(cliant.compositor);

  /*第3引数の output は NULL でOK（compositorに任せる＝デフォルトのモニターに表示）
  "mybar" は識別用の名前空間（compositor側でこのバーを識別するための文字列）*/
  struct zwlr_layer_surface_v1 *layer_surface =
    zwlr_layer_shell_v1_get_layer_surface(
        cliant.layer_shell, surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "mybar");

  /*zwlr_layer_surface_v1_set_anchor は、
  このレイヤーサーフェス（バー）を画面のどの辺にくっつけるかを指定するリクエストです。
  各値はビットフラグになっていて、ORで組み合わせます。*/
  zwlr_layer_surface_v1_set_anchor(layer_surface,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

  cliant.surface_size.h = 30;
  cliant.surface_size.w = 0;

  zwlr_layer_surface_v1_set_size(layer_surface, cliant.surface_size.w, cliant.surface_size.h); // 幅は0=画面幅に合わせる, 高さ30px
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, 30); // 他ウィンドウをこの分押し下げる

  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, &cliant);

  wl_surface_commit(surface);    // ② configureイベントのトリガー
  wl_display_roundtrip(display); // ③ configure受信用のroundtrip

  int bar_w    = (cliant.surface_size.w > 0 ? cliant.surface_size.w : 1920) * SCALE;
  int bar_h    = cliant.surface_size.h * SCALE;
  int stride   = bar_w * 4;        // 1ピクセル = ARGB8888 = 4バイト
  int shm_size = stride * bar_h;   // バッファ全体のバイト数

  int fd = (int)syscall(SYS_memfd_create,"mybar-shm",MFD_CLOEXEC);
  ftruncate(fd, shm_size); 

  void *shm_data = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  struct wl_shm_pool *pool = wl_shm_create_pool(cliant.shm, fd, shm_size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
    pool, 0, bar_w, bar_h, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);  // pool作成後は不要


  wl_surface_set_buffer_scale(surface, SCALE);
  wl_surface_attach(surface, buffer, 0, 0);
    // ① Cairo で shm_data に色を塗る（これがないと真っ黒か壊れた表示）
  cairo_surface_t *cs = cairo_image_surface_create_for_data(
      shm_data, CAIRO_FORMAT_ARGB32, bar_w, bar_h, stride);
  cairo_surface_set_device_scale(cs, SCALE, SCALE);
  cairo_t *cr = cairo_create(cs);
  cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
  cairo_paint(cr);

  // ② バッファをサーフェスに貼り付けてコミット（これがないと画面に出ない）
  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, bar_w, bar_h);
  wl_surface_commit(surface);

  int timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
  struct itimerspec its = {0};
  its.it_value.tv_sec = 1;
  its.it_interval.tv_sec = 1;

  timerfd_settime(timer_fd, 0, &its, NULL);

  int ev_max = 16;
  /* epoll_waitの結果の格納先 */
  struct epoll_event events[ev_max];
  /* epollインスタンスを参照するファイルディスクリプタ */
  int epfd;
  /* ファイルディスクリプタと紐付けるイベント情報 */
  struct epoll_event event;

  epfd = epoll_create(ev_max);
  if ( epfd < 0)
  {
    /* エラー処理 */
  }
  memset(&event, 0, sizeof(struct epoll_event)); /* イベント情報の初期化 */
  event.events = EPOLLIN;    /* 入力待ち（読み込み待ち） */
  event.data.fd = timer_fd;

  /* epollインスタンスに、sd_listenと上記のイベント情報とを追加する */
  epoll_ctl(epfd, EPOLL_CTL_ADD,timer_fd,&event);
  //ループ内のredrawは監視開始後１秒後からしか実行されないので
  redraw(cr, cs, surface, buffer, bar_w / SCALE, bar_h / SCALE);
  // ③ イベントループ（これがないと即終了して消える）
  while (wl_display_dispatch(display) != -1) {
    int ndfs = epoll_wait(epfd,events,ev_max,-1);
    if(ndfs > 0){
       for (int i = 0; i < ndfs; i++) {
        if (events[i].data.fd == timer_fd) {
          uint64_t exp;
          if (read(timer_fd, &exp, sizeof(exp)) == sizeof(exp)) {
              redraw(cr, cs, surface, buffer, bar_w / SCALE, bar_h / SCALE);
          }
        }
      }
    }
  }

  cairo_destroy(cr);
  cairo_surface_destroy(cs);

  



  return 0;
}



int main(void)
{
  return mybar_start();
}
