#include "renderer.hpp"
#include "camera.hpp"
#include "player.hpp"
#include "window.hpp"
#include "png.hpp"
#include "raycast.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::vector<char> data;
    if (!f) return data;
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    data.resize((size_t)size);
    f.read(data.data(), size);
    return data;
}

static VkShaderModule loadModule(VkDevice dev, const std::string& path) {
    auto data = readFile(path);
    if (data.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = data.size();
    ci.pCode = (const uint32_t*)data.data();
    VkShaderModule m;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

static bool createBuffer(VkCtx& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props, Buffer2& out) {
    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &ci, nullptr, &out.b) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, out.b, &mr);
    uint32_t mt = ctx.findMemoryType(mr.memoryTypeBits, props);
    if (mt == UINT32_MAX) {
        if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            mt = ctx.findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (mt == UINT32_MAX) return false;
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &out.m) != VK_SUCCESS) return false;
    vkBindBufferMemory(ctx.device, out.b, out.m, 0);
    return true;
}

static VkPipeline makePipeline(VkCtx& ctx, VkRenderPass rp, VkPipelineLayout layout,
                               const std::string& shaderDir, const char* vs, const char* fs,
                               VkVertexInputBindingDescription bindings[], VkVertexInputAttributeDescription attrs[],
                               uint32_t bindingCount, uint32_t attrCount,
                               VkPrimitiveTopology topo, VkCullModeFlags cull,
                               bool blend, bool depthTest, bool depthWrite) {
    auto vert = loadModule(ctx.device, shaderDir + "/" + vs);
    auto frag = loadModule(ctx.device, shaderDir + "/" + fs);
    if (!vert || !frag) return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = bindingCount;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = attrCount;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = topo;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState ba = {};
    ba.colorWriteMask = 0xF;
    if (blend) {
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
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
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dynCI;
    pi.layout = layout;
    pi.renderPass = rp;
    pi.subpass = 0;

    VkPipeline p;
    VkResult r = vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p);
    vkDestroyShaderModule(ctx.device, vert, nullptr);
    vkDestroyShaderModule(ctx.device, frag, nullptr);
    return r == VK_SUCCESS ? p : VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------
bool Renderer::init(VkCtx& ctx, Window& win, const std::string& assetDir,
                    const std::string& shaderDir) {
    ctxPtr_ = &ctx;
    windowW_ = win.width();
    windowH_ = win.height();

    atlas_ = buildAtlas(assetDir + "/block");
    if (!atlas_.built) { fprintf(stderr, "[renderer] atlas build failed\n"); return false; }
    createAtlasTexture(ctx);
    createDescriptors(ctx);

    // --- layouts ---
    VkPushConstantRange terrPC = {VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &terrainDSL_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &terrPC;
    vkCreatePipelineLayout(ctx.device, &pl, nullptr, &terrainLayout_);

    pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &uiDSL_;
    vkCreatePipelineLayout(ctx.device, &pl, nullptr, &uiLayout_);

    pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &skyDSL_;
    vkCreatePipelineLayout(ctx.device, &pl, nullptr, &skyLayout_);

    // --- buffers ---
    createBuffer(ctx, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, terrainUBO_);
    createBuffer(ctx, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, skyUBO_);
    createBuffer(ctx, 1 << 20, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uiBuf_);
    vkMapMemory(ctx.device, uiBuf_.m, 0, VK_WHOLE_SIZE, 0, &uiMap_);

    // --- descriptor sets ---
    VkDescriptorBufferInfo tbi = {terrainUBO_.b, 0, 256};
    VkDescriptorImageInfo tii = {atlasSampler_, atlasView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w1[2] = {};
    w1[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1[0].dstSet = terrainSet_;
    w1[0].dstBinding = 0;
    w1[0].descriptorCount = 1;
    w1[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w1[0].pBufferInfo = &tbi;
    w1[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1[1].dstSet = terrainSet_;
    w1[1].dstBinding = 1;
    w1[1].descriptorCount = 1;
    w1[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w1[1].pImageInfo = &tii;
    vkUpdateDescriptorSets(ctx.device, 2, w1, 0, nullptr);

    VkDescriptorBufferInfo sbi = {skyUBO_.b, 0, 64};
    VkWriteDescriptorSet w2 = {};
    w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w2.dstSet = skySet_;
    w2.dstBinding = 0;
    w2.descriptorCount = 1;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w2.pBufferInfo = &sbi;
    vkUpdateDescriptorSets(ctx.device, 1, &w2, 0, nullptr);

    VkDescriptorImageInfo uii = {atlasSampler_, atlasView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w3 = {};
    w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w3.dstSet = uiSet_;
    w3.dstBinding = 0;
    w3.descriptorCount = 1;
    w3.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w3.pImageInfo = &uii;
    vkUpdateDescriptorSets(ctx.device, 1, &w3, 0, nullptr);

    // --- pipelines ---
    VkVertexInputBindingDescription terrBinding = {0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription terrAttrs[2] = {};
    terrAttrs[0] = {0, 0, VK_FORMAT_R8G8B8A8_SINT, 0};
    terrAttrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT, 4};

    terrainPipe_ = makePipeline(ctx, ctx.renderPass, terrainLayout_, shaderDir,
                                "terrain.vert.spv", "terrain.frag.spv",
                                &terrBinding, terrAttrs, 1, 2,
                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT,
                                false, true, true);
    waterPipe_ = makePipeline(ctx, ctx.renderPass, terrainLayout_, shaderDir,
                              "water.vert.spv", "water.frag.spv",
                              &terrBinding, terrAttrs, 1, 2,
                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT,
                              true, true, false);

    skyPipe_ = makePipeline(ctx, ctx.renderPass, skyLayout_, shaderDir,
                            "sky.vert.spv", "sky.frag.spv",
                            nullptr, nullptr, 0, 0,
                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE,
                            false, false, false);

    VkVertexInputBindingDescription uiBinding = {0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription uiAttrs[3] = {};
    uiAttrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    uiAttrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8};
    uiAttrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};
    uiPipe_ = makePipeline(ctx, ctx.renderPass, uiLayout_, shaderDir,
                           "ui.vert.spv", "ui.frag.spv",
                           &uiBinding, uiAttrs, 1, 3,
                           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE,
                           true, false, false);

    if (!terrainPipe_ || !waterPipe_ || !skyPipe_ || !uiPipe_) {
        fprintf(stderr, "[renderer] pipeline failure: %d %d %d %d\n",
                (int)(terrainPipe_ != VK_NULL_HANDLE), (int)(waterPipe_ != VK_NULL_HANDLE),
                (int)(skyPipe_ != VK_NULL_HANDLE), (int)(uiPipe_ != VK_NULL_HANDLE));
        return false;
    }

    // screenshot buffer
    shotBufReady_ = false;
    return true;
}

void Renderer::setWorld(World& w) {
    world_ = &w;
    w.onDestroyChunk = [this](Chunk& c) { destroyChunkBuffers(*ctxPtr_, c); };
}

void Renderer::createAtlasTexture(VkCtx& ctx) {
    int w = atlas_.width, h = atlas_.height;
    uint32_t mips = 1;
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = mips;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(ctx.device, &ici, nullptr, &atlasImage_) != VK_SUCCESS) {
        fprintf(stderr, "[renderer] atlas image create failed\n");
        return;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device, atlasImage_, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(ctx.device, &ai, nullptr, &atlasMem_) != VK_SUCCESS) {
        fprintf(stderr, "[renderer] atlas image alloc failed\n");
        return;
    }
    vkBindImageMemory(ctx.device, atlasImage_, atlasMem_, 0);

    Buffer2 staging;
    if (!createBuffer(ctx, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
        fprintf(stderr, "[renderer] atlas staging alloc failed\n");
        return;
    }
    void* ptr;
    vkMapMemory(ctx.device, staging.m, 0, VK_WHOLE_SIZE, 0, &ptr);
    memcpy(ptr, atlas_.rgba.data(), w * h * 4);
    vkUnmapMemory(ctx.device, staging.m);

    VkCommandBuffer cb = ctx.cmd;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b0 = {};
    b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b0.image = atlasImage_;
    b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b0.subresourceRange.levelCount = mips;
    b0.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b0);

    VkBufferImageCopy bic = {};
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, staging.b, atlasImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);

    // mip chain
    for (uint32_t m = 1; m < mips; m++) {
        VkImageMemoryBarrier br = {};
        br.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        br.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        br.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        br.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        br.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        br.image = atlasImage_;
        br.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        br.subresourceRange.baseMipLevel = m - 1;
        br.subresourceRange.levelCount = 1;
        br.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &br);
        int sw = std::max(1, w >> (m - 1)), sh = std::max(1, h >> (m - 1));
        int dw = std::max(1, w >> m), dh = std::max(1, h >> m);
        VkImageBlit blit = {};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1};
        blit.srcOffsets[1] = {sw, sh, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        blit.dstOffsets[1] = {dw, dh, 1};
        vkCmdBlitImage(cb, atlasImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       atlasImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }
    // level 0 was left in TRANSFER_SRC (blit source); move it back to TRANSFER_DST
    {
        VkImageMemoryBarrier br = {};
        br.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        br.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        br.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        br.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        br.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        br.image = atlasImage_;
        br.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        br.subresourceRange.baseMipLevel = 0;
        br.subresourceRange.levelCount = 1;
        br.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &br);
    }
    VkImageMemoryBarrier b1 = {};
    b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b1.image = atlasImage_;
    b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b1.subresourceRange.levelCount = mips;
    b1.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b1);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    if (vkQueueSubmit(ctx.graphics, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
        fprintf(stderr, "[renderer] atlas upload submit failed\n");
    vkQueueWaitIdle(ctx.graphics);
    staging.destroy(ctx.device);

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = atlasImage_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = mips;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(ctx.device, &vci, nullptr, &atlasView_);

    VkSamplerCreateInfo sm = {};
    sm.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sm.magFilter = VK_FILTER_NEAREST;
    sm.minFilter = VK_FILTER_NEAREST;
    sm.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sm.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sm.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sm.anisotropyEnable = VK_FALSE;
    sm.maxLod = (float)mips;
    vkCreateSampler(ctx.device, &sm, nullptr, &atlasSampler_);
}

void Renderer::createDescriptors(VkCtx& ctx) {
    VkDescriptorSetLayoutBinding b0 = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutBinding b1 = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutBinding bs[2] = {b0, b1};
    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings = bs;
    vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &terrainDSL_);

    VkDescriptorSetLayoutBinding ui0 = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT};
    li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &ui0;
    vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &uiDSL_);

    VkDescriptorSetLayoutBinding sky0 = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT};
    li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &sky0;
    vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &skyDSL_);


    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 8;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 8;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 8;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    vkCreateDescriptorPool(ctx.device, &pci, nullptr, &pool_);

    VkDescriptorSetAllocateInfo aci = {};
    aci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    aci.descriptorPool = pool_;
    aci.descriptorSetCount = 1;
    aci.pSetLayouts = &terrainDSL_;
    vkAllocateDescriptorSets(ctx.device, &aci, &terrainSet_);
    aci.pSetLayouts = &skyDSL_;
    vkAllocateDescriptorSets(ctx.device, &aci, &skySet_);
    aci.pSetLayouts = &uiDSL_;
    vkAllocateDescriptorSets(ctx.device, &aci, &uiSet_);
}

