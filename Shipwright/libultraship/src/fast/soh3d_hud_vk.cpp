// SoH3D PC HUD — native Vulkan immediate-mode 2D textured-quad renderer. See soh3d_hud_vk.h.
//
// Self-contained: owns a tiny straight-alpha textured-quad pipeline and records its draws into the
// Fast3D Vulkan backend's current command buffer + render pass (GfxRenderingAPIVulkan::BeginSoH3DPass),
// the same hook the SoH3D model pass and the RmlUi menu use. Modelled on RmlRenderInterfaceVk's
// texture-upload + pipeline machinery, but immediate-mode (no compiled geometry; quads are appended
// into a per-frame host-visible vertex buffer and drawn directly).
#include "fast/soh3d_hud_vk.h"

// SDL3 GPU HUD path (unified op model): when the SDL3 GPU backend is live, the SoH3D_Hud_* C-ABI
// delegates to the Fast::SgHud_* implementation (soh3d_hud_sdl3gpu.cpp) instead of the Vulkan one.
#ifdef ENABLE_SDL3GPU
#include "fast/soh3d_hud_sdl3gpu.h"
#include "fast/backends/gfx_sdl3gpu.h"
#define SOH3D_HUD_SG_ACTIVE (Fast::g_activeSdl3GpuApi != nullptr)
#else
#define SOH3D_HUD_SG_ACTIVE 0
#endif

#ifdef ENABLE_VULKAN

#include "fast/backends/gfx_vulkan.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

// ---- shaders --------------------------------------------------------------------------------
// Pixel-space (origin top-left, y-down) -> Vulkan clip. Vulkan NDC y is already down, so the map
// is direct. The framebuffer size arrives via a push constant.
const char* kVert = R"(#version 450
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;   // R8G8B8A8_UNORM -> normalized straight-alpha
layout(location=2) in vec2 aUv;
layout(location=0) out vec4 vCol;
layout(location=1) out vec2 vUv;
layout(push_constant) uniform PC { vec2 uViewport; } pc;
void main() {
    gl_Position = vec4(2.0 * aPos.x / pc.uViewport.x - 1.0, 2.0 * aPos.y / pc.uViewport.y - 1.0, 0.0, 1.0);
    vCol = aCol;
    vUv = aUv;
}
)";

const char* kFrag = R"(#version 450
layout(location=0) in vec4 vCol;
layout(location=1) in vec2 vUv;
layout(location=0) out vec4 frag;
layout(binding=0) uniform sampler2D uTex;
void main() {
    frag = texture(uTex, vUv) * vCol; // straight alpha
}
)";

struct HudVert {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
};

struct PushConst {
    float viewport[2];
};

constexpr uint32_t kMaxQuadsPerFrame = 1024;
constexpr uint32_t kVertsPerQuad = 6;

std::once_flag g_glslOnce;
bool CompileGlsl(EShLanguage stage, const char* src, std::vector<uint32_t>& spv) {
    std::call_once(g_glslOnce, []() { glslang::InitializeProcess(); });
    glslang::TShader shader(stage);
    shader.setStrings(&src, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, msg)) {
        SPDLOG_ERROR("[HudVk] shader parse failed: {}", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        SPDLOG_ERROR("[HudVk] shader link failed: {}", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

VkShaderModule MakeModule(VkDevice dev, const std::vector<uint32_t>& spv) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode = spv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
}

template <typename F> void OneShot(VkDevice device, VkCommandPool pool, VkQueue queue, F record) {
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkFence fence;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fi, nullptr, &fence);
    vkQueueSubmit(queue, 1, &si, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

struct Tex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct Ring {
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    uint32_t vertCount = 0; // verts written this frame
};

struct HudVk {
    // Captured each BeginFrame from the SoH3DVkContext.
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkViewport viewport{};
    VkRect2D fullScissor{};
    uint32_t frameIndex = 0;
    uint32_t framesInFlight = 0;

    // Pipeline + persistent resources (built once).
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    bool ready = false;

    std::vector<Ring> rings;

    // Texture cache, keyed by the source RGBA buffer pointer (stable for the HUD textures).
    std::unordered_map<const void*, int> keyToId;
    std::unordered_map<int, Tex> idToTex;
    int nextTexId = 1;
    Tex whiteTex; // 1x1 white for solid quads (tex==0)

    bool active = false;
    bool frameSetupDone = false;
};

HudVk g;

uint32_t FindMemType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

void MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf,
                VkDeviceMemory& mem, void** mappedOut) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g.device, &bi, nullptr, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g.device, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, props);
    vkAllocateMemory(g.device, &ai, nullptr, &mem);
    vkBindBufferMemory(g.device, buf, mem, 0);
    if (mappedOut)
        vkMapMemory(g.device, mem, 0, size, 0, mappedOut);
}

