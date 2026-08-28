#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <volk.h>
#include <cstdint>
#include <string>
#include <vector>

struct Window;

// Full Vulkan context: instance, device, swapchain, sync primitives.
struct VkCtx {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics = VK_NULL_HANDLE;
    VkQueue present = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMem = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    // One command buffer per frame in flight. The renderer and menu submit one
    // frame per slot, so slot N is only reset after slot N's fence is signalled
    // (see acquireNext), allowing CPU/GPU overlap.
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> cmds;

    struct Frame {
        VkSemaphore avail = VK_NULL_HANDLE;
        VkSemaphore done = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
    std::vector<Frame> frames; // size == MAX_FRAMES_IN_FLIGHT

    bool ok = false;
    bool vsync = true;
    std::string lastError;

    bool init(Window& win, int w, int h);
    void shutdown();

    bool recreateSwapchain(int w, int h);
    void destroySwapchainObjects();
    void createDepth();
    void destroyDepth();

    // submits the recorded command buffer and presents; returns false if swapchain is stale.
    bool presentImage(uint32_t imageIndex, Frame& f, VkCommandBuffer cmd);
    bool acquireNext(Frame& f, uint32_t& imageIndex);

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    VkFormat pickDepthFormat();
    void selectSwapFormat();
    VkPhysicalDeviceProperties props = {};
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
};