void Renderer::updateTerrainUBO(VkCtx& ctx, const Camera& cam, float renderDist) {
    Mat4 proj = Mat4::perspective(1.22f, (float)windowW_ / (float)windowH_, 0.02f, 512.0f);
    Mat4 vp = Mat4::mul(proj, cam.view());
    cachedVP_ = vp;

    // day/night cycle: timeOfDay in [0,1]; 0.0 = noon, 0.5 = midnight
    float tod = (float)(timeOfDay_);
    float sunAngle = (tod - 0.25f) * 6.2831853f; // sun rises ~0.25, sets ~0.75
    Vec3 sunDir(0, 0, 1);
    sunDir = Vec3(std::sin(sunAngle), std::cos(sunAngle), 0.35f);
    sunDir = normalize(sunDir);
    float dayLight = clampf(std::cos(sunAngle), 0.0f, 1.0f);
    dayLight = 0.35f + 0.65f * dayLight;

    struct UboData {
        Mat4 viewProj;
        float camX, camY, camZ, underwater;  // underwater: 1 when camera is submerged
        float fogStart, fogEnd, skyR, skyG;
        float skyB, atlasPx, tilesX, tilePx;
        float dayLight, sunX, sunY, sunZ;
    } u;
    u.viewProj = vp;
    u.camX = cam.pos.x; u.camY = cam.pos.y; u.camZ = cam.pos.z;
    bool underwater = world_ &&
        world_->getBlock((int)std::floor(cam.pos.x),
                         (int)std::floor(cam.pos.y),
                         (int)std::floor(cam.pos.z)) == B_WATER;
    u.underwater = underwater ? 1.0f : 0.0f;
    u.fogStart = renderDist * 16.0f * 0.70f;
    u.fogEnd = renderDist * 16.0f * 0.94f;
    u.skyR = 0.60f; u.skyG = 0.77f;
    u.skyB = 0.90f;
    u.atlasPx = (float)atlas_.width;
    u.tilesX = (float)atlas_.tilesX;
    u.tilePx = (float)atlas_.tileSize;
    u.dayLight = dayLight;
    u.sunX = sunDir.x; u.sunY = sunDir.y; u.sunZ = sunDir.z;
    void* ptr;
    vkMapMemory(ctx.device, terrainUBO_.m, 0, sizeof(UboData), 0, &ptr);
    memcpy(ptr, &u, sizeof(UboData));
    vkUnmapMemory(ctx.device, terrainUBO_.m);

    struct SkyUbo { float hx, hy, hz, ha; float zx, zy, zz, za; } s;
    // day sky
    Vec3 horizon(0.60f, 0.77f, 0.90f);
    Vec3 zenith(0.35f, 0.55f, 0.86f);
    // night sky
    Vec3 nHorizon(0.10f, 0.12f, 0.22f);
    Vec3 nZenith(0.04f, 0.05f, 0.14f);
    horizon = lerp(nHorizon, horizon, dayLight);
    zenith = lerp(nZenith, zenith, dayLight);
    s.hx = horizon.x; s.hy = horizon.y; s.hz = horizon.z;
    s.ha = underwater ? 1.0f : 0.0f;   // reuse horizon.a as the underwater flag
    s.zx = zenith.x; s.zy = zenith.y; s.zz = zenith.z;
    vkMapMemory(ctx.device, skyUBO_.m, 0, sizeof(SkyUbo), 0, &ptr);
    memcpy(ptr, &s, sizeof(SkyUbo));
    vkUnmapMemory(ctx.device, skyUBO_.m);
}

