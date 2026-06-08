#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include "vulkan_mywrap.h"
#include "keybord.h"
#include "pty_make.h"


// ホスト可視・コヒーレントなメモリタイプを探す
static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *memProps,
                                  uint32_t typeFilter,
                                  VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps->memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}


int window_init(struct windata* wd) {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    wd->window = glfwCreateWindow(800, 600, "Vulkan Window", NULL, NULL);
    if (!wd->window) {
        fprintf(stderr, "ウィンドウの生成に失敗しました。\n");
        glfwTerminate();
        return -1;
    }

    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "my_Terminal";
    appInfo.apiVersion = VK_API_VERSION_1_3;
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == NULL) {
        fprintf(stderr, "Vulkanに必要な拡張機能が取得できませんでした。\n");
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }

    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    uint32_t validationLayerCount = 1;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = validationLayerCount;
    createInfo.ppEnabledLayerNames = validationLayers;

    VkResult result = vkCreateInstance(&createInfo, NULL, &wd->instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "VkInstance の作成に失敗しました。エラーコード: %d\n", result);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }
    printf("VkInstance の作成に成功しました！\n");

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(wd->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fprintf(stderr, "Vulkanに対応したGPUが見つかりませんでした。\n");
        vkDestroyInstance(wd->instance, NULL);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }

    wd->devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(wd->instance, &deviceCount, wd->devices);
    VkPhysicalDevice physicalDevice = wd->devices[0];

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    printf("使用するGPU: %s\n", deviceProperties.deviceName);

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &wd->memProps);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);

    int graphicsFamilyIndex = -1;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamilyIndex = (int)i;
            break;
        }
    }
    free(queueFamilies);

    if (graphicsFamilyIndex == -1) {
        fprintf(stderr, "グラフィックス対応キューファミリーが見つかりませんでした。\n");
        vkDestroyInstance(wd->instance, NULL);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }
    printf("グラフィックス用キューファミリーのインデックス: %d\n", graphicsFamilyIndex);

    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsFamilyIndex;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo deviceCreateInfo = {0};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
    deviceCreateInfo.enabledLayerCount = validationLayerCount;
    deviceCreateInfo.ppEnabledLayerNames = validationLayers;

    VkResult deviceResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &wd->device);
    if (deviceResult != VK_SUCCESS) {
        fprintf(stderr, "論理デバイスの作成に失敗しました。エラーコード: %d\n", deviceResult);
        vkDestroyInstance(wd->instance, NULL);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }
    printf("論理デバイスの作成に成功しました!\n");

    vkGetDeviceQueue(wd->device, graphicsFamilyIndex, 0, &wd->graphicsQueue);

    VkResult surfaceResult = glfwCreateWindowSurface(wd->instance, wd->window, NULL, &wd->surface);
    if (surfaceResult != VK_SUCCESS) {
        fprintf(stderr, "ウィンドウサーフェスの作成に失敗しました。\n");
        vkDestroyDevice(wd->device, NULL);
        vkDestroyInstance(wd->instance, NULL);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }
    printf("ウィンドウサーフェスの作成に成功しました!\n");

    // スワップチェーン設定
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, wd->surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, wd->surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, wd->surface, &formatCount, formats);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, wd->surface, &presentModeCount, NULL);
    VkPresentModeKHR* presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, wd->surface, &presentModeCount, presentModes);

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = formats[i];
            break;
        }
    }

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPresentMode = presentModes[i];
            break;
        }
    }

    wd->chosenExtent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == 0xFFFFFFFF) {
        wd->chosenExtent.width  = 800;
        wd->chosenExtent.height = 600;
    }

    free(formats);
    free(presentModes);
    printf("スワップチェーン設定完了 (サイズ: %dx%d)\n",
           wd->chosenExtent.width, wd->chosenExtent.height);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;

    // TRANSFER_DST_BIT を追加することで vkCmdCopyBufferToImage が使えるようになる
    VkImageUsageFlags swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "警告: スワップチェーンがTRANSFER_DSTをサポートしていません\n");
        swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {0};
    swapchainCreateInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface          = wd->surface;
    swapchainCreateInfo.minImageCount    = imageCount;
    swapchainCreateInfo.imageFormat      = chosenFormat.format;
    swapchainCreateInfo.imageColorSpace  = chosenFormat.colorSpace;
    swapchainCreateInfo.imageExtent      = wd->chosenExtent;
    swapchainCreateInfo.presentMode      = chosenPresentMode;
    swapchainCreateInfo.imageUsage       = swapUsage;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform     = capabilities.currentTransform;
    swapchainCreateInfo.clipped          = VK_TRUE;
    swapchainCreateInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.oldSwapchain     = VK_NULL_HANDLE;

    VkResult swapchainResult = vkCreateSwapchainKHR(wd->device, &swapchainCreateInfo, NULL, &wd->swapchain);
    if (swapchainResult != VK_SUCCESS) {
        fprintf(stderr, "スワップチェーンの作成に失敗しました。エラーコード: %d\n", swapchainResult);
        vkDestroyDevice(wd->device, NULL);
        vkDestroySurfaceKHR(wd->instance, wd->surface, NULL);
        vkDestroyInstance(wd->instance, NULL);
        glfwDestroyWindow(wd->window);
        glfwTerminate();
        return -1;
    }
    printf("スワップチェーンの作成に成功しました!\n");

    vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, NULL);
    wd->swapchainImages = malloc(sizeof(VkImage) * wd->swapchainImageCount);
    vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, wd->swapchainImages);
    printf("スワップチェーン内の画像枚数: %d\n", wd->swapchainImageCount);

    wd->swapchainImageViews = malloc(sizeof(VkImageView) * wd->swapchainImageCount);
    for (uint32_t i = 0; i < wd->swapchainImageCount; i++) {
        VkImageViewCreateInfo viewInfo = {0};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = wd->swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = chosenFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(wd->device, &viewInfo, NULL, &wd->swapchainImageViews[i]) != VK_SUCCESS) {
            fprintf(stderr, "%d番目のイメージビューの作成に失敗しました。\n", i);
            return -1;
        }
    }
    printf("イメージビューの作成に成功しました!\n");

    // コマンドプール
    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(wd->device, &poolInfo, NULL, &wd->commandPool) != VK_SUCCESS) {
        fprintf(stderr, "コマンドプールの作成に失敗しました。\n");
        return -1;
    }
    printf("コマンドプールの作成に成功しました!\n");

    wd->commandBuffers = malloc(sizeof(VkCommandBuffer) * wd->swapchainImageCount);
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = wd->commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = wd->swapchainImageCount;
    if (vkAllocateCommandBuffers(wd->device, &allocInfo, wd->commandBuffers) != VK_SUCCESS) {
        fprintf(stderr, "コマンドバッファの割り当てに失敗しました。\n");
        return -1;
    }
    printf("コマンドバッファの割り当てに成功しました!\n");

    // 同期オブジェクト
    VkSemaphoreCreateInfo semInfo = {0};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateSemaphore(wd->device, &semInfo, NULL, &wd->imageAvailableSemaphore);
    vkCreateSemaphore(wd->device, &semInfo, NULL, &wd->renderFinishedSemaphore);
    vkCreateFence(wd->device, &fenceInfo, NULL, &wd->inFlightFence);

    // ステージングバッファ作成（スクリーン全体のBGRA画像を保持）
    wd->stagingSize = wd->chosenExtent.width * wd->chosenExtent.height * 4;

    VkBufferCreateInfo bufInfo = {0};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = wd->stagingSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(wd->device, &bufInfo, NULL, &wd->stagingBuffer) != VK_SUCCESS) {
        fprintf(stderr, "ステージングバッファの作成に失敗しました。\n");
        return -1;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(wd->device, wd->stagingBuffer, &memReq);

    uint32_t memTypeIdx = find_memory_type(&wd->memProps, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memTypeIdx == UINT32_MAX) {
        fprintf(stderr, "適切なメモリタイプが見つかりませんでした。\n");
        return -1;
    }

    VkMemoryAllocateInfo memAllocInfo = {0};
    memAllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize  = memReq.size;
    memAllocInfo.memoryTypeIndex = memTypeIdx;
    if (vkAllocateMemory(wd->device, &memAllocInfo, NULL, &wd->stagingMemory) != VK_SUCCESS) {
        fprintf(stderr, "ステージングメモリの確保に失敗しました。\n");
        return -1;
    }
    vkBindBufferMemory(wd->device, wd->stagingBuffer, wd->stagingMemory, 0);
    vkMapMemory(wd->device, wd->stagingMemory, 0, wd->stagingSize, 0, &wd->stagingMapped);
    printf("ステージングバッファの作成に成功しました! (%u bytes)\n", wd->stagingSize);

    return 0;
}


