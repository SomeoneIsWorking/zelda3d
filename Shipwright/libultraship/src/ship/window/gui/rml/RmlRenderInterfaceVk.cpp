#include "RmlRenderInterfaceVk.h"

#ifdef ENABLE_VULKAN

#include <RmlUi/Core/Vertex.h>

#include "fast/backends/gfx_vulkan.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <mutex>

namespace Ship {

namespace {

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
        SPDLOG_ERROR("[RmlVk] shader parse failed: {}", shader.getInfoLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(msg)) {
        SPDLOG_ERROR("[RmlVk] shader link failed: {}", prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spv, &opt);
    return !spv.empty();
}

// Pixel-space (origin top-left, y-down) -> Vulkan clip. Vulkan NDC y is already down, so the map is
// direct. Geometry positions are in pixels; the per-draw translate is added first.
const char* kVert = R"(#version 450
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;   // R8G8B8A8_UNORM -> normalized, premultiplied alpha
layout(location=2) in vec2 aUv;
layout(location=0) out vec4 vCol;
layout(location=1) out vec2 vUv;
layout(binding=0, std140) uniform UBO { vec2 uTranslate; vec2 uViewport; } ubo;
void main() {
    vec2 p = aPos + ubo.uTranslate;
    gl_Position = vec4(2.0 * p.x / ubo.uViewport.x - 1.0, 2.0 * p.y / ubo.uViewport.y - 1.0, 0.0, 1.0);
    vCol = aCol;
    vUv = aUv;
}
)";

const char* kFrag = R"(#version 450
layout(location=0) in vec4 vCol;
layout(location=1) in vec2 vUv;
layout(location=0) out vec4 frag;
layout(binding=1) uniform sampler2D uTex;
void main() {
    frag = texture(uTex, vUv) * vCol; // both premultiplied
}
)";

struct VkUbo {
    float uTranslate[2];
    float uViewport[2];
};

constexpr uint32_t kMaxDrawsPerFrame = 8192;

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

} // namespace

RmlRenderInterfaceVk::RmlRenderInterfaceVk() = default;
RmlRenderInterfaceVk::~RmlRenderInterfaceVk() {
    Shutdown();
}

uint32_t RmlRenderInterfaceVk::FindMemType(uint32_t bits, VkMemoryPropertyFlags want) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(mPhys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

void RmlRenderInterfaceVk::MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                                      VkBuffer& buf, VkDeviceMemory& mem, void** mappedOut) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(mDevice, &bi, nullptr, &buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(mDevice, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, props);
    vkAllocateMemory(mDevice, &ai, nullptr, &mem);
    vkBindBufferMemory(mDevice, buf, mem, 0);
    if (mappedOut)
        vkMapMemory(mDevice, mem, 0, size, 0, mappedOut);
}