// ---------------------------------------------------------------------------
// chunk GPU upload
// ---------------------------------------------------------------------------
static void uploadPart(VkCtx& ctx, Chunk& c, bool opaque,
                       const std::vector<TerrainVertex>& verts,
                       const std::vector<uint32_t>& idx) {
    uint64_t& buf = opaque ? c.opaqueBuf : c.waterBuf;
    uint64_t& mem = opaque ? c.opaqueMem : c.waterMem;
    uint64_t& alloc = opaque ? c.opaqueAlloc : c.waterAlloc;
    uint32_t& count = opaque ? c.opaqueCount : c.waterCount;
    uint64_t& vertBytes = opaque ? c.opaqueVertBytes : c.waterVertBytes;

    VkBuffer buffer = (VkBuffer)(uintptr_t)buf;
    VkDeviceMemory memory = (VkDeviceMemory)(uintptr_t)mem;
    VkDeviceSize needed = (VkDeviceSize)verts.size() * sizeof(TerrainVertex) + (VkDeviceSize)idx.size() * sizeof(uint32_t);

    if (verts.empty() && idx.empty()) {
        if (buffer) vkDestroyBuffer(ctx.device, buffer, nullptr);
        if (memory) vkFreeMemory(ctx.device, memory, nullptr);
        buf = 0; mem = 0; alloc = 0; count = 0; vertBytes = 0;
        return;
    }
    if (!buffer || alloc < needed) {
        if (buffer) vkDestroyBuffer(ctx.device, buffer, nullptr);
        if (memory) vkFreeMemory(ctx.device, memory, nullptr);
        Buffer2 nb;
        if (!createBuffer(ctx, needed, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nb))
            return;
        buf = (uint64_t)(uintptr_t)nb.b;
        mem = (uint64_t)(uintptr_t)nb.m;
        alloc = (uint64_t)needed;
        buffer = nb.b;
        memory = nb.m;
    }
    void* ptr;
    if (vkMapMemory(ctx.device, memory, 0, needed, 0, &ptr) != VK_SUCCESS) return;
    size_t vb = verts.size() * sizeof(TerrainVertex);
    memcpy(ptr, verts.data(), vb);
    memcpy((uint8_t*)ptr + vb, idx.data(), idx.size() * sizeof(uint32_t));
    vkUnmapMemory(ctx.device, memory);
    count = (uint32_t)idx.size();
    vertBytes = (uint64_t)vb;
}

