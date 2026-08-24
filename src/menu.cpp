#include "menu.hpp"
#include "window.hpp"
#include "png.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

namespace {
const int BTN_BORDER = 3; // 9-slice border on the 200x20 MC button sprite

std::wstring utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// Creates a top-down 32bpp DIB from an RGBA buffer and selects it into a memory DC.
void makeDib(const uint8_t* rgba, int w, int h, HBITMAP& bmp, HDC& dc, uint8_t** bits) {
    dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &raw, nullptr, 0);
    uint8_t* pbits = (uint8_t*)raw;
    if (rgba && pbits) {
        for (int i = 0, n = w * h; i < n; i++) {
            pbits[i * 4 + 0] = rgba[i * 4 + 2];
            pbits[i * 4 + 1] = rgba[i * 4 + 1];
            pbits[i * 4 + 2] = rgba[i * 4 + 0];
            pbits[i * 4 + 3] = rgba[i * 4 + 3];
        }
    }
    if (bits) *bits = pbits;
    SelectObject(dc, bmp);
}
} // namespace

bool Menu::init(VkCtx& ctx, Window& win, const std::string& assetDir) {
    if (!loadTextures(assetDir)) return false;
    font_ = CreateFontA(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei");

    // drawable texture DIBs for buttons / text field (height = sprite height)
    makeDib(dirtRGBA_.data(), dirtW_, dirtH_, dirtBmp_, dirtDC_, nullptr);
    makeDib(btnRGBA_.data(), btnW_, btnH_, btnBmp_, btnDC_, nullptr);
    makeDib(btnHiRGBA_.data(), btnW_, btnH_, btnHiBmp_, btnHiDC_, nullptr);
    makeDib(fieldRGBA_.data(), btnW_, btnH_, fieldBmp_, fieldDC_, nullptr);
    ensureDIB(win.width(), win.height());
    createMenuTexture(ctx);

    // ---- pipeline ----
    VkDescriptorSetLayoutBinding b0 = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                       VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &b0;
    vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &dsl_);

    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &dsl_;
    vkCreatePipelineLayout(ctx.device, &pl, nullptr, &layout_);

    auto loadMod = [&](const char* path) -> VkShaderModule {
        std::string full = assetDir + "/../shaders/" + path;
        FILE* f = fopen(full.c_str(), "rb");
        if (!f) { full = std::string("shaders/") + path; f = fopen(full.c_str(), "rb"); }
        if (!f) return VK_NULL_HANDLE;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> data(sz);
        fread(data.data(), 1, sz, f);
        fclose(f);
        VkShaderModuleCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = data.size();
        ci.pCode = (const uint32_t*)data.data();
        VkShaderModule m;
        vkCreateShaderModule(ctx.device, &ci, nullptr, &m);
        return m;
    };
    auto vs = loadMod("menu.vert.spv");
    auto fs = loadMod("menu.frag.spv");
    if (!vs || !fs) { fprintf(stderr, "[menu] shader load failed\n"); return false; }

    VkPipelineShaderStageCreateInfo st[2] = {};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fs;
    st[1].pName = "main";

    VkVertexInputBindingDescription vb = {0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[3] = {};
    va[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    va[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8};
    va[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba = {};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynCI = {};
    dynCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynCI.dynamicStateCount = 2;
    dynCI.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dynCI;
    pi.layout = layout_;
    pi.renderPass = ctx.renderPass;
    pi.subpass = 0;
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe_);
    vkDestroyShaderModule(ctx.device, vs, nullptr);
    vkDestroyShaderModule(ctx.device, fs, nullptr);

    // descriptor set (menu texture)
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    VkDescriptorPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes = &ps;
    VkDescriptorPool pool;
    vkCreateDescriptorPool(ctx.device, &poolCI, nullptr, &pool);
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &dsl_;
    vkAllocateDescriptorSets(ctx.device, &ai, &set_);
    VkDescriptorImageInfo di = {sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set_;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(ctx.device, 1, &wr, 0, nullptr);
    mpool_ = pool;

    // fullscreen quad (2 triangles, NDC -1..1)
    struct QV { float x, y, u, v, r, g, b, a; };
    QV quad[6] = {
        {-1,-1,0,1,1,1,1,1},{1,-1,1,1,1,1,1,1},{1,1,1,0,1,1,1,1},
        {-1,-1,0,1,1,1,1,1},{1,1,1,0,1,1,1,1},{-1,1,0,0,1,1,1,1},
    };
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = sizeof(quad);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vkCreateBuffer(ctx.device, &bci, nullptr, &quadBuf_);
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, quadBuf_, &mr);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(ctx.device, &mai, nullptr, &quadMem_);
    vkBindBufferMemory(ctx.device, quadBuf_, quadMem_, 0);
    void* qp;
    vkMapMemory(ctx.device, quadMem_, 0, sizeof(quad), 0, &qp);
    memcpy(qp, quad, sizeof(quad));
    vkUnmapMemory(ctx.device, quadMem_);

    return true;
}

bool Menu::loadTextures(const std::string& assetDir) {
    auto load = [&](const std::string& rel, std::vector<uint8_t>& out, int& w, int& h) {
        return loadPNG((assetDir + "/" + rel).c_str(), out, w, h);
    };
    // dirt (background)
    load("block/dirt.png", dirtRGBA_, dirtW_, dirtH_);
    // dirties loaded as top-down; loadPNG gives top-down RGBA, good.
    if (!load("gui/button.png", btnRGBA_, btnW_, btnH_)) { fprintf(stderr, "[menu] button.png load failed\n"); return false; }
    if (!load("gui/button_highlighted.png", btnHiRGBA_, btnW_, btnH_)) return false;
    if (!load("gui/text_field.png", fieldRGBA_, btnW_, btnH_)) return false;
    // The button sprite is 200x20; give DIBs a small vertical margin to avoid clamping.
    return true;
}

void Menu::createMenuTexture(VkCtx& ctx) {
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)diw_, (uint32_t)dih_, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkCreateImage(ctx.device, &ici, nullptr, &img_);
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device, img_, &mr);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device, &mai, nullptr, &imgMem_);
    vkBindImageMemory(ctx.device, img_, imgMem_, 0);

    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = (VkDeviceSize)diw_ * dih_ * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(ctx.device, &bci, nullptr, &staging_);
    vkGetBufferMemoryRequirements(ctx.device, staging_, &mr);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(ctx.device, &mai, nullptr, &stagingMem_);
    vkBindBufferMemory(ctx.device, staging_, stagingMem_, 0);

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx.device, &vci, nullptr, &view_);

    VkSamplerCreateInfo sm = {};
    sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sm.magFilter = VK_FILTER_LINEAR;
    sm.minFilter = VK_FILTER_LINEAR;
    sm.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sm.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device, &sm, nullptr, &sampler_);
}