Tex UploadTex(const void* rgba, int w, int h) {
    Tex t;
    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(g.device, &ici, nullptr, &t.image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g.device, t.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g.device, &ai, nullptr, &t.mem);
    vkBindImageMemory(g.device, t.image, t.mem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(g.device, &vi, nullptr, &t.view);

    const VkDeviceSize size = (VkDeviceSize)w * h * 4;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    MakeBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    if (rgba)
        memcpy(mapped, rgba, size);
    else
        memset(mapped, 0xFF, size);
    vkUnmapMemory(g.device, stagingMem);

    OneShot(g.device, g.cmdPool, g.queue, [&](VkCommandBuffer cmd) {
        auto barrier = [&](VkImageLayout o, VkImageLayout n, VkAccessFlags sa, VkAccessFlags da,
                           VkPipelineStageFlags ss, VkPipelineStageFlags dsm) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = o;
            b.newLayout = n;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = t.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = sa;
            b.dstAccessMask = da;
            vkCmdPipelineBarrier(cmd, ss, dsm, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    vkDestroyBuffer(g.device, staging, nullptr);
    vkFreeMemory(g.device, stagingMem, nullptr);
    return t;
}

bool EnsureResources() {
    if (g.ready)
        return true;

    std::vector<uint32_t> vs, fs;
    if (!CompileGlsl(EShLangVertex, kVert, vs) || !CompileGlsl(EShLangFragment, kFrag, fs))
        return false;
    g.vs = MakeModule(g.device, vs);
    g.fs = MakeModule(g.device, fs);

    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 1;
    dli.pBindings = &bind;
    vkCreateDescriptorSetLayout(g.device, &dli, nullptr, &g.setLayout);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushConst);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g.setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(g.device, &pli, nullptr, &g.pipeLayout);

    g.rings.resize(g.framesInFlight ? g.framesInFlight : 1);
    for (Ring& r : g.rings) {
        MakeBuffer((VkDeviceSize)kMaxQuadsPerFrame * kVertsPerQuad * sizeof(HudVert),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, r.vbo, r.mem,
                   &r.mapped);
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = kMaxQuadsPerFrame;
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = kMaxQuadsPerFrame;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = &ps;
        vkCreateDescriptorPool(g.device, &dpi, nullptr, &r.pool);
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    vkCreateSampler(g.device, &si, nullptr, &g.sampler);

    // 1x1 white for solid (untextured) quads.
    const unsigned char white[4] = { 255, 255, 255, 255 };
    g.whiteTex = UploadTex(white, 1, 1);

    // Pipeline: straight-alpha blend, no depth, dynamic viewport/scissor.
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g.vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g.fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(HudVert);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(HudVert, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(HudVert, r) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(HudVert, u) };
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE; // HUD always on top
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // straight alpha
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynS{};
    dynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynS.dynamicStateCount = 2;
    dynS.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dynS;
    pci.layout = g.pipeLayout;
    pci.renderPass = g.renderPass;
    pci.subpass = 0;
    if (vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr, &g.pipeline) != VK_SUCCESS) {
        SPDLOG_ERROR("[HudVk] pipeline creation failed");
        return false;
    }

    g.ready = true;
    SPDLOG_INFO("[HudVk] resources ready");
    return true;
}

} // namespace

extern "C" {

int SoH3D_Hud_Available(void) {
    if (SOH3D_HUD_SG_ACTIVE)
        return Fast::SgHud_Available();
    return (Fast::g_activeVulkanApi != nullptr) ? 1 : 0;
}

int SoH3D_Hud_Begin(int* outW, int* outH) {
    if (SOH3D_HUD_SG_ACTIVE)
        return Fast::SgHud_Begin(outW, outH);
    g.active = false;
    if (!Fast::g_activeVulkanApi)
        return 0;
    Fast::SoH3DVkContext ctx{};
    if (!Fast::g_activeVulkanApi->BeginSoH3DPass(ctx))
        return 0;
    g.device = ctx.device;
    g.phys = ctx.physicalDevice;
    g.renderPass = ctx.renderPass;
    g.queue = ctx.graphicsQueue;
    g.cmdPool = ctx.commandPool;
    g.cmd = ctx.cmd;
    g.viewport = ctx.viewport;
    g.fullScissor = ctx.scissor;
    g.frameIndex = ctx.frameIndex;
    g.framesInFlight = ctx.framesInFlight;
    if (!EnsureResources())
        return 0;
    if (g.frameIndex >= g.rings.size())
        g.frameIndex = 0;
    Ring& r = g.rings[g.frameIndex];
    r.vertCount = 0;
    vkResetDescriptorPool(g.device, r.pool, 0);
    g.active = true;
    g.frameSetupDone = false;
    if (outW)
        *outW = (int)g.viewport.width;
    if (outH)
        *outH = (int)g.viewport.height;
    return 1;
}

int SoH3D_Hud_Tex(const void* key, const void* rgba, int w, int h) {
    if (SOH3D_HUD_SG_ACTIVE)
        return Fast::SgHud_Tex(key, rgba, w, h);
    if (!g.active || !key)
        return 0;
    auto it = g.keyToId.find(key);
    if (it != g.keyToId.end())
        return it->second;
    Tex t = UploadTex(rgba, w, h);
    int id = g.nextTexId++;
    g.idToTex[id] = t;
    g.keyToId[key] = id;
    return id;
}

void SoH3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    unsigned int tintRGBA) {
    if (SOH3D_HUD_SG_ACTIVE) {
        Fast::SgHud_Draw(tex, x, y, w, h, u0, v0, u1, v1, tintRGBA);
        return;
    }
    if (!g.active)
        return;
    Ring& r = g.rings[g.frameIndex];
    if (r.vertCount + kVertsPerQuad > kMaxQuadsPerFrame * kVertsPerQuad)
        return;

    VkImageView view = g.whiteTex.view;
    if (tex != 0) {
        auto t = g.idToTex.find(tex);
        if (t != g.idToTex.end())
            view = t->second.view;
    }

    const uint8_t cr = (uint8_t)((tintRGBA >> 24) & 0xFF);
    const uint8_t cg = (uint8_t)((tintRGBA >> 16) & 0xFF);
    const uint8_t cb = (uint8_t)((tintRGBA >> 8) & 0xFF);
    const uint8_t ca = (uint8_t)(tintRGBA & 0xFF);

    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    HudVert quad[6] = {
        { x0, y0, cr, cg, cb, ca, u0, v0 }, { x1, y0, cr, cg, cb, ca, u1, v0 },
        { x0, y1, cr, cg, cb, ca, u0, v1 }, { x1, y0, cr, cg, cb, ca, u1, v0 },
        { x1, y1, cr, cg, cb, ca, u1, v1 }, { x0, y1, cr, cg, cb, ca, u0, v1 },
    };
    const uint32_t firstVertex = r.vertCount;
    memcpy((uint8_t*)r.mapped + (size_t)firstVertex * sizeof(HudVert), quad, sizeof(quad));
    r.vertCount += kVertsPerQuad;

    // Per-draw descriptor set (texture); pool is reset each frame.
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = r.pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g.setLayout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(g.device, &dai, &set) != VK_SUCCESS)
        return;
    VkDescriptorImageInfo ii{};
    ii.sampler = g.sampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set;
    wr.dstBinding = 0;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.descriptorCount = 1;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(g.device, 1, &wr, 0, nullptr);

    VkCommandBuffer cmd = g.cmd;
    if (!g.frameSetupDone) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
        vkCmdSetViewport(cmd, 0, 1, &g.viewport);
        vkCmdSetScissor(cmd, 0, 1, &g.fullScissor);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &r.vbo, &zero);
        PushConst pc{};
        pc.viewport[0] = (float)g.viewport.width;
        pc.viewport[1] = (float)g.viewport.height;
        vkCmdPushConstants(cmd, g.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        g.frameSetupDone = true;
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeLayout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, kVertsPerQuad, 1, firstVertex, 0);
}

