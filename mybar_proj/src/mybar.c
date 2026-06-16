#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include <pango-1.0/pango/pangocairo.h>
#include <sys/syscall.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include<wayland-client.h>
#include <linux/memfd.h>
#include <sys/mman.h> 
#include <syscall.h>
#include<sys/stat.h>
#include"wlr-layer-shell-unstable-v1-client-protocol.h"
#include"mybar.h"

#define RADIUS 150
#define N_WORDS 10
#define FONT "Sans Bold 27"




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




static void handle_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width, uint32_t height)
{
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
    static const struct wl_registry_listener registry_listener = {
    handle_global,
    handle_global_remove,
  };

  App cliant = {0};

  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &cliant);

  wl_display_roundtrip(display); // ① bind用のroundtrip → ここでcompositor等が埋まる

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
  cliant.surface_size.w = 100;

  zwlr_layer_surface_v1_set_size(layer_surface, cliant.surface_size.w, cliant.surface_size.h); // 幅は0=画面幅に合わせる, 高さ30px
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, 30); // 他ウィンドウをこの分押し下げる

  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, NULL);

  wl_surface_commit(surface);    // ② configureイベントのトリガー
  wl_display_roundtrip(display); // ③ configure受信用のroundtrip

  int bar_w    = cliant.surface_size.w > 0 ? cliant.surface_size.w : 1920;
  int bar_h    = cliant.surface_size.h;
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


  wl_surface_attach(surface, buffer, 0, 0);
    // ① Cairo で shm_data に色を塗る（これがないと真っ黒か壊れた表示）
  cairo_surface_t *cs = cairo_image_surface_create_for_data(
      shm_data, CAIRO_FORMAT_ARGB32, bar_w, bar_h, stride);
  cairo_t *cr = cairo_create(cs);
  cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(cs);

  // ② バッファをサーフェスに貼り付けてコミット（これがないと画面に出ない）
  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, bar_w, bar_h);
  wl_surface_commit(surface);

  // ③ イベントループ（これがないと即終了して消える）
  while (wl_display_dispatch(display) != -1) {
  }



  return 0;
}