#include <sys/wait.h>
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


// このファイルが担当する描画の全体像:
//
//   term_cell配列
//       ↓ render_cells_to_buffer()がCPUで文字と背景を描く
//   stagingMappedが指す画面全体のBGRA画像
//       ↓ pty_make_v1.cがvkCmdCopyBufferToImage()でGPUへコピーする
//   swapchainImagesのうち現在表示する1枚
//       ↓ vkQueuePresentKHR()
//   GLFWウィンドウに表示
//
// Vulkanは、CPUから関数を1回呼べば即座に画面が変わる仕組みではない。
// 先に「使用するGPU」「表示用画像」「GPUへ渡す命令」「完了通知」を作り、
// 描画時にそれらを組み合わせてGPUへ仕事を送る。このファイルは、その準備と
// CPU側の文字画像作成を担当する。実際のコピー・表示命令はpty_make_v1.cにある。
//
// このファイルで頻出するVulkan用語:
//   Instance       Vulkanを使い始めるためのアプリ全体の入口。
//   PhysicalDevice PCに実在するGPU。
//   Device         選んだGPUをプログラムから操作するための接続口。
//   Queue          GPUへ命令を順番に提出する窓口。
//   Surface        GLFWウィンドウとVulkanを結び付ける表示先。
//   Swapchain      表示用画像を複数枚交換しながら使う仕組み。
//   ImageView      VkImageをシェーダや描画先から使うための見方。
//   CommandBuffer  GPUに実行させる命令を記録する入れ物。
//   Semaphore      GPU処理同士の順番を待ち合わせる通知。
//   Fence          CPUがGPU処理の完了を待つための通知。
//   StagingBuffer  CPUで作った画面画像をGPUへ運ぶための一時バッファ。


// find_memory_type(): バッファに割り当て可能で、要求した性質も持つGPUメモリを探す。
// typeFilterの各ビットは、その番号のメモリタイプを対象にできるかを表す。
// propertiesには、CPUから書けることなど、追加で必要な性質を指定する。
// 引数:
//   memProps   = GPUが持つ全メモリタイプの一覧。
//   typeFilter = このバッファに使用可能なメモリタイプを示すビット列。
//   properties = 必須の性質。ここではHOST_VISIBLEとHOST_COHERENTを使う。
// 返り値: 条件を満たすメモリタイプの番号。見つからなければUINT32_MAX。
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