void set_window(struct windata* wd) {
    glfwSetWindowTitle(wd->window, "bash");
    glfwShowWindow(wd->window);
}

void destroy_data(struct windata* wd)
{
    vkDeviceWaitIdle(wd->device);

    vkUnmapMemory(wd->device, wd->stagingMemory);
    vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
    vkFreeMemory(wd->device, wd->stagingMemory, NULL);

    vkDestroySemaphore(wd->device, wd->renderFinishedSemaphore, NULL);
    vkDestroySemaphore(wd->device, wd->imageAvailableSemaphore, NULL);
    vkDestroyFence(wd->device, wd->inFlightFence, NULL);

    vkDestroyCommandPool(wd->device, wd->commandPool, NULL);

    for (uint32_t i = 0; i < wd->swapchainImageCount; i++)
        vkDestroyImageView(wd->device, wd->swapchainImageViews[i], NULL);

    free(wd->commandBuffers);
    free(wd->swapchainImageViews);
    free(wd->swapchainImages);

    vkDestroySwapchainKHR(wd->device, wd->swapchain, NULL);
    vkDestroyDevice(wd->device, NULL);
    vkDestroySurfaceKHR(wd->instance, wd->surface, NULL);
    vkDestroyInstance(wd->instance, NULL);
    glfwDestroyWindow(wd->window);
    glfwTerminate();
    free(wd->devices);

    exit(1);
}