bool RmlRenderInterfaceVk::EnsureResources() {
    if (mReady)
        return true;
    // mDevice/mPhys/mRenderPass/mFramesInFlight were captured in BeginFrame before this runs.

    std::vector<uint32_t> vs, fs;
    if (!CompileGlsl(EShLangVertex, kVert, vs) || !CompileGlsl(EShLangFragment, kFrag, fs))
        return false;
    mVs = MakeModule(mDevice, vs);
    mFs = MakeModule(mDevice, fs);

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 2;
    dli.pBindings = binds;
    vkCreateDescriptorSetLayout(mDevice, &dli, nullptr, &mSetLayout);

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &mSetLayout;
    vkCreatePipelineLayout(mDevice, &pli, nullptr, &mPipeLayout);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(mPhys, &props);
    VkDeviceSize align = props.limits.minUniformBufferOffsetAlignment;
    if (align == 0)
        align = 1;
    mUboStride = ((sizeof(VkUbo) + align - 1) / align) * align;

    mRings.resize(mFramesInFlight);
    for (uint32_t i = 0; i < mFramesInFlight; i++) {
        Ring& r = mRings[i];
        r.capacity = mUboStride * kMaxDrawsPerFrame;
        MakeBuffer(r.capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, r.ubo, r.mem,
                   &r.mapped);
        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps[0].descriptorCount = kMaxDrawsPerFrame;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[1].descriptorCount = kMaxDrawsPerFrame;
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = kMaxDrawsPerFrame;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = ps;
        vkCreateDescriptorPool(mDevice, &dpi, nullptr, &r.pool);
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    vkCreateSampler(mDevice, &si, nullptr, &mSampler);

    // 1x1 white for solid (untextured) geometry: white * premultiplied vertex colour = the colour.
    const unsigned char white[4] = { 255, 255, 255, 255 };
    Rml::TextureHandle wh = GenerateTexture(Rml::Span<const Rml::byte>(white, 4), Rml::Vector2i(1, 1));
    if (wh) {
        mWhiteTex = mTextures[wh];
        mTextures.erase(wh); // keep it out of the public handle table; we own it directly
    }

    // Two pipeline variants sharing all fixed state except depth-stencil + color-write mask.
    // Built here via a lambda to avoid repeating the large descriptor chain.
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = mVs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = mFs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Rml::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(Rml::Vertex, position) };
    attrs[1] = { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(Rml::Vertex, colour) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(Rml::Vertex, tex_coord) };
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

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                             VK_DYNAMIC_STATE_STENCIL_REFERENCE };
    VkPipelineDynamicStateCreateInfo dynS{};
    dynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynS.dynamicStateCount = 3;
    dynS.pDynamicStates = dyn;

    // Stencil op state shared by both pipelines (the reference is set dynamically).
    // Normal draw: stencil test = EQUAL (passes when ref==mask; ref driven by RenderToClipMask).
    //              Stencil write = KEEP (no modification).
    // Mask-write:  stencil test = ALWAYS (always passes; we are writing the mask, not testing it).
    //              Stencil write = REPLACE (write ref into stencil buffer).
    auto makeStencilOp = [](VkCompareOp cmp, VkStencilOp pass) {
        VkStencilOpState s{};
        s.failOp = VK_STENCIL_OP_KEEP;
        s.passOp = pass;
        s.depthFailOp = VK_STENCIL_OP_KEEP;
        s.compareOp = cmp;
        s.compareMask = 0xFF;
        s.writeMask = 0xFF;
        s.reference = 0; // set dynamically via vkCmdSetStencilReference
        return s;
    };

    // --- Pipeline 1: normal draw (stencil test, color writes on) ---
    VkPipelineDepthStencilStateCreateInfo dsNormal{};
    dsNormal.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsNormal.depthTestEnable = VK_FALSE;  // menu always on top
    dsNormal.depthWriteEnable = VK_FALSE;
    dsNormal.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    dsNormal.stencilTestEnable = VK_TRUE; // test against mask (ref set dynamically; passes when disabled = ref 0, mask=0→always)
    dsNormal.front = makeStencilOp(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP);
    dsNormal.back = dsNormal.front;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // premultiplied alpha
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cbNormal{};
    cbNormal.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbNormal.attachmentCount = 1;
    cbNormal.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &dsNormal;
    pci.pColorBlendState = &cbNormal;
    pci.pDynamicState = &dynS;
    pci.layout = mPipeLayout;
    pci.renderPass = mRenderPass;
    pci.subpass = 0;
    if (vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mPipeline) != VK_SUCCESS) {
        SPDLOG_ERROR("[RmlVk] normal pipeline creation failed");
        return false;
    }

    // --- Pipeline 2: mask-write (color writes off, stencil write via REPLACE) ---
    VkPipelineDepthStencilStateCreateInfo dsMask{};
    dsMask.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsMask.depthTestEnable = VK_FALSE;
    dsMask.depthWriteEnable = VK_FALSE;
    dsMask.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    dsMask.stencilTestEnable = VK_TRUE;
    dsMask.front = makeStencilOp(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE);
    dsMask.back = dsMask.front;

    VkPipelineColorBlendAttachmentState cbaMask{};
    cbaMask.colorWriteMask = 0; // no color writes during mask write
    cbaMask.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cbMask{};
    cbMask.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbMask.attachmentCount = 1;
    cbMask.pAttachments = &cbaMask;

    pci.pDepthStencilState = &dsMask;
    pci.pColorBlendState = &cbMask;
    if (vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mPipelineMaskWrite) != VK_SUCCESS) {
        SPDLOG_ERROR("[RmlVk] mask-write pipeline creation failed");
        return false;
    }

    mReady = true;
    SPDLOG_INFO("[RmlVk] resources ready (ubo stride {})", (unsigned long long)mUboStride);
    return true;
}