// window_init(): ウィンドウとVulkanの描画環境を最初から順番に構築する。
// 作成した資源はwdへ保存し、描画ループとdestroy_data()から利用する。
// 引数: wd=初期化結果を保持するwindata。ゼロ初期化済みの領域を渡す。
// 返り値: 0=成功、0以外=初期化失敗。
int window_init(struct windata* wd) {
	// GLFWはウィンドウ作成とキーボード・マウス入力を担当する。
	if (!glfwInit()) return -1;

	// GLFWにはOpenGLの描画環境を作らせず、Vulkan用のウィンドウだけ作らせる。
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

	// Instance作成時にVulkanへ渡す、このアプリ自身の基本情報。
	// sTypeは「この構造体が何の設定か」をVulkanへ伝える識別子。
	VkApplicationInfo appInfo = {0};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "my_Terminal";
	appInfo.apiVersion = VK_API_VERSION_1_3;
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

	// Surface作成にはOSごとのVulkan拡張が必要になる。
	// Wayland/X11などの違いはGLFWに任せ、必要な拡張名だけ受け取る。
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	if (glfwExtensions == NULL) {
		fprintf(stderr, "Vulkanに必要な拡張機能が取得できませんでした。\n");
		glfwDestroyWindow(wd->window);
		glfwTerminate();
		return -1;
	}

	// Validation LayerはVulkan APIの誤った使い方を実行時に検出する。
	const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
	uint32_t validationLayerCount = 1;

	// Instanceは、このプロセスでVulkanを使うための最上位ハンドル。
	// ここにアプリ情報、GLFWが要求した拡張、検証レイヤーをまとめる。
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

	// PhysicalDeviceはPC上の実際のGPU。最初の呼び出しで個数を調べ、
	// その個数分を確保してから、2回目の呼び出しで一覧を受け取る。
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
	// 現在は性能比較をせず、列挙された最初のGPUを使用する。
	VkPhysicalDevice physicalDevice = wd->devices[0];

	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
	printf("使用するGPU: %s\n", deviceProperties.deviceName);

	// 後でCPUから書けるステージングメモリを選ぶため、GPUのメモリ情報を保存する。
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &wd->memProps);

	// GPU内部には、描画・転送・計算などを受け付けるQueue Familyがある。
	// ここでは画面表示に使うため、描画命令を実行できるfamilyを1つ探す。
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

	// 選んだQueue FamilyからQueueを1本作る設定。
	// priorityは同じDevice内に複数Queueがある場合の優先度で、1.0が最高。
	VkDeviceQueueCreateInfo queueCreateInfo = {0};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = graphicsFamilyIndex;
	queueCreateInfo.queueCount = 1;
	float queuePriority = 1.0f;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	// 画面表示用画像を交換するswapchain機能はDevice拡張として有効化する。
	const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	// Logical Deviceは、選んだPhysicalDeviceをアプリから操作するためのハンドル。
	// このDevice経由でバッファ、画像、同期オブジェクトなどを作る。
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

	// vkCreateDevice()で作成を要求したQueueのハンドルを取り出す。
	vkGetDeviceQueue(wd->device, graphicsFamilyIndex, 0, &wd->graphicsQueue);

	// Surfaceは「このGLFWウィンドウへ表示する」という接続先。
	// まだ画像は持たず、次に作るswapchainの出力先として使われる。
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

	// Swapchainは表示用画像を複数枚持ち、描画済みの画像と表示中の画像を
	// 順番に交換する。まずSurfaceが対応する形式・サイズ・表示方法を調べる。
	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, wd->surface, &capabilities);

	// Surface formatは1ピクセル内の色の並びと色空間を表す。
	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, wd->surface, &formatCount, NULL);
	VkSurfaceFormatKHR* formats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, wd->surface, &formatCount, formats);

	// Present modeは完成画像を画面へ出すタイミングを表す。
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, wd->surface, &presentModeCount, NULL);
	VkPresentModeKHR* presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, wd->surface, &presentModeCount, presentModes);

	// CPU側の画像がBGRA順なので、対応していればBGRAのSRGB形式を選ぶ。
	VkSurfaceFormatKHR chosenFormat = formats[0];
	for (uint32_t i = 0; i < formatCount; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
			formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			chosenFormat = formats[i];
			break;
		}
	}
	wd->swapchainImageFormat = chosenFormat.format;

	// FIFOは垂直同期付きで必ず利用できる。MAILBOXがあれば、表示待ちの
	// 古い画像を新しい画像で置き換えられるため、こちらを優先する。
	VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	for (uint32_t i = 0; i < presentModeCount; i++) {
		if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			chosenPresentMode = presentModes[i];
			break;
		}
	}

	// extentはswapchain画像の実ピクセルサイズ。
	// OSがサイズを固定して返す環境ではcurrentExtentを使い、未指定を示す
	// UINT32_MAXの場合はGLFWから現在のフレームバッファサイズを取得する。
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

	// 最低枚数より1枚多く用意し、表示待ち中でも次の画像を扱いやすくする。
	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	// この端末はシェーダで文字を描かず、CPUで完成させた画像を直接コピーする。
	// そのコピー先にするためTRANSFER_DST_BITが必要になる。未対応時は下で
	// COLOR_ATTACHMENTだけに戻すが、現在の直接コピー経路自体は利用できなくなる。
	VkImageUsageFlags swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
								  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
		fprintf(stderr, "警告: スワップチェーンがTRANSFER_DSTをサポートしていません\n");
		swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	// ここまで選んだ表示先、枚数、色形式、サイズ、表示方法をまとめて
	// swapchainを作る。oldSwapchainは初回なのでVK_NULL_HANDLE。
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

	// swapchain画像はVulkan側が作るため、アプリはそのハンドル一覧を受け取る。
	// ここも最初に枚数を取得し、その後で配列へ実体を取得する2段階になっている。
	vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, NULL);
	wd->swapchainImages = malloc(sizeof(VkImage) * wd->swapchainImageCount);
	vkGetSwapchainImagesKHR(wd->device, wd->swapchain, &wd->swapchainImageCount, wd->swapchainImages);
	printf("スワップチェーン内の画像枚数: %d\n", wd->swapchainImageCount);

	// ImageViewは通常、各VkImageをシェーダや描画先から使うために必要になる。
	// 現在の表示経路はVkImageへ直接コピーするためImageViewを参照していないが、
	// swapchain画像1枚につき1個作成して保持している。
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

	// CommandPoolはCommandBufferを確保・再利用するための管理元。
	// RESET_COMMAND_BUFFER_BITにより、描画のたびに各CommandBufferを録音し直せる。
	VkCommandPoolCreateInfo poolInfo = {0};
	poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = graphicsFamilyIndex;
	poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	if (vkCreateCommandPool(wd->device, &poolInfo, NULL, &wd->commandPool) != VK_SUCCESS) {
		fprintf(stderr, "コマンドプールの作成に失敗しました。\n");
		return -1;
	}
	printf("コマンドプールの作成に成功しました!\n");

	// CommandBufferには、画像の状態変更とCPU画像のコピー命令を記録する。
	// pty_make_v1.cが取得したswapchain画像に対応する1本を毎回使用する。
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

	// CPUとGPUは別々に進むため、処理順を明示する同期オブジェクトが必要になる。
	// imageAvailableSemaphore: 表示用画像を取得できた後、GPUのコピーを開始させる。
	// renderFinishedSemaphore:  コピー終了後、画面表示を開始させる。
	// inFlightFence:            前回のGPU処理完了をCPU側で確認する。
	VkSemaphoreCreateInfo semInfo = {0};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fenceInfo = {0};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	if (vkCreateSemaphore(wd->device, &semInfo, NULL, &wd->imageAvailableSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(wd->device, &semInfo, NULL, &wd->renderFinishedSemaphore) != VK_SUCCESS ||
		vkCreateFence(wd->device, &fenceInfo, NULL, &wd->inFlightFence) != VK_SUCCESS) {
		fprintf(stderr, "同期オブジェクトの作成に失敗しました。\n");
		return -1;
	}

	// StagingBufferはCPUとGPUの受け渡し場所。
	// 1ピクセル4バイト(B、G、R、A)で画面全体を保持する。
	wd->stagingSize = wd->chosenExtent.width * wd->chosenExtent.height * 4;

	// TRANSFER_SRC_BITは、このバッファを画像コピーの転送元として使う指定。
	VkBufferCreateInfo bufInfo = {0};
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size  = wd->stagingSize;
	bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(wd->device, &bufInfo, NULL, &wd->stagingBuffer) != VK_SUCCESS) {
		fprintf(stderr, "ステージングバッファの作成に失敗しました。\n");
		return -1;
	}

	// VkBufferは大きさや用途を表す入れ物で、保存領域そのものではない。
	// 必要なメモリ量と使用可能なメモリタイプを調べ、別途VkDeviceMemoryを確保する。
	VkMemoryRequirements memReq;
	vkGetBufferMemoryRequirements(wd->device, wd->stagingBuffer, &memReq);

	// HOST_VISIBLEによりCPUから見え、HOST_COHERENTによりCPUの書き込みを
	// 明示的にflushせずGPUから読めるメモリを選ぶ。
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
	// 確保した保存領域をBufferへ結び付け、CPUアドレスへmapする。
	// 以後stagingMappedへ通常の配列のように書くと、GPU転送元の内容が変わる。
	if (vkBindBufferMemory(wd->device, wd->stagingBuffer, wd->stagingMemory, 0) != VK_SUCCESS) {
		fprintf(stderr, "ステージングバッファのバインドに失敗しました。\n");
		return -1;
	}
	if (vkMapMemory(wd->device, wd->stagingMemory, 0, wd->stagingSize, 0, &wd->stagingMapped) != VK_SUCCESS) {
		fprintf(stderr, "ステージングメモリのmapに失敗しました。\n");
		return -1;
	}
	printf("ステージングバッファの作成に成功しました! (%u bytes)\n", wd->stagingSize);

	return 0;
}