void Renderer::gpuSync(VkCtx& ctx) {
    if (ctx.lastSubmitFence)
        vkWaitForFences(ctx.device, 1, &ctx.lastSubmitFence, VK_TRUE, UINT64_MAX);
}

const uint8_t Renderer::kInvBlocks[] = {
    B_GRASS, B_STONE, B_COBBLE, B_PLANKS, B_LOG, B_DIRT, B_SAND, B_GRAVEL,
    B_GLASS, B_LEAVES, B_SNOW, B_COAL, B_IRON, B_GOLD, B_DIAMOND, B_REDSTONE,
    B_BEDROCK,
};
const int Renderer::kInvCount = (int)(sizeof(kInvBlocks) / sizeof(kInvBlocks[0]));

uint8_t Renderer::selectedBlock() const {
    return placementBlock_;
}

void Renderer::setInventoryOpen(bool open) {
    if (invOpen_ && !open) {
        // closing the inventory: place the picked block into the selected hotbar slot
        hotbar_[selectedSlot_] = placementBlock_;
    }
    invOpen_ = open;
}

void Renderer::destroyChunkBuffers(VkCtx& ctx, Chunk& c) {
    if (c.opaqueBuf) vkDestroyBuffer(ctx.device, (VkBuffer)(uintptr_t)c.opaqueBuf, nullptr);
    if (c.opaqueMem) vkFreeMemory(ctx.device, (VkDeviceMemory)(uintptr_t)c.opaqueMem, nullptr);
    if (c.waterBuf) vkDestroyBuffer(ctx.device, (VkBuffer)(uintptr_t)c.waterBuf, nullptr);
    if (c.waterMem) vkFreeMemory(ctx.device, (VkDeviceMemory)(uintptr_t)c.waterMem, nullptr);
    c.opaqueBuf = c.opaqueMem = c.waterBuf = c.waterMem = 0;
    c.opaqueAlloc = c.waterAlloc = c.opaqueCount = c.waterCount = 0;
    c.opaqueVertBytes = c.waterVertBytes = 0;
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------
bool Renderer::render(VkCtx& ctx, const Camera& cam, Player& player, Input& in, float dt,
                      float renderDist, bool drawUI) {
    windowW_ = ctx.extent.width;
    windowH_ = ctx.extent.height;
    if (in.keys['T']) timeScale_ = 40.0f / 1200.0f; // hold T for time-lapse
    else timeScale_ = 1.0f / 1200.0f;
    timeOfDay_ += dt * timeScale_;
    if (timeOfDay_ >= 1.0f) timeOfDay_ -= 1.0f;

    if (pendingShot_.empty()) shotTaken_ = false;

    uint32_t imageIndex;
    VkCtx::Frame& f = ctx.frames[frameIdx_ % ctx.frames.size()];
    // Ensure the shared command buffer is free BEFORE acquiring (acquireNext resets
    // f.fence, which may be the same fence we would otherwise wait on below).
    if (ctx.lastSubmitFence)
        vkWaitForFences(ctx.device, 1, &ctx.lastSubmitFence, VK_TRUE, UINT64_MAX);
    // Only now safe to rewrite the host-visible UBO — the GPU is done with it.
    updateTerrainUBO(ctx, cam, renderDist);
    if (!ctx.acquireNext(f, imageIndex)) {
        ctx.recreateSwapchain(windowW_, windowH_);
        return false;
    }
    frameIdx_++;
    VkCommandBuffer cb = ctx.cmd;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);

    VkClearValue clears[2];
    clears[0].color = {{0.35f, 0.55f, 0.86f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = ctx.renderPass;
    rp.framebuffer = ctx.framebuffers[imageIndex];
    rp.renderArea = {{0, 0}, ctx.extent};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    // Upload pending chunk meshes BEFORE render pass (uploadPart does
    // vkCreateBuffer/vkAllocateMemory which must not run during render pass).
    if (world_) {
        world_->forEachChunk([&](std::shared_ptr<Chunk>& c, int, int) {
            if (c->needsUpload.exchange(false)) {
                std::lock_guard<std::mutex> lk(c->meshLock);
                ChunkMeshData local = c->mesh;
                uploadPart(ctx, *c, true, local.opaqueVerts, local.opaqueIdx);
                uploadPart(ctx, *c, false, local.waterVerts, local.waterIdx);
            }
        });
    }

    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {0, 0, (float)ctx.extent.width, (float)ctx.extent.height, 0, 1};
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, ctx.extent};
    vkCmdSetScissor(cb, 0, 1, &sc);

    // sky
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, skyLayout_, 0, 1, &skySet_, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);

    drawChunks(ctx, cam);

    if (drawUI) {
        drawUIOverlay(ctx, cam, in);
    }

    vkCmdEndRenderPass(cb);

    // screenshot copy (after render pass, image is in PRESENT_SRC layout)
    bool takeShot = !pendingShot_.empty() && !shotTaken_;
    if (takeShot) {
        if (!shotBufReady_ ||
            (uint32_t)shotW_ != ctx.extent.width || (uint32_t)shotH_ != ctx.extent.height) {
            if (shotBuf_) vkDestroyBuffer(ctx.device, shotBuf_, nullptr);
            if (shotMem_) vkFreeMemory(ctx.device, shotMem_, nullptr);
            shotW_ = ctx.extent.width;
            shotH_ = ctx.extent.height;
            Buffer2 b;
            if (createBuffer(ctx, (VkDeviceSize)shotW_ * shotH_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b)) {
                shotBuf_ = b.b;
                shotMem_ = b.m;
                shotBufReady_ = true;
            }
        }
        if (shotBufReady_) {
            VkImageMemoryBarrier br = {};
            br.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            br.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            br.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            br.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            br.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            br.image = ctx.swapImages[imageIndex];
            br.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            br.subresourceRange.levelCount = 1;
            br.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &br);
            VkBufferImageCopy bic = {};
            bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bic.imageSubresource.layerCount = 1;
            bic.imageExtent = {(uint32_t)shotW_, (uint32_t)shotH_, 1};
            vkCmdCopyImageToBuffer(cb, ctx.swapImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   shotBuf_, 1, &bic);
            br.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            br.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &br);
            shotTaken_ = true;
        }
    }

    vkEndCommandBuffer(cb);
    ctx.lastSubmitFence = f.fence;
    if (!ctx.presentImage(imageIndex, f)) {
        ctx.recreateSwapchain(windowW_, windowH_);
        return false;
    }

    if (shotTaken_ && !pendingShot_.empty()) {
        vkWaitForFences(ctx.device, 1, &f.fence, VK_TRUE, UINT64_MAX);
        std::vector<uint8_t> bgra((size_t)shotW_ * shotH_ * 4);
        void* ptr = nullptr;
        if (vkMapMemory(ctx.device, shotMem_, 0, VK_WHOLE_SIZE, 0, &ptr) == VK_SUCCESS) {
            memcpy(bgra.data(), ptr, bgra.size());
            vkUnmapMemory(ctx.device, shotMem_);
            savePNG(pendingShot_, shotW_, shotH_, bgra);
        }
        pendingShot_.clear();
    }

    frames_++;
    fpsTimer_ += dt;
    if (fpsTimer_ >= 0.5f) {
        fps_ = frames_ / fpsTimer_;
        frames_ = 0;
        fpsTimer_ = 0;
    }
    return true;
}