bool RmlRenderInterfaceVk::BeginFrame() {
    mActive = false;
    if (!Fast::g_activeVulkanApi)
        return false;
    Fast::SoH3DVkContext ctx{};
    if (!Fast::g_activeVulkanApi->BeginSoH3DPass(ctx))
        return false;
    // Capture this frame's handles (the .cpp alone may touch SoH3DVkContext; see the header note).
    mDevice = ctx.device;
    mPhys = ctx.physicalDevice;
    mRenderPass = ctx.renderPass;
    mQueue = ctx.graphicsQueue;
    mCmdPool = ctx.commandPool;
    mCmd = ctx.cmd;
    mViewport = ctx.viewport;
    mFullScissor = ctx.scissor;
    mFrameIndex = ctx.frameIndex;
    mFramesInFlight = ctx.framesInFlight;
    if (!EnsureResources())
        return false;
    mFrameCounter++;
    ProcessPendingFrees();
    Ring& r = mRings[mFrameIndex];
    r.offset = 0;
    vkResetDescriptorPool(mDevice, r.pool, 0);
    // Default scissor = full viewport; stencil ref 0 = all pixels pass EQUAL test (mask disabled).
    mScissorEnabled = false;
    mScissor = mFullScissor;
    mClipMaskEnabled = false;
    mStencilRef = 0;
    mActive = true;

    // Clear the stencil buffer to 0 at the start of each menu frame so the EQUAL test with
    // ref=0 passes everywhere when no clip mask has been written. ClearFramebuffer only clears
    // depth (not stencil) and mFbRenderPass uses DONT_CARE for stencil load, so we must do this
    // explicitly.
    VkClearAttachment stencilClear{};
    stencilClear.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    stencilClear.clearValue.depthStencil = { 0.0f, 0 };
    VkClearRect clearRect{};
    clearRect.rect = mFullScissor;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;
    vkCmdClearAttachments(mCmd, 1, &stencilClear, 1, &clearRect);

    return true;
}

void RmlRenderInterfaceVk::EndFrame() {
    mActive = false;
}

Rml::CompiledGeometryHandle RmlRenderInterfaceVk::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                 Rml::Span<const int> indices) {
    if (!mReady)
        return 0; // resources come up on the first BeginFrame; geometry is compiled afterwards
    Geometry g;
    g.indexCount = (uint32_t)indices.size();

    const VkDeviceSize vbBytes = vertices.size() * sizeof(Rml::Vertex);
    const VkDeviceSize ibBytes = indices.size() * sizeof(int);

    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    // Vertex buffer.
    MakeBuffer(vbBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    memcpy(mapped, vertices.data(), vbBytes);
    vkUnmapMemory(mDevice, stagingMem);
    MakeBuffer(vbBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, g.vbo, g.vboMem, nullptr);
    OneShot(mDevice, mCmdPool, mQueue, [&](VkCommandBuffer cmd) {
        VkBufferCopy c{};
        c.size = vbBytes;
        vkCmdCopyBuffer(cmd, staging, g.vbo, 1, &c);
    });
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMem, nullptr);

    // Index buffer.
    MakeBuffer(ibBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    memcpy(mapped, indices.data(), ibBytes);
    vkUnmapMemory(mDevice, stagingMem);
    MakeBuffer(ibBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, g.ibo, g.iboMem, nullptr);
    OneShot(mDevice, mCmdPool, mQueue, [&](VkCommandBuffer cmd) {
        VkBufferCopy c{};
        c.size = ibBytes;
        vkCmdCopyBuffer(cmd, staging, g.ibo, 1, &c);
    });
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMem, nullptr);

    Rml::CompiledGeometryHandle handle = mNextGeometry++;
    mGeometries[handle] = g;
    return handle;
}