// recreate_swapchain(): ウィンドウの新しい大きさに合わせて表示用画像を作り直す。
// swapchain画像に依存するImageViewとCommandBufferも同時に作り直す。
// 引数: wd=現在使用中のVulkan資源と、新しい資源の保存先。
// 返り値: 0=成功、-1=再作成失敗。
int recreate_swapchain(struct windata *wd)
{
	// 使用中の資源を先に破棄しないよう、GPUの全処理が終わるまで待つ。
	vkDeviceWaitIdle(wd->device);

	// CommandBufferは古いswapchain画像を参照するので先に解放する。
	vkFreeCommandBuffers(wd->device, wd->commandPool,
						 wd->swapchainImageCount, wd->commandBuffers);
	free(wd->commandBuffers);
	wd->commandBuffers = NULL;

	// ImageViewも古い画像専用なので破棄する。swapchainImages自体は
	// Vulkanが所有しており、ここでfreeするのはハンドルを入れたCPU側配列だけ。
	for (uint32_t i = 0; i < wd->swapchainImageCount; i++)
		vkDestroyImageView(wd->device, wd->swapchainImageViews[i], NULL);
	free(wd->swapchainImageViews);
	free(wd->swapchainImages);
	wd->swapchainImageViews = NULL;
	wd->swapchainImages = NULL;

	VkPhysicalDevice physicalDevice = wd->devices[0];

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, wd->surface, &capabilities);

	// Surfaceの最新の制約を取得し、新しいswapchain画像の実ピクセルサイズを決める。
	// min/maxの範囲外にならないよう、対応範囲内へ丸める。
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

	// 画面が大きくなり、既存StagingBufferへ全ピクセルが入らない場合だけ拡張する。
	// 小さくなった場合は既存領域を再利用し、リサイズの確保コストを避ける。
	uint32_t newStagingSize = wd->chosenExtent.width * wd->chosenExtent.height * 4;
	if (newStagingSize > wd->stagingSize) {
		vkUnmapMemory(wd->device, wd->stagingMemory);
		vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
		vkFreeMemory(wd->device, wd->stagingMemory, NULL);
		// 失敗時はこの関数がそれ以降の処理を諦めて-1を返す前提で、
		// 呼び出し側(render_cells_to_bufferやフレーム描画)がstagingMapped==NULLを見て
		// 無効なハンドルを使わずに済むよう、失敗しうる区間は常にNULL/VK_NULL_HANDLEにしておく。
		wd->stagingBuffer = VK_NULL_HANDLE;
		wd->stagingMemory = VK_NULL_HANDLE;
		wd->stagingMapped = NULL;
		wd->stagingSize = 0;

		VkBufferCreateInfo bufInfo = {0};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size  = newStagingSize;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(wd->device, &bufInfo, NULL, &wd->stagingBuffer) != VK_SUCCESS) {
			fprintf(stderr, "ステージングバッファの再作成に失敗しました。\n");
			wd->stagingBuffer = VK_NULL_HANDLE;
			return -1;
		}

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(wd->device, wd->stagingBuffer, &memReq);
		uint32_t memTypeIdx = find_memory_type(&wd->memProps, memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memTypeIdx == UINT32_MAX) {
			fprintf(stderr, "適切なメモリタイプが見つかりませんでした。\n");
			vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
			wd->stagingBuffer = VK_NULL_HANDLE;
			return -1;
		}

		VkMemoryAllocateInfo memAllocInfo = {0};
		memAllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize  = memReq.size;
		memAllocInfo.memoryTypeIndex = memTypeIdx;
		if (vkAllocateMemory(wd->device, &memAllocInfo, NULL, &wd->stagingMemory) != VK_SUCCESS) {
			fprintf(stderr, "ステージングメモリの再確保に失敗しました。\n");
			vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
			wd->stagingBuffer = VK_NULL_HANDLE;
			return -1;
		}
		if (vkBindBufferMemory(wd->device, wd->stagingBuffer, wd->stagingMemory, 0) != VK_SUCCESS ||
			vkMapMemory(wd->device, wd->stagingMemory, 0, newStagingSize, 0, &wd->stagingMapped) != VK_SUCCESS) {
			fprintf(stderr, "ステージングバッファのバインド/mapに失敗しました。\n");
			vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
			vkFreeMemory(wd->device, wd->stagingMemory, NULL);
			wd->stagingBuffer = VK_NULL_HANDLE;
			wd->stagingMemory = VK_NULL_HANDLE;
			wd->stagingMapped = NULL;
			return -1;
		}
		wd->stagingSize = newStagingSize;
	}

	// ここから下はwindow_init()と同じ要領で、新サイズのswapchainと
	// それに属するImageView、CommandBufferを作り直す。
	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	VkImageUsageFlags swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
								  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	// oldSwapchainを作成情報へ渡すと、Vulkan側が古い表示資源から
	// 新しい表示資源へ安全に移行できる。新しい作成後に古い方を破棄する。
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


