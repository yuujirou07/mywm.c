#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include "vulkan_mywrap.h"
#include "keybord.h"
#include "pty_make.h"
#include "codepoint_comb.h"
#include "error_log_output.h"


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


// window_init(): GLFWウィンドウ、Vulkanインスタンス、デバイス、スワップチェーン、
// ステージングバッファなど描画に必要な状態をまとめて初期化する。
int window_init(struct windata* wd) {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);


    const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

    if(mode == NULL)
    {
        error_log_write("mode get error");
        return 1;
    }
    
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

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
    wd->swapchainImageFormat = chosenFormat.format;

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPresentMode = presentModes[i];
            break;
        }
    }

    int fb_w, fb_h;
    glfwGetFramebufferSize(wd->window, &fb_w, &fb_h);
    wd->chosenExtent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == 0xFFFFFFFF) {
        wd->chosenExtent.width  = (uint32_t)fb_w;
        wd->chosenExtent.height = (uint32_t)fb_h;
    }
    wd->renderExtent = wd->chosenExtent;

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


int recreate_swapchain(struct windata *wd)
{
    vkDeviceWaitIdle(wd->device);

    // コマンドバッファを解放
    vkFreeCommandBuffers(wd->device, wd->commandPool,
                         wd->swapchainImageCount, wd->commandBuffers);
    free(wd->commandBuffers);
    wd->commandBuffers = NULL;

    // イメージビューを解放
    for (uint32_t i = 0; i < wd->swapchainImageCount; i++)
        vkDestroyImageView(wd->device, wd->swapchainImageViews[i], NULL);
    free(wd->swapchainImageViews);
    free(wd->swapchainImages);
    wd->swapchainImageViews = NULL;
    wd->swapchainImages = NULL;

    VkPhysicalDevice physicalDevice = wd->devices[0];

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, wd->surface, &capabilities);

    // chosenExtent/renderExtent を新しいウィンドウサイズに更新
    int fb_w, fb_h;
    glfwGetFramebufferSize(wd->window, &fb_w, &fb_h);
    wd->chosenExtent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == 0xFFFFFFFF) {
        wd->chosenExtent.width  = (uint32_t)fb_w;
        wd->chosenExtent.height = (uint32_t)fb_h;
    }
    if (wd->chosenExtent.width  < capabilities.minImageExtent.width)  wd->chosenExtent.width  = capabilities.minImageExtent.width;
    if (wd->chosenExtent.width  > capabilities.maxImageExtent.width)  wd->chosenExtent.width  = capabilities.maxImageExtent.width;
    if (wd->chosenExtent.height < capabilities.minImageExtent.height) wd->chosenExtent.height = capabilities.minImageExtent.height;
    if (wd->chosenExtent.height > capabilities.maxImageExtent.height) wd->chosenExtent.height = capabilities.maxImageExtent.height;
    wd->renderExtent = wd->chosenExtent;

    // ステージングバッファは新サイズが現在の確保済みサイズを超えた時だけ再確保
    uint32_t newStagingSize = wd->chosenExtent.width * wd->chosenExtent.height * 4;
    if (newStagingSize > wd->stagingSize) {
        vkUnmapMemory(wd->device, wd->stagingMemory);
        vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
        vkFreeMemory(wd->device, wd->stagingMemory, NULL);
        wd->stagingMapped = NULL;

        wd->stagingSize = newStagingSize;
        VkBufferCreateInfo bufInfo = {0};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = wd->stagingSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(wd->device, &bufInfo, NULL, &wd->stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(wd->device, wd->stagingBuffer, &memReq);
        uint32_t memTypeIdx = find_memory_type(&wd->memProps, memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo memAllocInfo = {0};
        memAllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAllocInfo.allocationSize  = memReq.size;
        memAllocInfo.memoryTypeIndex = memTypeIdx;
        vkAllocateMemory(wd->device, &memAllocInfo, NULL, &wd->stagingMemory);
        vkBindBufferMemory(wd->device, wd->stagingBuffer, wd->stagingMemory, 0);
        vkMapMemory(wd->device, wd->stagingMemory, 0, wd->stagingSize, 0, &wd->stagingMapped);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;

    VkImageUsageFlags swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkSwapchainKHR oldSwapchain = wd->swapchain;

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {0};
    swapchainCreateInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface          = wd->surface;
    swapchainCreateInfo.minImageCount    = imageCount;
    swapchainCreateInfo.imageFormat      = wd->swapchainImageFormat;
    swapchainCreateInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainCreateInfo.imageExtent      = wd->chosenExtent;
    swapchainCreateInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreateInfo.imageUsage       = swapUsage;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform     = capabilities.currentTransform;
    swapchainCreateInfo.clipped          = VK_TRUE;
    swapchainCreateInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.oldSwapchain     = oldSwapchain;

    VkResult result = vkCreateSwapchainKHR(wd->device, &swapchainCreateInfo, NULL, &wd->swapchain);
    vkDestroySwapchainKHR(wd->device, oldSwapchain, NULL);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "スワップチェーンの再作成に失敗しました: %d\n", result);
        return -1;
    }

    vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, NULL);
    wd->swapchainImages = malloc(sizeof(VkImage) * wd->swapchainImageCount);
    vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, wd->swapchainImages);

    wd->swapchainImageViews = malloc(sizeof(VkImageView) * wd->swapchainImageCount);
    for (uint32_t i = 0; i < wd->swapchainImageCount; i++) {
        VkImageViewCreateInfo viewInfo = {0};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = wd->swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = wd->swapchainImageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(wd->device, &viewInfo, NULL, &wd->swapchainImageViews[i]) != VK_SUCCESS) {
            fprintf(stderr, "%d番目のイメージビューの再作成に失敗しました。\n", i);
            return -1;
        }
    }

    wd->commandBuffers = malloc(sizeof(VkCommandBuffer) * wd->swapchainImageCount);
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = wd->commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = wd->swapchainImageCount;
    if (vkAllocateCommandBuffers(wd->device, &allocInfo, wd->commandBuffers) != VK_SUCCESS) {
        fprintf(stderr, "コマンドバッファの再割り当てに失敗しました。\n");
        return -1;
    }

    return 0;
}