void Renderer::drawChunks(VkCtx& ctx, const Camera& cam) {
    Frustum fr;
    fr.extract(cachedVP_);

    // Pass 1: draw ALL opaque geometry
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipe_);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainLayout_, 0, 1, &terrainSet_, 0, nullptr);

    int draws = 0;
    world_->forEachChunk([&](std::shared_ptr<Chunk>& c, int cx, int cz) {
        if (c->state.load() < 2) return;
        float minX = cx * 16.0f, minZ = cz * 16.0f;
        if (!fr.testAABB(minX, 0, minZ, minX + 16, WORLD_HEIGHT, minZ + 16)) return;
        if (!c->opaqueBuf || !c->opaqueCount) return;
        draws++;

        Vec3 origin((float)(cx * 16), 0, (float)(cz * 16));
        vkCmdPushConstants(ctx.cmd, terrainLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, &origin);
        VkBuffer vb = (VkBuffer)(uintptr_t)c->opaqueBuf;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
        vkCmdBindIndexBuffer(ctx.cmd, vb, c->opaqueVertBytes, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(ctx.cmd, c->opaqueCount, 1, 0, 0, 1);
    });

    // Pass 2: draw ALL water (semi-transparent, must be after opaque)
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipe_);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainLayout_, 0, 1, &terrainSet_, 0, nullptr);

    world_->forEachChunk([&](std::shared_ptr<Chunk>& c, int cx, int cz) {
        if (c->state.load() < 2) return;
        float minX = cx * 16.0f, minZ = cz * 16.0f;
        if (!fr.testAABB(minX, 0, minZ, minX + 16, WORLD_HEIGHT, minZ + 16)) return;
        if (!c->waterBuf || !c->waterCount) return;

        Vec3 origin((float)(cx * 16), 0, (float)(cz * 16));
        vkCmdPushConstants(ctx.cmd, terrainLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, &origin);
        VkBuffer vb = (VkBuffer)(uintptr_t)c->waterBuf;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
        vkCmdBindIndexBuffer(ctx.cmd, vb, c->waterVertBytes, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(ctx.cmd, c->waterCount, 1, 0, 0, 1);
    });

    debugDraws_ = draws;
}