// set_window(): 作成済みウィンドウへタイトルを設定して表示する。
// 引数: wd=対象のGLFWwindowを保持するwindata。
// 返り値: なし。
void set_window(struct windata* wd) {
	glfwSetWindowTitle(wd->window, "bash");
	glfwShowWindow(wd->window);
}

// destroy_data(): window_init()で作った資源を依存関係の逆順で破棄し、終了する。
// GPUが使用中の資源を破棄しないよう、最初にvkDeviceWaitIdle()で完了を待つ。
// 引数: wd=破棄対象の全資源を保持するwindata。
// 返り値: 戻らない。最後にexit(1)でプロセスを終了する。
void destroy_data(struct windata* wd)
{
	vkDeviceWaitIdle(wd->device);

	// CPUアドレスとの対応を解除してから、Bufferとその保存領域を破棄する。
	vkUnmapMemory(wd->device, wd->stagingMemory);
	vkDestroyBuffer(wd->device, wd->stagingBuffer, NULL);
	vkFreeMemory(wd->device, wd->stagingMemory, NULL);

	// 同期オブジェクトはCommandBufferの実行完了後なら安全に破棄できる。
	vkDestroySemaphore(wd->device, wd->renderFinishedSemaphore, NULL);
	vkDestroySemaphore(wd->device, wd->imageAvailableSemaphore, NULL);
	vkDestroyFence(wd->device, wd->inFlightFence, NULL);

	// CommandPoolを破棄すると、そこから確保したCommandBufferもまとめて破棄される。
	vkDestroyCommandPool(wd->device, wd->commandPool, NULL);

	for (uint32_t i = 0; i < wd->swapchainImageCount; i++)
		vkDestroyImageView(wd->device, wd->swapchainImageViews[i], NULL);

	free(wd->commandBuffers);
	free(wd->swapchainImageViews);
	free(wd->swapchainImages);

	free(wd->prev_term_cell);

	// 下位資源から順に、swapchain→Device→Surface→Instanceを破棄する。
	vkDestroySwapchainKHR(wd->device, wd->swapchain, NULL);
	vkDestroyDevice(wd->device, NULL);
	vkDestroySurfaceKHR(wd->instance, wd->surface, NULL);
	vkDestroyInstance(wd->instance, NULL);
	glfwDestroyWindow(wd->window);
	glfwTerminate();
	free(wd->devices);
	int status = 0;
	pid_t result = 
		waitpid(wd->ctx->bash_pid,&status,WUNTRACED);
	if(result > 0){
        printf("子プロセス %d が終了しました。\n", result);
    }
	exit(1);
}