// set_window(): ウィンドウタイトルを設定して画面に表示する。
void set_window(struct windata* wd) {
    glfwSetWindowTitle(wd->window, "bash");
    glfwShowWindow(wd->window);
}

// destroy_data(): Vulkan/GLFW関連のリソースを破棄してプログラムを終了する。
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

    free(wd->prev_term_cell);

    vkDestroySwapchainKHR(wd->device, wd->swapchain, NULL);
    vkDestroyDevice(wd->device, NULL);
    vkDestroySurfaceKHR(wd->instance, wd->surface, NULL);
    vkDestroyInstance(wd->instance, NULL);
    glfwDestroyWindow(wd->window);
    glfwTerminate();
    free(wd->devices);

    exit(1);
}


// set_kbd_callback(): GLFWにキー入力と文字入力のコールバックを登録する。
int set_kbd_callback(struct windata* wd) {
    glfwSetKeyCallback(wd->window, key_callback);
    glfwSetCharCallback(wd->window, character_callback);
    return 0;
}


// セル1個分の背景とグリフをステージングバッファへ描画する
static void draw_cell_pixels(uint8_t *buf, int sw, int sh,
                              int base_x, int base_y, int cell_w, int cell_h,
                              int ascender, const struct term_cell *cell,
                              struct glyph_data *glyphs)
{   
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
    // 罫線・ブロック素片(U+2500〜U+259F)はフォントを持たないため、
    // コードポイントから直接ピクセル描画する（nvim等の枠線表示用）
    if (is_box_codepoint(c)) {
        draw_box_codepoint(buf, sw, sh, base_x, base_y, cell_w, cell_h,
                           c, cell->fg_color);
        return;
    }
    if (c < 32 || c > 126) return;
    struct glyph_data *g = &glyphs[c];
    if (!g->bitmap || g->width == 0 || g->height == 0) return;

    // 2x読み込みなので座標・サイズを1/2に換算
    int glyph_x = base_x + g->bearing_x / 2;
    int glyph_y = base_y + (ascender / 2 - g->bearing_y / 2);
    int half_w  = g->width  / 2;
    int half_h  = g->height / 2;