int set_kbd_callback(struct windata* wd) {
    glfwSetKeyCallback(wd->window, key_callback);
    glfwSetCharCallback(wd->window, character_callback);
    return 0;
}


// term_cell の全セルを BGRA ピクセルとしてステージングバッファに描画する
void render_cells_to_buffer(struct windata *wd)
{
    struct term_context *ctx = wd->ctx;
    if (!ctx || !wd->stagingMapped) return;

    uint8_t *buf  = (uint8_t *)wd->stagingMapped;
    int sw        = (int)wd->chosenExtent.width;
    int sh        = (int)wd->chosenExtent.height;
    int cell_w    = ctx->cell_w;
    int cell_h    = ctx->cell_h;
    int ascender  = wd->font_ascender;

    // 画面を黒でクリア
    memset(buf, 0, (size_t)(sw * sh * 4));

    for (int row = 0; row < ctx->term_size.h; row++) {
        for (int col = 0; col < ctx->term_size.w; col++) {
            struct term_cell *cell =
                &ctx->term_cell[row * ctx->term_size.w + col];

            int base_x = col * cell_w;
            int base_y = row * cell_h;

            // 背景色で塗りつぶす
            for (int py = 0; py < cell_h; py++) {
                int sy = base_y + py;
                if (sy >= sh) break;
                for (int px = 0; px < cell_w; px++) {
                    int sx = base_x + px;
                    if (sx >= sw) break;
                    int idx = (sy * sw + sx) * 4;
                    buf[idx + 0] = cell->bg_color.b;
                    buf[idx + 1] = cell->bg_color.g;
                    buf[idx + 2] = cell->bg_color.r;
                    buf[idx + 3] = 255;
                }
            }

            // グリフを描画（スペース・範囲外はスキップ）
            int c = cell->character;
            if (c < 32 || c > 126) continue;
            struct glyph_data *g = &wd->glyphs[c];
            if (!g->bitmap || g->width == 0 || g->height == 0) continue;

            // ベースライン基準でグリフの左上ピクセル座標を計算
            int glyph_x = base_x + g->bearing_x;
            int glyph_y = base_y + (ascender - g->bearing_y);

            for (int gy = 0; gy < g->height; gy++) {
                int sy = glyph_y + gy;
                if (sy < 0 || sy >= sh) continue;
                for (int gx = 0; gx < g->width; gx++) {
                    int sx = glyph_x + gx;
                    if (sx < 0 || sx >= sw) continue;
                    uint8_t alpha = g->bitmap[gy * g->width + gx];
                    if (alpha == 0) continue;
                    int idx = (sy * sw + sx) * 4;
                    uint8_t inv = 255 - alpha;
                    // アルファブレンド: fg * alpha + bg * (1-alpha)
                    buf[idx + 0] = (uint8_t)((cell->fg_color.b * alpha + buf[idx + 0] * inv) / 255);
                    buf[idx + 1] = (uint8_t)((cell->fg_color.g * alpha + buf[idx + 1] * inv) / 255);
                    buf[idx + 2] = (uint8_t)((cell->fg_color.r * alpha + buf[idx + 2] * inv) / 255);
                    buf[idx + 3] = 255;
                }
            }
        }
    }

    // カーソルを反転色で描画
    int cx = ctx->cur->cur_pos.w * cell_w;
    int cy = ctx->cur->cur_pos.h * cell_h;
    for (int py = 0; py < cell_h; py++) {
        int sy = cy + py;
        if (sy >= sh) break;
        for (int px = 0; px < cell_w; px++) {
            int sx = cx + px;
            if (sx >= sw) break;
            int idx = (sy * sw + sx) * 4;
            buf[idx + 0] ^= 0xFF;
            buf[idx + 1] ^= 0xFF;
            buf[idx + 2] ^= 0xFF;
        }
    }
}