// set_kbd_callback(): GLFWにキー入力と文字入力のコールバックを登録する。
// key_callbackは特殊キー、character_callbackは確定した文字を処理する。
// 引数: wd=コールバックを登録するGLFWwindowを保持するwindata。
// 返り値: 常に0。
int set_kbd_callback(struct windata* wd) {
	glfwSetKeyCallback(wd->window, key_callback);
	glfwSetCharCallback(wd->window, character_callback);
	return 0;
}


// draw_cell_pixels(): 端末セル1個をCPU上のBGRA画像へ描く。
// Vulkan APIは呼ばず、stagingMappedを通常のバイト配列として書き換える。
// 引数:
//   buf       = 画面全体のBGRA画像。
//   sw/sh     = bufの横幅と高さ。単位はピクセル。
//   base_x/y  = このセルの左上座標。
//   cell_w/h  = セル1個のピクセルサイズ。
//   ascender  = フォント基準線より上の高さ。
//   cell      = 描く文字、前景色、背景色を持つ端末セル。
//   glyphs    = ASCII文字のビットマップ一覧。
// 返り値: なし。bufの該当セル領域を直接更新する。
static void draw_cell_pixels(uint8_t *buf, int sw, int sh,
							  int base_x, int base_y, int cell_w, int cell_h,
							  int ascender, const struct term_cell *cell,
							  struct glyph_data *glyphs)
{
	// まずセル全体を背景色で塗る。画像の1ピクセルはB、G、R、Aの4バイト。
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

	// 次に背景の上へ文字を重ねる。スペース・未対応文字は背景だけで終了する。
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

