#include "vk.hpp"
#include "window.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static const char* kInstanceExts[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
static const char* kDeviceExts[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

bool VkCtx::init(Window& win, int w, int h) {
    if (volkInitialize() != VK_SUCCESS) {
        lastError = "volkInitialize failed";
        return false;
    }

    // ---- instance ----
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExts(instExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExts.data());

    std::vector<const char*> enabledExts;
    for (const char* e : kInstanceExts) {
        for (auto& p : instExts)
            if (strcmp(p.extensionName, e) == 0) enabledExts.push_back(e);
    }

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    bool hasValidation = false;
    for (auto& l : layers)
        if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) hasValidation = true;

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "VoxMine";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "VoxMine";
    app.apiVersion = VK_API_VERSION_1_1;

    hasValidation = hasValidation && (getenv("VULKAN_VALIDATION") != nullptr);
    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = (uint32_t)enabledExts.size();
    ici.ppEnabledExtensionNames = enabledExts.empty() ? nullptr : enabledExts.data();
    if (hasValidation) {
        const char* v = "VK_LAYER_KHRONOS_validation";
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &v;
    }

    VkResult rci = vkCreateInstance(&ici, nullptr, &instance);
    if (rci != VK_SUCCESS) {
        fprintf(stderr, "[vk] vkCreateInstance failed (%d)\n", (int)rci);
        lastError = "vkCreateInstance failed";
        return false;
    }
    volkLoadInstance(instance);

    // ---- surface ----
    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hwnd = (HWND)win.hwnd();
    sci.hinstance = GetModuleHandleA(nullptr);
    if (vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) {
        lastError = "surface creation failed";
        return false;
    }

    // ---- physical device ----
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    if (devCount == 0) {
        lastError = "no physical device";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice d : devices) {
        vkGetPhysicalDeviceProperties(d, &props);
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, qfs.data());
        bool hasGfx = false, hasSwap = false;
        for (uint32_t i = 0; i < qfCount; i++) {
            if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &support);
                if (support) { hasGfx = true; graphicsFamily = i; break; }
            }
        }
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        for (auto& e : exts)
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) hasSwap = true;
        int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 50);
        if (hasGfx && hasSwap && score > bestScore) {
            bestScore = score;
            phys = d;
        }
    }
    if (phys == VK_NULL_HANDLE) {
        lastError = "no suitable device";
        return false;
    }
    vkGetPhysicalDeviceProperties(phys, &props);

    // ---- device ----
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, qfs.data());

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = graphicsFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkPhysicalDeviceFeatures feats = {};
    feats.samplerAnisotropy = VK_TRUE;
    feats.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &feats;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = kDeviceExts;
    if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) {
        lastError = "vkCreateDevice failed";
        return false;
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphics);
    present = graphics;

    depthFormat = pickDepthFormat();

    // ---- render pass ----
    VkAttachmentDescription color = {};
    color.format = VK_FORMAT_B8G8R8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth = {};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription atts[2] = {color, depth};
    VkRenderPassCreateInfo rpi = {};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &subpass;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS) {
        lastError = "render pass creation failed";
        return false;
    }

    VkCommandPoolCreateInfo cpci = {};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = graphicsFamily;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &cpci, nullptr, &cmdPool) != VK_SUCCESS) {
        lastError = "command pool failed";
        return false;
    }
    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) {
        lastError = "command buffer alloc failed";
        return false;
    }

    if (!recreateSwapchain(w, h)) return false;
    ok = true;
    return true;
}

uint32_t VkCtx::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

VkFormat VkCtx::pickDepthFormat() {
    VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                             VK_FORMAT_D16_UNORM};
    for (VkFormat f : candidates) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(phys, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    return VK_FORMAT_D32_SFLOAT;
}