// ---------------------------------------------------------------------------
// overlay UI
// ---------------------------------------------------------------------------
void Renderer::drawUIOverlay(VkCtx& ctx, const Camera& cam, Input& in) {
    // block highlight: project the targeted block's edges into screen space and
    // draw thin quads through the (working) UI pipeline.
    Vec3 fwd = cam.forward();
    RayHit hit = raycastWorld(*world_, cam.pos, fwd, 6.0f);

    Mat4 vp = cachedVP_;

    // build UI quads
    if (in.keys['1']) { selectedSlot_ = 0; placementBlock_ = hotbar_[0]; }
    if (in.keys['2']) { selectedSlot_ = 1; placementBlock_ = hotbar_[1]; }
    if (in.keys['3']) { selectedSlot_ = 2; placementBlock_ = hotbar_[2]; }
    if (in.keys['4']) { selectedSlot_ = 3; placementBlock_ = hotbar_[3]; }
    if (in.keys['5']) { selectedSlot_ = 4; placementBlock_ = hotbar_[4]; }
    if (in.keys['6']) { selectedSlot_ = 5; placementBlock_ = hotbar_[5]; }
    if (in.keys['7']) { selectedSlot_ = 6; placementBlock_ = hotbar_[6]; }
    if (in.keys['8']) { selectedSlot_ = 7; placementBlock_ = hotbar_[7]; }
    if (in.keys['9']) { selectedSlot_ = 8; placementBlock_ = hotbar_[8]; }
    if (in.scrollAccum > 0) { selectedSlot_ = (selectedSlot_ - 1 + 9) % 9; placementBlock_ = hotbar_[selectedSlot_]; }
    if (in.scrollAccum < 0) { selectedSlot_ = (selectedSlot_ + 1) % 9; placementBlock_ = hotbar_[selectedSlot_]; }

    std::vector<UIVertex> quads;
    quads.reserve(512);

    auto ndcX = [&](float px) { return px / (float)windowW_ * 2.0f - 1.0f; };
    auto ndcY = [&](float py) { return py / (float)windowH_ * 2.0f - 1.0f; };
    auto pushQuad = [&](float x0, float y0, float x1, float y1, int tile,
                        float r, float g, float b, float a) {
        float u0 = atlas_.tileU(tile, 0), v0 = atlas_.tileV(tile, 0);
        float u1 = atlas_.tileU(tile, 2), v1 = atlas_.tileV(tile, 2);
        UIVertex q[6] = {
            {ndcX(x0), ndcY(y0), u0, v0, r, g, b, a},
            {ndcX(x1), ndcY(y0), u1, v0, r, g, b, a},
            {ndcX(x1), ndcY(y1), u1, v1, r, g, b, a},
            {ndcX(x0), ndcY(y0), u0, v0, r, g, b, a},
            {ndcX(x1), ndcY(y1), u1, v1, r, g, b, a},
            {ndcX(x0), ndcY(y1), u0, v1, r, g, b, a},
        };
        for (auto& v : q) quads.push_back(v);
    };

    // block highlight: draw the targeted block's 12 edges as thin screen-space quads
    if (hit.hit) {
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = vp.m[i];
        auto project = [&](float px, float py, float pz, float& ox, float& oy) {
            float c0 = m[0] * px + m[4] * py + m[8] * pz + m[12];
            float c1 = m[1] * px + m[5] * py + m[9] * pz + m[13];
            float c3 = m[3] * px + m[7] * py + m[11] * pz + m[15];
            if (c3 <= 0.001f) return false;
            ox = (c0 / c3 * 0.5f + 0.5f) * (float)windowW_;
            oy = (c1 / c3 * 0.5f + 0.5f) * (float)windowH_;
            return true;
        };
        // oriented quad (parallelogram) for a screen-space line segment
        auto pushLineQuad = [&](float ax, float ay, float bx, float by, float t,
                                float r, float g, float b, float a) {
            float dx = bx - ax, dy = by - ay;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.6f) return;
            float nx = -dy / len * t, ny = dx / len * t;
            float u0 = atlas_.tileU(T_WHITE, 0), v0 = atlas_.tileV(T_WHITE, 0);
            float u1 = atlas_.tileU(T_WHITE, 2), v1 = atlas_.tileV(T_WHITE, 2);
            UIVertex q[6] = {
                {ndcX(ax + nx), ndcY(ay + ny), u0, v0, r, g, b, a},
                {ndcX(bx + nx), ndcY(by + ny), u1, v0, r, g, b, a},
                {ndcX(bx - nx), ndcY(by - ny), u1, v1, r, g, b, a},
                {ndcX(ax + nx), ndcY(ay + ny), u0, v0, r, g, b, a},
                {ndcX(bx - nx), ndcY(by - ny), u1, v1, r, g, b, a},
                {ndcX(ax - nx), ndcY(ay - ny), u0, v1, r, g, b, a},
            };
            for (auto& v : q) quads.push_back(v);
        };
        float bx = (float)hit.x, by = (float)hit.y, bz = (float)hit.z;
        Vec3 cube[8] = {
            {bx, by, bz}, {bx + 1, by, bz}, {bx + 1, by + 1, bz}, {bx, by + 1, bz},
            {bx, by, bz + 1}, {bx + 1, by, bz + 1}, {bx + 1, by + 1, bz + 1}, {bx, by + 1, bz + 1}};
        int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        float t = 1.0f;
        for (auto& e : edges) {
            float ax, ay, b2x, b2y;
            if (!project(cube[e[0]].x, cube[e[0]].y, cube[e[0]].z, ax, ay)) continue;
            if (!project(cube[e[1]].x, cube[e[1]].y, cube[e[1]].z, b2x, b2y)) continue;
            pushLineQuad(ax, ay, b2x, b2y, t, 0, 0, 0, 0.85f);
        }
    }

    // crosshair (hidden while inventory open)
    if (!invOpen_) {
        float cx = windowW_ * 0.5f, cy = windowH_ * 0.5f;
        float len = 6.0f, th = 1.0f;
        pushQuad(cx - th, cy - len, cx + th, cy + len, T_WHITE, 0, 0, 0, 0.85f);
        pushQuad(cx - len, cy - th, cx + len, cy + th, T_WHITE, 0, 0, 0, 0.85f);
    }

    // hotbar
    const float slotSize = 40.0f, gap = 2.0f;
    float total = 9 * slotSize + 8 * gap;
    float hx0 = (windowW_ - total) * 0.5f;
    float hy0 = windowH_ - slotSize - 8.0f;
    for (int i = 0; i < 9; i++) {
        float x0 = hx0 + i * (slotSize + gap);
        bool sel = i == selectedSlot_;
        pushQuad(x0, hy0, x0 + slotSize, hy0 + slotSize, T_WHITE,
                 sel ? 0.45f : 0.15f, sel ? 0.45f : 0.15f, sel ? 0.45f : 0.15f, 0.85f);
        int tile = blockTile(hotbar_[i], F_PY);
        pushQuad(x0 + 5, hy0 + 5, x0 + slotSize - 5, hy0 + slotSize - 5, tile, 1, 1, 1, 1);
        if (sel) {
            pushQuad(x0, hy0, x0 + slotSize, hy0 + 1.5f, T_WHITE, 1, 1, 1, 1);
            pushQuad(x0, hy0 + slotSize - 1.5f, x0 + slotSize, hy0 + slotSize, T_WHITE, 1, 1, 1, 1);
            pushQuad(x0, hy0, x0 + 1.5f, hy0 + slotSize, T_WHITE, 1, 1, 1, 1);
            pushQuad(x0 + slotSize - 1.5f, hy0, x0 + slotSize, hy0 + slotSize, T_WHITE, 1, 1, 1, 1);
        }
    }

    // inventory (E) screen
    if (invOpen_) {
        const float invSlot = 44.0f, invGap = 4.0f;
        int rows = (kInvCount + kInvCols - 1) / kInvCols;
        float gridW = kInvCols * invSlot + (kInvCols - 1) * invGap;
        float gridH = rows * invSlot + (rows - 1) * invGap;
        float gx0 = (windowW_ - gridW) * 0.5f;
        float gy0 = (windowH_ - gridH) * 0.5f;
        // dim background
        pushQuad(0, 0, (float)windowW_, (float)windowH_, T_WHITE, 0.05f, 0.05f, 0.05f, 0.55f);
        int currentInvIdx = -1;
        for (int i = 0; i < kInvCount; i++)
            if (kInvBlocks[i] == placementBlock_) { currentInvIdx = i; break; }
        for (int i = 0; i < kInvCount; i++) {
            int col = i % kInvCols, row = i / kInvCols;
            float x0 = gx0 + col * (invSlot + invGap);
            float y0 = gy0 + row * (invSlot + invGap);
            bool sel = i == currentInvIdx;
            pushQuad(x0, y0, x0 + invSlot, y0 + invSlot, T_WHITE,
                     sel ? 0.50f : 0.22f, sel ? 0.50f : 0.22f, sel ? 0.50f : 0.22f, 0.92f);
            int tile = blockTile(kInvBlocks[i], F_PY);
            pushQuad(x0 + 4, y0 + 4, x0 + invSlot - 4, y0 + invSlot - 4, tile, 1, 1, 1, 1);
            if (sel) {
                pushQuad(x0, y0, x0 + invSlot, y0 + 2.0f, T_WHITE, 1, 1, 1, 1);
                pushQuad(x0, y0 + invSlot - 2.0f, x0 + invSlot, y0 + invSlot, T_WHITE, 1, 1, 1, 1);
                pushQuad(x0, y0, x0 + 2.0f, y0 + invSlot, T_WHITE, 1, 1, 1, 1);
                pushQuad(x0 + invSlot - 2.0f, y0, x0 + invSlot, y0 + invSlot, T_WHITE, 1, 1, 1, 1);
            }
        }
        // click to select
        if (in.mouse[0] && !prevMouse0_) {
            for (int i = 0; i < kInvCount; i++) {
                int col = i % kInvCols, row = i / kInvCols;
                float x0 = gx0 + col * (invSlot + invGap);
                float y0 = gy0 + row * (invSlot + invGap);
                if (cursorX_ >= x0 && cursorX_ <= x0 + invSlot && cursorY_ >= y0 && cursorY_ <= y0 + invSlot) {
                    placementBlock_ = kInvBlocks[i];
                    break;
                }
            }
        }
    }
    prevMouse0_ = in.mouse[0];

    memcpy(uiMap_, quads.data(), quads.size() * sizeof(UIVertex));
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe_);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLayout_, 0, 1, &uiSet_, 0, nullptr);
    VkBuffer ub = uiBuf_.b;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &ub, &off);
    vkCmdDraw(ctx.cmd, (uint32_t)quads.size(), 1, 0, 0);
}