void SoH3D_Hud_End(void) {
    if (SOH3D_HUD_SG_ACTIVE) {
        Fast::SgHud_End();
        return;
    }
    g.active = false;
}

void SoH3D_Hud_Shutdown(void) {
    if (!g.device)
        return;
    vkDeviceWaitIdle(g.device);
    for (auto& [id, t] : g.idToTex) {
        if (t.view)
            vkDestroyImageView(g.device, t.view, nullptr);
        if (t.image)
            vkDestroyImage(g.device, t.image, nullptr);
        if (t.mem)
            vkFreeMemory(g.device, t.mem, nullptr);
    }
    g.idToTex.clear();
    g.keyToId.clear();
    if (g.whiteTex.view)
        vkDestroyImageView(g.device, g.whiteTex.view, nullptr);
    if (g.whiteTex.image)
        vkDestroyImage(g.device, g.whiteTex.image, nullptr);
    if (g.whiteTex.mem)
        vkFreeMemory(g.device, g.whiteTex.mem, nullptr);
    g.whiteTex = {};
    for (Ring& r : g.rings) {
        if (r.pool)
            vkDestroyDescriptorPool(g.device, r.pool, nullptr);
        if (r.vbo)
            vkDestroyBuffer(g.device, r.vbo, nullptr);
        if (r.mem)
            vkFreeMemory(g.device, r.mem, nullptr);
    }
    g.rings.clear();
    if (g.sampler)
        vkDestroySampler(g.device, g.sampler, nullptr);
    if (g.pipeline)
        vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.vs)
        vkDestroyShaderModule(g.device, g.vs, nullptr);
    if (g.fs)
        vkDestroyShaderModule(g.device, g.fs, nullptr);
    if (g.pipeLayout)
        vkDestroyPipelineLayout(g.device, g.pipeLayout, nullptr);
    if (g.setLayout)
        vkDestroyDescriptorSetLayout(g.device, g.setLayout, nullptr);
    g = HudVk{};
}

} // extern "C"

#else // !ENABLE_VULKAN — stubs so the soh-side HUD links on non-Vulkan builds.

extern "C" {
int SoH3D_Hud_Available(void) {
    return 0;
}
int SoH3D_Hud_Begin(int* outW, int* outH) {
    (void)outW;
    (void)outH;
    return 0;
}
int SoH3D_Hud_Tex(const void* key, const void* rgba, int w, int h) {
    (void)key;
    (void)rgba;
    (void)w;
    (void)h;
    return 0;
}
void SoH3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    unsigned int tintRGBA) {
    (void)tex;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)u0;
    (void)v0;
    (void)u1;
    (void)v1;
    (void)tintRGBA;
}
void SoH3D_Hud_End(void) {
}
void SoH3D_Hud_Shutdown(void) {
}
}

#endif // ENABLE_VULKAN