void RmlRenderInterfaceVk::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                          Rml::TextureHandle texture) {
    if (!mActive)
        return;
    auto it = mGeometries.find(geometry);
    if (it == mGeometries.end())
        return;
    const Geometry& g = it->second;
    if (g.indexCount == 0)
        return;

    Ring& ring = mRings[mFrameIndex];
    if (ring.offset + mUboStride > ring.capacity)
        return;
    VkCommandBuffer cmd = mCmd;

    VkUbo ubo{};
    ubo.uTranslate[0] = translation.x;
    ubo.uTranslate[1] = translation.y;
    ubo.uViewport[0] = (float)mViewport.width;
    ubo.uViewport[1] = (float)mViewport.height;
    const VkDeviceSize uboOff = ring.offset;
    memcpy((uint8_t*)ring.mapped + uboOff, &ubo, sizeof(ubo));
    ring.offset += mUboStride;

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = ring.pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &mSetLayout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(mDevice, &dai, &set) != VK_SUCCESS)
        return;

    VkDescriptorBufferInfo bi{};
    bi.buffer = ring.ubo;
    bi.offset = uboOff;
    bi.range = sizeof(VkUbo);

    VkImageView view = mWhiteTex.view;
    if (texture != 0) {
        auto t = mTextures.find(texture);
        if (t != mTextures.end())
            view = t->second.view;
    }
    VkDescriptorImageInfo ii{};
    ii.sampler = mSampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &bi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = set;
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].descriptorCount = 1;
    w[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(mDevice, 2, w, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
    // Set stencil reference: when clip mask is active the ref is the expected mask value; when
    // disabled ref=0 and stencil buffer is all-zeros → EQUAL passes everywhere.
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, mStencilRef);
    vkCmdSetViewport(cmd, 0, 1, &mViewport);
    VkRect2D sc = mScissorEnabled ? mScissor : mFullScissor;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &g.vbo, &zero);
    vkCmdBindIndexBuffer(cmd, g.ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout, 0, 1, &set, 0, nullptr);
    vkCmdDrawIndexed(cmd, g.indexCount, 1, 0, 0, 0);
}

void RmlRenderInterfaceVk::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto it = mGeometries.find(geometry);
    if (it == mGeometries.end())
        return;
    PendingFree pf;
    pf.freeAtFrame = mFrameCounter + (mFramesInFlight ? mFramesInFlight : 3) + 1;
    pf.geo = it->second;
    mPendingFrees.push_back(pf);
    mGeometries.erase(it);
}

Rml::TextureHandle RmlRenderInterfaceVk::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    // The menu uses no <img>/file textures; only generated (font) textures. Report failure.
    (void)texture_dimensions;
    (void)source;
    return 0;
}