	// フォントは実際の表示サイズの2倍で読み込んでいる。
	// 2x2ピクセルを平均して1ピクセルへ縮小し、輪郭の濃淡を滑らかにする。
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
			// FreeType画像の2x2ブロックを平均し、文字色を混ぜる強さにする。
			// a=0なら背景のまま、a=255なら完全に前景色になる。
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

// invert_cell_pixels(): セル1個分のRGBを反転し、ブロックカーソルとして見せる。
// 引数: buf=BGRA画像、sw/sh=画面サイズ、base_x/y=セル左上、cell_w/h=セルサイズ。
// 返り値: なし。アルファ値は変えず、RGBだけを直接更新する。
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

// cell_visually_equal(): 2セルの見た目に関係する値だけを比較する。
// 引数: a/b=比較するセル。
// 返り値: 文字・前景色・背景色がすべて同じならtrue、それ以外はfalse。
static bool cell_visually_equal(const struct term_cell *a, const struct term_cell *b)
{
	return a->character == b->character &&
		   memcmp(&a->fg_color, &b->fg_color, sizeof(Color)) == 0 &&
		   memcmp(&a->bg_color, &b->bg_color, sizeof(Color)) == 0;
}

// render_taskは、1本の描画スレッドへ渡す入力をまとめたもの。
// row_start以上row_end未満の行だけを担当する。
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

// render_rows(): taskで指定された行範囲のうち、再描画が必要なセルだけを描く。
// 引数: t=画面情報、前フレーム情報、担当行範囲をまとめたrender_task。
// 返り値: なし。t->bufとt->wd->prev_term_cellを直接更新する。
static void render_rows(struct render_task *t)
{
	struct term_context *ctx = t->wd->ctx;
	for (int row = t->row_start; row < t->row_end; row++) {
		for (int col = 0; col < t->term_w; col++) {
			int idx = row * t->term_w + col;
			struct term_cell *cell = &ctx->term_cell[idx];
			struct term_cell *prev = &t->wd->prev_term_cell[idx];

			// 現在のカーソル位置は、セルを描いた後に色反転する必要がある。
			bool is_new_cursor = (col == t->cur_col && row == t->cur_row);
			// カーソルが移動した場合、移動元のセルを反転無しで再描画して元に戻す
			bool is_old_cursor = !t->full_redraw &&
				col == t->prev_cur_col && row == t->prev_cur_row && !is_new_cursor;

			// 通常フレームは、内容が前回と同じセルを飛ばしてCPU描画を減らす。
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

			// 次回の差分判定に使うため、今回描いた状態を保存する。
			*prev = *cell;
		}
	}
}

// render_rows_thread(): pthreadからrender_rows()を呼ぶための入口。
// 引数: arg=struct render_taskへのポインタ。
// 返り値: pthread用の戻り値。返すデータはないため常にNULL。
static void *render_rows_thread(void *arg)
{
	render_rows((struct render_task *)arg);
	return NULL;
}

// render_cells_to_buffer(): term_cellから画面全体のBGRA画像をCPUで作る。
// 通常は前フレームから変化したセルとカーソルの移動元・移動先だけを再描画する。
// 全画面再描画(full_redraw)はリサイズ中に毎フレーム発生し文字数に比例して重いため、
// その場合だけ行範囲を複数スレッドへ分割して並列描画する。
// この関数の終了時点ではGPUへのコピーや画面表示はまだ行われていない。
// 引数: wd=端末状態、フォント、stagingMapped、前フレーム状態を保持するwindata。
// 返り値: なし。wd->stagingMappedと差分判定用状態を直接更新する。
void render_cells_to_buffer(struct windata *wd)
{
	struct term_context *ctx = wd->ctx;
	if (!ctx || !wd->stagingMapped) return;

	// map済みGPUメモリをCPU側では画面全体のバイト配列として扱う。
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
		// セル数が変わる可能性があるため、前フレーム配列も同じ大きさで作り直す。
		// 全ピクセルを透明な黒で消してから全セルを描き直す。
		free(wd->prev_term_cell);
		wd->prev_term_cell = malloc(sizeof(struct term_cell) * (size_t)cell_count);
		memset(buf, 0, (size_t)(sw * sh * 4));
	}

	int cur_col = ctx->cur->cur_pos.w;
	int cur_row = ctx->cur->cur_pos.h;

	// 全スレッドで共通する描画条件を作り、後で担当する行範囲だけ変える。
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
		// 各taskは別の行範囲を持つため、同じbufを共有しても書き込み先は重ならない。
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

	// 今回の状態を保存し、次回は変更箇所だけを判断できるようにする。
	wd->prev_term_size = ctx->term_size;
	wd->prev_cell_w    = cell_w;
	wd->prev_cell_h    = cell_h;
	wd->prev_sw        = sw;
	wd->prev_sh        = sh;
	wd->prev_cur_col   = cur_col;
	wd->prev_cur_row   = cur_row;
}


// change_font_size(): セルとフォントの大きさを指定段階だけ変更する。
// 実際のterm_size再計算はメインループのリサイズ処理に委ねるため、font_size_changedを立てる。
// 引数: wd=端末状態とフォントを保持するwindata、delta=cell_hへ加えるピクセル数。
// 返り値: なし。上限・下限に達していて大きさが変わらない場合は何もしない。
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
