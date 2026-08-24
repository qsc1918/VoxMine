#pragma once
#include "atlas.hpp"
#include "util.hpp"
#include "vk.hpp"
#include "window.hpp"
#include "world.hpp"
#include <string>

struct Camera;
struct Player;

struct UIVertex {
    float x, y;     // NDC
    float u, v;     // atlas uv
    float r, g, b, a;
};

struct Buffer2 {
    VkBuffer b = VK_NULL_HANDLE;
    VkDeviceMemory m = VK_NULL_HANDLE;
    void destroy(VkDevice d) {
        if (b) vkDestroyBuffer(d, b, nullptr);
        if (m) vkFreeMemory(d, m, nullptr);
        b = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
    }
};

class Renderer {
public:
    bool init(VkCtx& ctx, Window& win, const std::string& assetDir,
              const std::string& shaderDir);
    void shutdown(VkCtx& ctx);

    // Acquires image, records and submits one frame. Returns false if swapchain was recreated.
    bool render(VkCtx& ctx, const Camera& cam, Player& player, Input& in, float dt,
                float renderDist, bool drawUI);

    // Bind the world to render (may change when entering a new world).
    void setWorld(World& w);

    // GPU-side chunk mesh management (main thread only)
    void uploadChunks(VkCtx& ctx);
    void destroyChunkBuffers(VkCtx& ctx, Chunk& c);

    // Wait until the previously-submitted frame's GPU work is finished. This MUST
    // be called before the CPU destroys or rewrites any chunk vertex/index buffer
    // (world update / upload), otherwise the GPU may still be reading those buffers
    // from the frame in flight, producing corruption (flicker/seams during motion).
    void gpuSync(VkCtx& ctx);

    void requestScreenshot(const std::string& path) { pendingShot_ = path; }
    void setTimeOfDay(float t) { timeOfDay_ = t; }

    int selectedSlot() const { return selectedSlot_; }
    uint8_t selectedBlock() const;
    float fps() const { return fps_; }
    int debugDraws() const { return debugDraws_; }

    // Inventory (E) screen
    void setInventoryOpen(bool open);
    bool inventoryOpen() const { return invOpen_; }
    void setCursor(float x, float y) { cursorX_ = x; cursorY_ = y; }

private:
    bool createPipelines(VkCtx& ctx);
    void createAtlasTexture(VkCtx& ctx);
    void createDescriptors(VkCtx& ctx);
    void updateTerrainUBO(VkCtx& ctx, const Camera& cam, float renderDist);
    void drawChunks(VkCtx& ctx, const Camera& cam);
    void drawUIOverlay(VkCtx& ctx, const Camera& cam, Input& in);
    void captureScreenshot(VkCtx& ctx, uint32_t imageIndex);

    World* world_ = nullptr;
    VkCtx* ctxPtr_ = nullptr;
    Atlas atlas_;

    VkDescriptorSetLayout terrainDSL_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDSL_ = VK_NULL_HANDLE;   // sampler only
    VkDescriptorSetLayout skyDSL_ = VK_NULL_HANDLE;  // ubo only
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    VkPipelineLayout terrainLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skyLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout uiLayout_ = VK_NULL_HANDLE;

    VkPipeline terrainPipe_ = VK_NULL_HANDLE;
    VkPipeline waterPipe_ = VK_NULL_HANDLE;
    VkPipeline skyPipe_ = VK_NULL_HANDLE;
    VkPipeline uiPipe_ = VK_NULL_HANDLE;

    VkImage atlasImage_ = VK_NULL_HANDLE;
    VkDeviceMemory atlasMem_ = VK_NULL_HANDLE;
    VkImageView atlasView_ = VK_NULL_HANDLE;
    VkSampler atlasSampler_ = VK_NULL_HANDLE;

    Buffer2 terrainUBO_;
    VkDescriptorSet terrainSet_ = VK_NULL_HANDLE;

    Buffer2 skyUBO_;
    VkDescriptorSet skySet_ = VK_NULL_HANDLE;

    Buffer2 uiBuf_;
    void* uiMap_ = nullptr;
    VkDescriptorSet uiSet_ = VK_NULL_HANDLE;

    VkBuffer shotBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory shotMem_ = VK_NULL_HANDLE;
    bool shotBufReady_ = false;
    std::string pendingShot_;
    bool shotTaken_ = false;

    int windowW_ = 0, windowH_ = 0;
    float fps_ = 0.0f;
    float fpsTimer_ = 0.0f;
    int frames_ = 0;

    int selectedSlot_ = 0;
    float slotAnim_ = 0.0f;
    int lastWheel_ = 0;
    uint64_t frameIdx_ = 0;
    int shotW_ = 0, shotH_ = 0;
    int debugDraws_ = 0;
    float timeOfDay_ = 0.25f;
    float timeScale_ = 1.0f / 1200.0f; // full day/night cycle in 20 minutes

    bool invOpen_ = false;
    float cursorX_ = 0.0f, cursorY_ = 0.0f;
    bool prevMouse0_ = false;
    uint8_t placementBlock_ = B_GRASS;
    uint8_t hotbar_[9] = {B_GRASS, B_STONE, B_COBBLE, B_PLANKS, B_LOG,
                          B_DIRT, B_SAND, B_GRAVEL, B_GLASS};
    // inventory contents (all placeable blocks)
    static const uint8_t kInvBlocks[];
    static const int kInvCount;
    static const int kInvCols = 8;
};
