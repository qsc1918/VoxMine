#pragma once
#include "vk.hpp"
#include "save.hpp"
#include <string>
#include <vector>

class Window;

// Which full-screen menu is active (null/any means none -> gameplay).
enum class Menuscreen {
    None,
    MainMenu,
    SaveSelect,
    NewWorld,
    Options,
    VideoSettings,
    Pause,
};

// Button ids returned by renderMenu().
enum {
    MENU_NONE = -1,
    MENU_SINGLEPLAYER = 1,
    MENU_OPTIONS = 2,
    MENU_QUIT = 3,
    MENU_NEWWORLD = 4,
    MENU_CREATE = 5,
    MENU_CANCEL = 6,
    MENU_VIDEO = 7,
    MENU_BACK = 8,
    MENU_VSYNC = 9,
    MENU_RENDERDIST = 10,
    MENU_SAVEANDTITLE = 200,
    MENU_SAVE_FIRST = 100,     // saves are 100, 101, ...
    MENU_DELETE_FIRST = 500,   // delete buttons 500+i
};

struct MenuData {
    std::vector<WorldSave> saves;
    std::string seedText;
    std::string titleText;          // e.g. "新建世界" / pause text
    bool vsync = true;
    int renderDist = 8;             // chunk render distance (video settings)
    int saveCount = 0;
};

// Full-screen menu renderer. Draws a GDI-composited menu (dirt background, MC-style
// 9-slice buttons, system-font text) into a texture and blits it as a fullscreen quad.
class Menu {
public:
    bool init(VkCtx& ctx, Window& win, const std::string& assetDir);
    void shutdown(VkCtx& ctx);

    // Renders the given screen and returns the id of the button clicked this frame
    // (MENU_NONE if none). cx,cy are cursor client coords; mouseDown is a fresh click.
    int renderMenu(VkCtx& ctx, Menuscreen screen, const MenuData& data,
                   float cx, float cy, bool mouseDown);

    void onResize(VkCtx& ctx, int w, int h);

    // Debug: save the last rendered menu (the DIB) to a PNG file.
    bool debugSaveMenu(const std::string& path) const;

private:
    bool loadTextures(const std::string& assetDir);
    void createMenuTexture(VkCtx& ctx);
    void destroyMenuTexture(VkCtx& ctx);
    void ensureDIB(int w, int h);
    void renderToDIB(Menuscreen screen, const MenuData& data, float cx, float cy);
    void uploadAndDraw(VkCtx& ctx);
    int  hitTest(float cx, float cy) const;

    // GDI
    void* dibBits_ = nullptr;      // screen DIB pixel buffer
    HBITMAP dibBmp_ = nullptr;
    HDC     dibDC_ = nullptr;
    int     diw_ = 0, dih_ = 0;
    HFONT   font_ = nullptr;

    // loaded textures (RGBA + DIB/HDC for GDI blitting)
    std::vector<uint8_t> dirtRGBA_;
    std::vector<uint8_t> btnRGBA_, btnHiRGBA_, fieldRGBA_;
    int dirtW_ = 0, dirtH_ = 0, btnW_ = 0, btnH_ = 0;
    HDC dirtDC_ = nullptr, btnDC_ = nullptr, btnHiDC_ = nullptr, fieldDC_ = nullptr;
    HBITMAP dirtBmp_ = nullptr, btnBmp_ = nullptr, btnHiBmp_ = nullptr, fieldBmp_ = nullptr;

    // Vulkan
    VkImage    img_ = VK_NULL_HANDLE;
    VkDeviceMemory imgMem_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler  sampler_ = VK_NULL_HANDLE;
    VkBuffer   staging_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline  pipe_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkBuffer   quadBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory quadMem_ = VK_NULL_HANDLE;
    VkDescriptorPool mpool_ = VK_NULL_HANDLE;
    bool texReady_ = false;
    uint64_t frameIdx_ = 0;

    // button rects for hit-testing, filled each frame
    struct Btn { int id; float x, y, w, h; };
    std::vector<Btn> btns_;
    bool prevMouseDown_ = false;
};