Rml::TextureHandle RmlRenderInterfaceVk::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                         Rml::Vector2i source_dimensions) {
    if (!mReady && !mDevice)
        return 0;
    int w = source_dimensions.x > 0 ? source_dimensions.x : 1;
    int h = source_dimensions.y > 0 ? source_dimensions.y : 1;

    Texture t;
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
    vkCreateImage(mDevice, &ici, nullptr, &t.image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(mDevice, t.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(mDevice, &ai, nullptr, &t.mem);
    vkBindImageMemory(mDevice, t.image, t.mem, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(mDevice, &vi, nullptr, &t.view);

    const VkDeviceSize size = (VkDeviceSize)w * h * 4;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    void* mapped = nullptr;
    MakeBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
               &mapped);
    if (source.data() && (VkDeviceSize)source.size() >= size)
        memcpy(mapped, source.data(), size);
    else
        memset(mapped, 0xFF, size);
    vkUnmapMemory(mDevice, stagingMem);

    OneShot(mDevice, mCmdPool, mQueue, [&](VkCommandBuffer cmd) {
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
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMem, nullptr);

    Rml::TextureHandle handle = mNextTexture++;
    mTextures[handle] = t;
    return handle;
}

void RmlRenderInterfaceVk::ReleaseTexture(Rml::TextureHandle texture) {
    auto it = mTextures.find(texture);
    if (it == mTextures.end())
        return;
    PendingFree pf;
    pf.freeAtFrame = mFrameCounter + (mFramesInFlight ? mFramesInFlight : 3) + 1;
    pf.tex = it->second;
    mPendingFrees.push_back(pf);
    mTextures.erase(it);
}

void RmlRenderInterfaceVk::EnableScissorRegion(bool enable) {
    mScissorEnabled = enable;
}

void RmlRenderInterfaceVk::SetScissorRegion(Rml::Rectanglei region) {
    mScissor.offset.x = region.Left();
    mScissor.offset.y = region.Top();
    mScissor.extent.width = (uint32_t)(region.Width() < 0 ? 0 : region.Width());
    mScissor.extent.height = (uint32_t)(region.Height() < 0 ? 0 : region.Height());
}

void RmlRenderInterfaceVk::EnableClipMask(bool enable) {
    // Stencil test is always active in mPipeline (EQUAL). When the mask is disabled,
    // mStencilRef=0 and the stencil buffer is 0 everywhere → test passes everywhere.
    // When enabled, mStencilRef was set by the last RenderToClipMask call.
    mClipMaskEnabled = enable;
    if (!enable)
        mStencilRef = 0;
}

void RmlRenderInterfaceVk::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                             Rml::Vector2f translation) {
    if (!mActive || !mPipelineMaskWrite)
        return;

    VkCommandBuffer cmd = mCmd;

    // Matching the GL3 reference implementation:
    //   Set:        clear stencil to 0, paint geometry pixels with 1. Normal draw ref=1 → passes inside.
    //   SetInverse: clear stencil to 1, paint geometry pixels with 0. Normal draw ref=1 → passes outside.
    //   Intersect:  paint geometry pixels with (mStencilRef+1) using ALWAYS. Normal draw ref bumped.
    uint8_t clearValue = 0;
    uint8_t writeRef = 1;
    bool doClear = false;

    switch (operation) {
    case Rml::ClipMaskOperation::Set:
        clearValue = 0;
        writeRef = 1;
        doClear = true;
        mStencilRef = 1;
        break;
    case Rml::ClipMaskOperation::SetInverse:
        clearValue = 1;
        writeRef = 0;
        doClear = true;
        mStencilRef = 1; // pixels outside the geometry have stencil=1 → ref=1 → EQUAL passes them
        break;
    case Rml::ClipMaskOperation::Intersect:
        doClear = false;
        writeRef = static_cast<uint8_t>(mStencilRef + 1);
        mStencilRef = writeRef;
        break;
    }

    if (doClear) {
        VkClearAttachment clr{};
        clr.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        clr.clearValue.depthStencil = { 0.0f, clearValue };
        VkClearRect cr{};
        cr.rect = mFullScissor;
        cr.baseArrayLayer = 0;
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clr, 1, &cr);
    }

    auto it = mGeometries.find(geometry);
    if (it == mGeometries.end())
        return;
    const Geometry& g = it->second;
    if (g.indexCount == 0)
        return;

    Ring& ring = mRings[mFrameIndex];
    if (ring.offset + mUboStride > ring.capacity)
        return;

    VkUbo ubo{};
    ubo.uTranslate[0] = translation.x;
    ubo.uTranslate[1] = translation.y;
    ubo.uViewport[0] = (float)mViewport.width;
    ubo.uViewport[1] = (float)mViewport.height;
    const VkDeviceSize uboOff = ring.offset;
    memcpy((uint8_t*)ring.mapped + uboOff, &ubo, sizeof(ubo));
    ring.offset += mUboStride;

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = ring.pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &mSetLayout;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(mDevice, &dai, &set) != VK_SUCCESS)
        return;

    VkDescriptorBufferInfo bi{};
    bi.buffer = ring.ubo;
    bi.offset = uboOff;
    bi.range = sizeof(VkUbo);
    VkDescriptorImageInfo ii{};
    ii.sampler = mSampler;
    ii.imageView = mWhiteTex.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w[2]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &bi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = set;
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].descriptorCount = 1;
    w[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(mDevice, 2, w, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineMaskWrite);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, writeRef);
    vkCmdSetViewport(cmd, 0, 1, &mViewport);
    VkRect2D sc = mScissorEnabled ? mScissor : mFullScissor;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &g.vbo, &zero);
    vkCmdBindIndexBuffer(cmd, g.ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout, 0, 1, &set, 0, nullptr);
    vkCmdDrawIndexed(cmd, g.indexCount, 1, 0, 0, 0);
}