void Menu::destroyMenuTexture(VkCtx& ctx) {
    vkDeviceWaitIdle(ctx.device);
    if (view_) vkDestroyImageView(ctx.device, view_, nullptr);
    if (img_) vkDestroyImage(ctx.device, img_, nullptr);
    if (imgMem_) vkFreeMemory(ctx.device, imgMem_, nullptr);
    if (sampler_) vkDestroySampler(ctx.device, sampler_, nullptr);
    if (staging_) vkDestroyBuffer(ctx.device, staging_, nullptr);
    if (stagingMem_) vkFreeMemory(ctx.device, stagingMem_, nullptr);
    img_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    staging_ = VK_NULL_HANDLE;
    imgMem_ = VK_NULL_HANDLE;
    stagingMem_ = VK_NULL_HANDLE;
    texReady_ = false;
}

void Menu::ensureDIB(int w, int h) {
    if (dibDC_ && diw_ == w && dih_ == h) return;
    if (dibDC_) { DeleteDC(dibDC_); dibDC_ = nullptr; }
    if (dibBmp_) { DeleteObject(dibBmp_); dibBmp_ = nullptr; }
    diw_ = w;
    dih_ = h;
    dibDC_ = CreateCompatibleDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dibBmp_ = CreateDIBSection(dibDC_, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    SelectObject(dibDC_, dibBmp_);
}

void Menu::onResize(VkCtx& ctx, int w, int h) {
    destroyMenuTexture(ctx);
    ensureDIB(w, h);            // sets diw_/dih_ first
    createMenuTexture(ctx);     // then (re)create the texture at the real size
}

// 9-slice draw a button sprite from `src` into the screen DIB.
static void slice9(HDC dst, HDC src, int tw, int th, int border,
                   float dx, float dy, float dw, float dh) {
    int b = border;
    // corners
    BitBlt(dst, (int)dx, (int)dy, b, b, src, 0, 0, SRCCOPY);
    BitBlt(dst, (int)(dx + dw - b), (int)dy, b, b, src, tw - b, 0, SRCCOPY);
    BitBlt(dst, (int)dx, (int)(dy + dh - b), b, b, src, 0, th - b, SRCCOPY);
    BitBlt(dst, (int)(dx + dw - b), (int)(dy + dh - b), b, b, src, tw - b, th - b, SRCCOPY);
    // edges
    StretchBlt(dst, (int)(dx + b), (int)dy, (int)(dw - 2 * b), b,
               src, b, 0, tw - 2 * b, b, SRCCOPY);
    StretchBlt(dst, (int)(dx + b), (int)(dy + dh - b), (int)(dw - 2 * b), b,
               src, b, th - b, tw - 2 * b, b, SRCCOPY);
    StretchBlt(dst, (int)dx, (int)(dy + b), b, (int)(dh - 2 * b),
               src, 0, b, b, th - 2 * b, SRCCOPY);
    StretchBlt(dst, (int)(dx + dw - b), (int)(dy + b), b, (int)(dh - 2 * b),
               src, tw - b, b, b, th - 2 * b, SRCCOPY);
    // center
    StretchBlt(dst, (int)(dx + b), (int)(dy + b), (int)(dw - 2 * b), (int)(dh - 2 * b),
               src, b, b, tw - 2 * b, th - 2 * b, SRCCOPY);
}

void Menu::renderToDIB(Menuscreen screen, const MenuData& data, float cx, float cy) {
    HDC dc = dibDC_;
    // dirt background tiled, scaled up 4x
    {
        int ts = dirtW_ * 4; // tile screen size
        for (int y = 0; y < dih_; y += ts)
            for (int x = 0; x < diw_; x += ts)
                StretchBlt(dc, x, y, ts, ts, dirtDC_, 0, 0, dirtW_, dirtH_, SRCCOPY);
    }
    btns_.clear();
    int w = diw_, h = dih_;
    int cxr = cx, cyr = cy;

    auto addBtn = [&](int id, float x, float y, float bw, float bh, const std::string& text) {
        bool hov = cxr >= x && cxr <= x + bw && cyr >= y && cyr <= y + bh;
        slice9(dc, hov ? btnHiDC_ : btnDC_, btnW_, btnH_, BTN_BORDER, x, y, bw, bh);
        RECT r = {(int)x, (int)y, (int)(x + bw), (int)(y + bh)};
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_CENTER);
        SelectObject(dc, font_);
        DrawTextW(dc, utf8ToWide(text).c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        btns_.push_back({id, x, y, bw, bh});
    };

    if (screen == Menuscreen::MainMenu) {
        float bw = 400, bh = 40;
        float bx = (w - bw) / 2;
        addBtn(MENU_SINGLEPLAYER, bx, 210, bw, bh, "单人游戏");
        float bw2 = 190;
        float bx2 = (w - 2 * bw2 - 20) / 2;
        addBtn(MENU_OPTIONS, bx2, 280, bw2, bh, "选项");
        addBtn(MENU_QUIT, bx2 + bw2 + 20, 280, bw2, bh, "退出游戏");
    } else if (screen == Menuscreen::SaveSelect) {
        float bw = 400, bh = 36;
        float bx = (w - bw) / 2;
        addBtn(MENU_NEWWORLD, bx, 90, bw, 40, "新建世界");
        int y = 160;
        for (int i = 0; i < (int)data.saves.size() && y < h - 90; i++) {
            addBtn(MENU_SAVE_FIRST + i, bx, y, bw, bh, data.saves[i].name);
            y += bh + 8;
        }
        addBtn(MENU_BACK, bx, h - 70, bw, 40, "返回");
    } else if (screen == Menuscreen::NewWorld) {
        float bw = 400, bh = 40;
        float bx = (w - bw) / 2;
        // title
        RECT r = {(int)bx, 90, (int)(bx + bw), 150};
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_CENTER);
        SelectObject(dc, font_);
        DrawTextW(dc, utf8ToWide(data.titleText).c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // seed label + field
        RECT lr = {(int)bx, 180, (int)(bx + 120), 220};
        SetTextAlign(dc, TA_LEFT);
        DrawTextW(dc, utf8ToWide("世界种子").c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        float fx = bx + 120, fy = 190, fw2 = bw - 120, fh2 = 22;
        slice9(dc, fieldDC_, btnW_, btnH_, 3, fx, fy, fw2, fh2);
        RECT fr = {(int)(fx + 6), (int)fy, (int)(fx + fw2 - 6), (int)(fy + fh2)};
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, utf8ToWide(data.seedText).c_str(), -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        addBtn(MENU_CREATE, bx, 280, bw, bh, "创建新的世界");
        addBtn(MENU_CANCEL, bx, 340, bw, bh, "取消");
    } else if (screen == Menuscreen::Options) {
        float bw = 300, bh = 40;
        float bx = (w - bw) / 2;
        addBtn(MENU_VIDEO, bx, 260, bw, bh, "视频设置");
        addBtn(MENU_BACK, bx, h - 90, bw, bh, "返回");
    } else if (screen == Menuscreen::VideoSettings) {
        float bw = 300, bh = 40;
        float bx = (w - bw) / 2;
        std::string label = std::string("垂直同步：") + (data.vsync ? "开" : "关");
        addBtn(MENU_VSYNC, bx, 250, bw, bh, label);
        addBtn(MENU_BACK, bx, h - 90, bw, bh, "返回");
    } else if (screen == Menuscreen::Pause) {
        float bw = 400, bh = 40;
        float bx = (w - bw) / 2;
        addBtn(MENU_SAVEANDTITLE, bx, 230, bw, bh, "保存并返回标题屏幕");
        addBtn(MENU_OPTIONS, bx, 290, bw, bh, "选项");
    }
}

int Menu::hitTest(float cx, float cy) const {
    for (auto& b : btns_)
        if (cx >= b.x && cx <= b.x + b.w && cy >= b.y && cy <= b.y + b.h) return b.id;
    return MENU_NONE;
}

int Menu::renderMenu(VkCtx& ctx, Menuscreen screen, const MenuData& data,
                     float cx, float cy, bool mouseDown) {
    ensureDIB(ctx.extent.width, ctx.extent.height);
    renderToDIB(screen, data, cx, cy);

    // upload DIB (BGRA) -> staging (RGBA)
    {
        void* p;
        if (vkMapMemory(ctx.device, stagingMem_, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS) {
            uint8_t* dst = (uint8_t*)p;
            uint8_t* src = (uint8_t*)dibBits_;
            size_t n = (size_t)diw_ * dih_;
            for (size_t i = 0; i < n; i++) {
                dst[i * 4 + 0] = src[i * 4 + 2];
                dst[i * 4 + 1] = src[i * 4 + 1];
                dst[i * 4 + 2] = src[i * 4 + 0];
                dst[i * 4 + 3] = src[i * 4 + 3];
            }
            vkUnmapMemory(ctx.device, stagingMem_);
        }
    }

    // frame
    uint32_t imageIndex;
    VkCtx::Frame& f = ctx.frames[frameIdx_ % ctx.frames.size()];
    if (!ctx.acquireNext(f, imageIndex)) {
        ctx.recreateSwapchain(diw_, dih_);
        return MENU_NONE;
    }
    frameIdx_++;

    VkCommandBuffer cb = ctx.cmd;
    if (ctx.lastSubmitFence) vkWaitForFences(ctx.device, 1, &ctx.lastSubmitFence, VK_TRUE, UINT64_MAX);
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);

    // transition + upload
    VkImageMemoryBarrier bar = {};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = img_;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bar);
    VkBufferImageCopy bic = {};
    bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    bic.imageExtent = {(uint32_t)diw_, (uint32_t)dih_, 1};
    vkCmdCopyBufferToImage(cb, staging_, img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bar);

    VkClearValue clear = {};
    clear.color = {{0, 0, 0, 1}};
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = ctx.renderPass;
    rp.framebuffer = ctx.framebuffers[imageIndex];
    rp.renderArea = {{0, 0}, ctx.extent};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = {0, 0, (float)ctx.extent.width, (float)ctx.extent.height, 0, 1};
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, ctx.extent};
    vkCmdSetScissor(cb, 0, 1, &sc);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    VkBuffer qb = quadBuf_;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &qb, &off);
    vkCmdDraw(cb, 6, 1, 0, 0);

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
    ctx.lastSubmitFence = f.fence;
    if (!ctx.presentImage(imageIndex, f)) { ctx.recreateSwapchain(diw_, dih_); return MENU_NONE; }

    if (mouseDown && !prevMouseDown_) return hitTest(cx, cy);
    prevMouseDown_ = mouseDown;
    return MENU_NONE;
}

