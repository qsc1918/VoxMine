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
    void updateTerrainUBO(VkCtx& ctx, const Camera& cam, float renderDist, int slot);
    void drawChunks(VkCtx& ctx, const Camera& cam);
    void drawUIOverlay(VkCtx& ctx, const Camera& cam, Input& in);
    void captureScreenshot(VkCtx& ctx, uint32_t imageIndex);
    void uploadPart(VkCtx& ctx, Chunk& c, bool opaque,
                    const std::vector<TerrainVertex>& verts,
                    const std::vector<uint32_t>& idx);
    void retireBuffer(VkBuffer b, VkDeviceMemory m, bool opaque, Chunk& c);
    void flushRetired(VkCtx& ctx, uint64_t submittedFrames);

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

    Buffer2 terrainUBO_[VkCtx::MAX_FRAMES_IN_FLIGHT];
    void* terrainUBOMap_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};
    VkDescriptorSet terrainSet_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};

    Buffer2 skyUBO_[VkCtx::MAX_FRAMES_IN_FLIGHT];
    void* skyUBOMap_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};
    VkDescriptorSet skySet_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};

    Buffer2 uiBuf_[VkCtx::MAX_FRAMES_IN_FLIGHT];
    void* uiMap_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};
    VkDescriptorSet uiSet_[VkCtx::MAX_FRAMES_IN_FLIGHT] = {};

    // Chunk GPU buffers that are no longer referenced but cannot be freed while a
    // frame in flight may still read them. Freed once enough frames have elapsed.
    struct RetiredBuf { VkBuffer b; VkDeviceMemory m; uint64_t frame; VkDeviceSize size; void* mapPtr; };
    std::vector<RetiredBuf> retired_;
    // Once a retired buffer is safe (its frames completed) it is moved here and
    // reused for future chunk uploads, avoiding vkCreateBuffer/vkAllocateMemory
    // churn while streaming. Bounded to avoid unbounded memory growth.
    std::vector<RetiredBuf> freePool_;

    int curFrame_ = 0;   // frame slot (0..MAX_FRAMES_IN_FLIGHT-1) being recorded now

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
    Mat4 cachedVP_;
    float fogEndWorld_ = 0.0f;   // world-space distance at which fog fully obscures a chunk
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

    // Reusable raw-pointer snapshot of world chunks (avoids shared_ptr per frame).
    std::vector<World::ChunkInfo> snapshot_;
};