    for (int gy = 0; gy < half_h; gy++) {
        int sy = glyph_y + gy;
        // 隣接セルへ色がにじむと差分描画で消えずに残ってしまうため、
        // 自セルの矩形範囲外は描画しない
        if (sy < base_y || sy >= base_y + cell_h || sy >= sh) continue;
        for (int gx = 0; gx < half_w; gx++) {
            int sx = glyph_x + gx;
            if (sx < base_x || sx >= base_x + cell_w || sx >= sw) continue;
            // 2x2ブロックを平均してアルファ値を算出
            int bx = gx * 2, by = gy * 2;
            int a = (int)g->bitmap[by       * g->width + bx]
                  + (int)g->bitmap[by       * g->width + bx + 1]
                  + (int)g->bitmap[(by + 1) * g->width + bx]
                  + (int)g->bitmap[(by + 1) * g->width + bx + 1];
            a /= 4;
            if (a == 0) continue;
            int idx = (sy * sw + sx) * 4;
            buf[idx + 0] = (uint8_t)(buf[idx + 0] + (cell->fg_color.b - buf[idx + 0]) * a / 255);
            buf[idx + 1] = (uint8_t)(buf[idx + 1] + (cell->fg_color.g - buf[idx + 1]) * a / 255);
            buf[idx + 2] = (uint8_t)(buf[idx + 2] + (cell->fg_color.r - buf[idx + 2]) * a / 255);
            buf[idx + 3] = 255;
        }
    }
}

// セル1個分の領域を反転色にする（カーソル表示用）
static void invert_cell_pixels(uint8_t *buf, int sw, int sh,
                                int base_x, int base_y, int cell_w, int cell_h)
{
    for (int py = 0; py < cell_h; py++) {
        int sy = base_y + py;
        if (sy >= sh) break;
        for (int px = 0; px < cell_w; px++) {
            int sx = base_x + px;
            if (sx >= sw) break;
            int idx = (sy * sw + sx) * 4;
            buf[idx + 0] ^= 0xFF;
            buf[idx + 1] ^= 0xFF;
            buf[idx + 2] ^= 0xFF;
        }
    }
}

// 描画結果に影響する項目（文字・前景色・背景色）が一致しているかを判定する
static bool cell_visually_equal(const struct term_cell *a, const struct term_cell *b)
{
    return a->character == b->character &&
           memcmp(&a->fg_color, &b->fg_color, sizeof(Color)) == 0 &&
           memcmp(&a->bg_color, &b->bg_color, sizeof(Color)) == 0;
}

// 描画ワーカーに渡すパラメータ。行範囲[row_start, row_end)だけを担当する。
// 各スレッドは互いに重ならないピクセル行とprev_term_cellエントリにしか
// 書き込まないため、ロック無しで並列描画できる。
struct render_task {
    struct windata *wd;
    uint8_t *buf;
    int sw, sh, cell_w, cell_h, ascender, term_w;
    int cur_col, cur_row, prev_cur_col, prev_cur_row;
    bool full_redraw;
    int row_start, row_end;
};

// 指定された行範囲のセルを描画する（1スレッド分の仕事）
static void render_rows(struct render_task *t)
{
    struct term_context *ctx = t->wd->ctx;
    for (int row = t->row_start; row < t->row_end; row++) {
        for (int col = 0; col < t->term_w; col++) {
            int idx = row * t->term_w + col;
            struct term_cell *cell = &ctx->term_cell[idx];
            struct term_cell *prev = &t->wd->prev_term_cell[idx];

            bool is_new_cursor = (col == t->cur_col && row == t->cur_row);
            // カーソルが移動した場合、移動元のセルを反転無しで再描画して元に戻す
            bool is_old_cursor = !t->full_redraw &&
                col == t->prev_cur_col && row == t->prev_cur_row && !is_new_cursor;

            if (!t->full_redraw && !is_new_cursor && !is_old_cursor &&
                cell_visually_equal(cell, prev)) {
                continue;
            }

            int base_x = col * t->cell_w;
            int base_y = row * t->cell_h;

            draw_cell_pixels(t->buf, t->sw, t->sh, base_x, base_y, t->cell_w, t->cell_h,
                             t->ascender, cell, t->wd->glyphs);

            if (is_new_cursor) {
                invert_cell_pixels(t->buf, t->sw, t->sh, base_x, base_y, t->cell_w, t->cell_h);
            }

            *prev = *cell;
        }
    }
}

// render_rows_thread(): pthread用の入口。受け取った行範囲をrender_rows()で描画する。
static void *render_rows_thread(void *arg)
{
    render_rows((struct render_task *)arg);
    return NULL;
}

