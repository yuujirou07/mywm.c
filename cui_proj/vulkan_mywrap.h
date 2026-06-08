#ifndef VULKAN_MYWRAP_H
#define VULKAN_MYWRAP_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include "vulkan_otf_draw.h"

struct term_context;

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

    // フォント情報（レンダリングに使用）
    struct glyph_data glyphs[128];
    int font_ascender;
};


int window_init(struct windata* wd);
void destroy_data(struct windata* wd);
void set_window(struct windata* wd);
void render_cells_to_buffer(struct windata *wd);

#endif