void RmlRenderInterfaceVk::DestroyGeometry(Geometry& g) {
    if (g.vbo)
        vkDestroyBuffer(mDevice, g.vbo, nullptr);
    if (g.vboMem)
        vkFreeMemory(mDevice, g.vboMem, nullptr);
    if (g.ibo)
        vkDestroyBuffer(mDevice, g.ibo, nullptr);
    if (g.iboMem)
        vkFreeMemory(mDevice, g.iboMem, nullptr);
    g = {};
}

void RmlRenderInterfaceVk::DestroyTexture(Texture& t) {
    if (t.view)
        vkDestroyImageView(mDevice, t.view, nullptr);
    if (t.image)
        vkDestroyImage(mDevice, t.image, nullptr);
    if (t.mem)
        vkFreeMemory(mDevice, t.mem, nullptr);
    t = {};
}

void RmlRenderInterfaceVk::ProcessPendingFrees() {
    for (auto it = mPendingFrees.begin(); it != mPendingFrees.end();) {
        if (mFrameCounter >= it->freeAtFrame) {
            DestroyGeometry(it->geo);
            DestroyTexture(it->tex);
            it = mPendingFrees.erase(it);
        } else {
            ++it;
        }
    }
}

void RmlRenderInterfaceVk::Shutdown() {
    if (!mDevice)
        return;
    vkDeviceWaitIdle(mDevice);
    for (auto& [h, g] : mGeometries)
        DestroyGeometry(g);
    mGeometries.clear();
    for (auto& [h, t] : mTextures)
        DestroyTexture(t);
    mTextures.clear();
    for (auto& pf : mPendingFrees) {
        DestroyGeometry(pf.geo);
        DestroyTexture(pf.tex);
    }
    mPendingFrees.clear();
    DestroyTexture(mWhiteTex);
    for (Ring& r : mRings) {
        if (r.pool)
            vkDestroyDescriptorPool(mDevice, r.pool, nullptr);
        if (r.ubo)
            vkDestroyBuffer(mDevice, r.ubo, nullptr);
        if (r.mem)
            vkFreeMemory(mDevice, r.mem, nullptr);
    }
    mRings.clear();
    if (mSampler)
        vkDestroySampler(mDevice, mSampler, nullptr);
    if (mPipeline)
        vkDestroyPipeline(mDevice, mPipeline, nullptr);
    if (mPipelineMaskWrite)
        vkDestroyPipeline(mDevice, mPipelineMaskWrite, nullptr);
    if (mVs)
        vkDestroyShaderModule(mDevice, mVs, nullptr);
    if (mFs)
        vkDestroyShaderModule(mDevice, mFs, nullptr);
    if (mPipeLayout)
        vkDestroyPipelineLayout(mDevice, mPipeLayout, nullptr);
    if (mSetLayout)
        vkDestroyDescriptorSetLayout(mDevice, mSetLayout, nullptr);
    mSampler = VK_NULL_HANDLE;
    mPipeline = VK_NULL_HANDLE;
    mPipelineMaskWrite = VK_NULL_HANDLE;
    mVs = mFs = VK_NULL_HANDLE;
    mPipeLayout = VK_NULL_HANDLE;
    mSetLayout = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mReady = false;
}

} // namespace Ship

#endif // ENABLE_VULKAN