// term_cell のうち、前フレームから変化したセル（とカーソルの移動元/移動先）
// だけを再描画してステージングバッファを更新する。
// 全画面再描画(full_redraw)はリサイズ中に毎フレーム発生し文字数に比例して重いため、
// その場合だけ行範囲を複数スレッドへ分割して並列描画する。
void render_cells_to_buffer(struct windata *wd)
{
    struct term_context *ctx = wd->ctx;
    if (!ctx || !wd->stagingMapped) return;

    uint8_t *buf    = (uint8_t *)wd->stagingMapped;
    int sw          = (int)wd->renderExtent.width;
    int sh          = (int)wd->renderExtent.height;
    int cell_w      = ctx->cell_w;
    int cell_h      = ctx->cell_h;
    int ascender    = wd->font_ascender;
    int term_w      = ctx->term_size.w;
    int term_h      = ctx->term_size.h;
    int cell_count  = term_w * term_h;

    // 画面サイズ・セルサイズ・フォントが変わった場合は全画面を再描画する
    bool full_redraw = !wd->prev_term_cell ||
        wd->prev_term_size.w != term_w || wd->prev_term_size.h != term_h ||
        wd->prev_cell_w != cell_w || wd->prev_cell_h != cell_h ||
        wd->prev_sw != sw || wd->prev_sh != sh;

    if (full_redraw) {
        free(wd->prev_term_cell);
        wd->prev_term_cell = malloc(sizeof(struct term_cell) * (size_t)cell_count);
        memset(buf, 0, (size_t)(sw * sh * 4));
    }

    int cur_col = ctx->cur->cur_pos.w;
    int cur_row = ctx->cur->cur_pos.h;

    struct render_task base = {
        .wd = wd, .buf = buf, .sw = sw, .sh = sh,
        .cell_w = cell_w, .cell_h = cell_h, .ascender = ascender, .term_w = term_w,
        .cur_col = cur_col, .cur_row = cur_row,
        .prev_cur_col = wd->prev_cur_col, .prev_cur_row = wd->prev_cur_row,
        .full_redraw = full_redraw,
    };

    // スレッド数を決定。全画面再描画かつ行数が十分ある時だけ並列化する。
    // 差分描画(通常のタイプ時など)は変化セルが少なくスレッド生成の方が高くつくため1本で処理。
    int nthreads = 1;
    if (full_redraw && term_h >= 8) {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpu < 1) ncpu = 1;
        if (ncpu > 8) ncpu = 8;        // スレッド生成オーバーヘッドが見合う上限
        nthreads = (int)ncpu;
        if (nthreads > term_h) nthreads = term_h;
    }

    if (nthreads <= 1) {
        base.row_start = 0;
        base.row_end   = term_h;
        render_rows(&base);
    } else {
        pthread_t threads[8];
        struct render_task tasks[8];
        bool created[8] = {0};
        int rows_per = (term_h + nthreads - 1) / nthreads;
        for (int i = 0; i < nthreads; i++) {
            int rs = i * rows_per;
            int re = rs + rows_per;
            if (rs >= term_h) break;
            if (re > term_h) re = term_h;
            tasks[i] = base;
            tasks[i].row_start = rs;
            tasks[i].row_end   = re;
            if (pthread_create(&threads[i], NULL, render_rows_thread, &tasks[i]) != 0) {
                // 生成失敗時はこの範囲を呼び出しスレッドで処理する
                render_rows(&tasks[i]);
            } else {
                created[i] = true;
            }
        }
        for (int i = 0; i < nthreads; i++) {
            if (created[i]) pthread_join(threads[i], NULL);
        }
    }

    wd->prev_term_size = ctx->term_size;
    wd->prev_cell_w    = cell_w;
    wd->prev_cell_h    = cell_h;
    wd->prev_sw        = sw;
    wd->prev_sh        = sh;
    wd->prev_cur_col   = cur_col;
    wd->prev_cur_row   = cur_row;
}


// フォントサイズ（cell_h基準、cell_wはその半分）を変更し、グリフを再読み込みする。
// 実際のterm_size再計算はメインループのリサイズ処理に委ねるため、font_size_changedを立てる。
void change_font_size(struct windata *wd, int delta)
{
    struct term_context *ctx = wd->ctx;
    if (!ctx) return;

    int new_h = ctx->cell_h + delta;
    if (new_h < FONT_CELL_H_MIN) new_h = FONT_CELL_H_MIN;
    if (new_h > FONT_CELL_H_MAX) new_h = FONT_CELL_H_MAX;
    if (new_h == ctx->cell_h) return;

    int new_w = new_h / 2;

    free_otf_glyphs(wd->glyphs);
    struct pos font_size = {new_w, new_h};
    if (load_otf_glyphs("/home/yuujirou07/myfont.otf", font_size, wd->glyphs, &wd->font_ascender) != 0) {
        error_log_write("フォントグリフの再読み込みに失敗しました");
    }

    ctx->cell_w = new_w;
    ctx->cell_h = new_h;
    wd->font_size_changed = true;
}