void Renderer::shutdown(VkCtx& ctx) {
    if (shotBuf_) vkDestroyBuffer(ctx.device, shotBuf_, nullptr);
    if (shotMem_) vkFreeMemory(ctx.device, shotMem_, nullptr);
    uiBuf_.destroy(ctx.device);
    terrainUBO_.destroy(ctx.device);
    skyUBO_.destroy(ctx.device);
    if (pool_) vkDestroyDescriptorPool(ctx.device, pool_, nullptr);
    if (terrainDSL_) vkDestroyDescriptorSetLayout(ctx.device, terrainDSL_, nullptr);
    if (uiDSL_) vkDestroyDescriptorSetLayout(ctx.device, uiDSL_, nullptr);
    if (skyDSL_) vkDestroyDescriptorSetLayout(ctx.device, skyDSL_, nullptr);
    if (terrainPipe_) vkDestroyPipeline(ctx.device, terrainPipe_, nullptr);
    if (waterPipe_) vkDestroyPipeline(ctx.device, waterPipe_, nullptr);
    if (skyPipe_) vkDestroyPipeline(ctx.device, skyPipe_, nullptr);
    if (uiPipe_) vkDestroyPipeline(ctx.device, uiPipe_, nullptr);
    if (terrainLayout_) vkDestroyPipelineLayout(ctx.device, terrainLayout_, nullptr);
    if (skyLayout_) vkDestroyPipelineLayout(ctx.device, skyLayout_, nullptr);
    if (uiLayout_) vkDestroyPipelineLayout(ctx.device, uiLayout_, nullptr);
    if (atlasView_) vkDestroyImageView(ctx.device, atlasView_, nullptr);
    if (atlasSampler_) vkDestroySampler(ctx.device, atlasSampler_, nullptr);
    if (atlasImage_) vkDestroyImage(ctx.device, atlasImage_, nullptr);
    if (atlasMem_) vkFreeMemory(ctx.device, atlasMem_, nullptr);
}








