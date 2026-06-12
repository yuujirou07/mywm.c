#ifndef VULKAN_MYWRAP_H
#define VULKAN_MYWRAP_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include "vulkan_otf_draw.h"

struct term_context;

// フォントサイズ変更用の定数（cell_hを基準にcell_wは半分の比率で追従させる）
#define FONT_CELL_H_MIN  8
#define FONT_CELL_H_MAX  48
#define FONT_CELL_H_STEP 2

struct windata
{
    int master_fd;
    int *nfds;
    struct term_context *ctx;

    struct {
        bool write_buff_overflow;
        struct epoll_event *epoll;
        struct epoll_event *master_fd_ev_poll;
        const char *clip_bord_chr;
        int *epoll_fd_list;
        int cftl_c_sig_counter;
    } kbd_data;

    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice* devices;
    VkDevice device;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    VkExtent2D chosenExtent;
    VkExtent2D renderExtent;

    VkSwapchainKHR swapchain;
    uint32_t swapchainImageCount;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;

    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    // CPU→GPU 転送用ステージングバッファ（スクリーン全体のBGRA画像）
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    void *stagingMapped;
    uint32_t stagingSize;
    VkPhysicalDeviceMemoryProperties memProps;
    VkFormat swapchainImageFormat;

    // フォント情報（レンダリングに使用）
    struct glyph_data glyphs[128];
    int font_ascender;

    // フォントサイズが変更され、term_sizeの再計算が必要なことを示すフラグ
    bool font_size_changed;

    // 差分描画用：直前フレームのセル内容・カーソル位置・画面構成
    struct term_cell *prev_term_cell;
    struct pos prev_term_size;
    int prev_cur_col;
    int prev_cur_row;
    int prev_cell_w;
    int prev_cell_h;
    int prev_sw;
    int prev_sh;
};


int window_init(struct windata* wd);
int recreate_swapchain(struct windata *wd);
void destroy_data(struct windata* wd);
void set_window(struct windata* wd);
void render_cells_to_buffer(struct windata *wd);
void change_font_size(struct windata *wd, int delta);

#endif