bool Menu::debugSaveMenu(const std::string& path) const {
    if (!dibBits_) return false;
    const uint8_t* src = (const uint8_t*)dibBits_;
    std::vector<uint8_t> bgra(src, src + (size_t)diw_ * dih_ * 4);
    return savePNG(path, diw_, dih_, bgra);
}

void Menu::shutdown(VkCtx& ctx) {
    destroyMenuTexture(ctx);
    vkDeviceWaitIdle(ctx.device);
    if (quadBuf_) vkDestroyBuffer(ctx.device, quadBuf_, nullptr);
    if (quadMem_) vkFreeMemory(ctx.device, quadMem_, nullptr);
    if (mpool_) vkDestroyDescriptorPool(ctx.device, mpool_, nullptr);
    if (dsl_) vkDestroyDescriptorSetLayout(ctx.device, dsl_, nullptr);
    if (layout_) vkDestroyPipelineLayout(ctx.device, layout_, nullptr);
    if (pipe_) vkDestroyPipeline(ctx.device, pipe_, nullptr);
    if (font_) DeleteObject(font_);
    if (dibDC_) DeleteDC(dibDC_);
    if (dibBmp_) DeleteObject(dibBmp_);
    if (btnDC_) DeleteDC(btnDC_);
    if (btnBmp_) DeleteObject(btnBmp_);
    if (btnHiDC_) DeleteDC(btnHiDC_);
    if (btnHiBmp_) DeleteObject(btnHiBmp_);
    if (fieldDC_) DeleteDC(fieldDC_);
    if (fieldBmp_) DeleteObject(fieldBmp_);
    if (dirtDC_) DeleteDC(dirtDC_);
    if (dirtBmp_) DeleteObject(dirtBmp_);
    quadBuf_ = VK_NULL_HANDLE;
    quadMem_ = VK_NULL_HANDLE;
    mpool_ = VK_NULL_HANDLE;
    dsl_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pipe_ = VK_NULL_HANDLE;
    font_ = nullptr;
}