void VkCtx::destroySwapchainObjects() {
    vkDeviceWaitIdle(device);
    lastSubmitFence = VK_NULL_HANDLE;
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
    for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
    swapViews.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    for (auto& f : frames) {
        if (f.avail) vkDestroySemaphore(device, f.avail, nullptr);
        if (f.done) vkDestroySemaphore(device, f.done, nullptr);
        if (f.fence) vkDestroyFence(device, f.fence, nullptr);
    }
    frames.clear();
}

void VkCtx::createDepth() {
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = depthFormat;
    ici.extent = {extent.width, extent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ici, nullptr, &depthImage) != VK_SUCCESS) return;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, depthImage, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &depthMem) != VK_SUCCESS) return;
    vkBindImageMemory(device, depthImage, depthMem, 0);
    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = depthImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = depthFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &vci, nullptr, &depthView);
}

void VkCtx::destroyDepth() {
    if (depthView) vkDestroyImageView(device, depthView, nullptr);
    if (depthImage) vkDestroyImage(device, depthImage, nullptr);
    if (depthMem) vkFreeMemory(device, depthMem, nullptr);
    depthView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthMem = VK_NULL_HANDLE;
}

bool VkCtx::recreateSwapchain(int w, int h) {
    vkDeviceWaitIdle(device);
    destroySwapchainObjects();
    destroyDepth();

    extent = {std::max(1u, (uint32_t)w), std::max(1u, (uint32_t)h)};

    // choose surface format
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts.data());
    swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB) { swapFormat = VK_FORMAT_B8G8R8A8_SRGB; break; }
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    }

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = imgCount;
    sci.imageFormat = swapFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    swapImages.resize(imgCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImages.data());

    // framebuffers must use the real swap format; rebuild render pass attachments if needed.
    // We create the color attachment description dynamically below via a stored format,
    // so recreate the render pass if it differs from the initial one.
    if (renderPass) {
        // The render pass was created with UNORM format assumption; SRGB works the same,
        // so we can keep it. Format identity is only used for the attachment; SRGB vs
        // UNORM swaps are allowed at framebuffer level? Not strictly. Recreate to be safe.
        VkAttachmentDescription atts[2];
        atts[0].flags = 0;
        atts[0].format = swapFormat;
        atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
        atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        atts[1] = atts[0];
        atts[1].format = depthFormat;
        atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        VkRenderPassCreateInfo rpi = {};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 2;
        rpi.pAttachments = atts;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &subpass;
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
            return false;
    }

    for (VkImage img : swapImages) {
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        VkImageView view;
        vkCreateImageView(device, &vci, nullptr, &view);
        swapViews.push_back(view);
    }

    createDepth();

    for (size_t i = 0; i < swapImages.size(); i++) {
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = renderPass;
        fci.width = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        VkImageView fbs[2] = {swapViews[i], depthView};
        fci.attachmentCount = 2;
        fci.pAttachments = fbs;
        VkFramebuffer fb;
        vkCreateFramebuffer(device, &fci, nullptr, &fb);
        framebuffers.push_back(fb);
    }

    for (size_t i = 0; i < swapImages.size(); i++) {
        Frame f;
        VkSemaphoreCreateInfo sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(device, &sci, nullptr, &f.avail);
        vkCreateSemaphore(device, &sci, nullptr, &f.done);
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fci, nullptr, &f.fence);
        frames.push_back(f);
    }
    return true;
}

bool VkCtx::acquireNext(Frame& f, uint32_t& imageIndex) {
    vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.avail, VK_NULL_HANDLE, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
    vkResetFences(device, 1, &f.fence);
    return true;
}

bool VkCtx::presentImage(uint32_t imageIndex, Frame& f) {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.avail;
    si.pWaitDstStageMask = &waitStage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &f.done;
    if (vkQueueSubmit(graphics, 1, &si, f.fence) != VK_SUCCESS) return false;

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &f.done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    VkResult r = vkQueuePresentKHR(present, &pi);
    return r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR;
}

void VkCtx::shutdown() {
    vkDeviceWaitIdle(device);
    destroySwapchainObjects();
    destroyDepth();
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    surface = VK_NULL_HANDLE;
}
