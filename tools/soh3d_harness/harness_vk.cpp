// harness_vk.cpp — see harness_vk.h. Minimal, correctness-first libretro
// Vulkan HW-render frontend: one instance, one device (negotiated with the
// core), one shared readback path. No swapchain, no presentation, no async —
// the harness is a headless oracle, so we trade throughput for a dead-simple
// synchronous "copy the core's image to CPU" per frame.

#include "harness_vk.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vulkan/vulkan.h>

#include "libretro_vulkan.h"

namespace HarnessVk {
namespace {

#define VKLOG(...) std::fprintf(stderr, "[harness-vk] " __VA_ARGS__)

struct State {
    bool ok = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    // Host-visible linear staging image the core's optimal image is copied into
    // (same format, R8G8B8A8_UNORM), (re)created on first use / size change.
    VkImage linImg = VK_NULL_HANDLE;
    VkDeviceMemory linMem = VK_NULL_HANDLE;
    uint32_t linW = 0, linH = 0;

    // Latest image handed to us by the core via set_image. Guarded by imgMtx.
    std::mutex imgMtx;
    bool haveImage = false;
    VkImage srcImage = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Serializes queue use between the core (via lock_queue/unlock_queue) and
    // our own readback submits — Azahar's Vulkan scheduler submits async.
    std::mutex queueMtx;

    retro_hw_render_interface_vulkan iface{};
};

State g;

uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(g.gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

// --- retro_hw_render_interface_vulkan callbacks -----------------------------

void cb_set_image(void* /*handle*/, const struct retro_vulkan_image* image, uint32_t /*num_semaphores*/,
                  const VkSemaphore* /*semaphores*/, uint32_t /*src_queue_family*/) {
    std::lock_guard<std::mutex> lk(g.imgMtx);
    if (!image) {
        g.haveImage = false;
        return;
    }
    // create_info.image is the underlying VkImage of the view; that is the
    // single shared output texture the core rendered this frame. Its
    // dimensions are NOT in the view create_info (VkImageViewCreateInfo has no
    // extent) — they arrive as the video_refresh width/height params and are
    // passed to Readback() by the caller.
    g.srcImage = image->create_info.image;
    g.srcLayout = image->image_layout;
    g.haveImage = (g.srcImage != VK_NULL_HANDLE);
}

// Single-image, synchronous model: we always hand the core frame slot 0 and
// finish our readback before returning from video_refresh, so there is never
// more than one live frame to track.
uint32_t cb_get_sync_index(void* /*handle*/) {
    return 0;
}
uint32_t cb_get_sync_index_mask(void* /*handle*/) {
    return 0x1;
}
void cb_wait_sync_index(void* /*handle*/) {
    // Our readback already fence-waited inside Readback(), so by the time the
    // core asks to reuse slot 0 the previous read is complete. Nothing to do.
}
void cb_set_command_buffers(void* /*handle*/, uint32_t /*n*/, const VkCommandBuffer* /*cmd*/) {
    // The core uses the set_image path, not set_command_buffers. No-op.
}
void cb_lock_queue(void* /*handle*/) {
    g.queueMtx.lock();
}
void cb_unlock_queue(void* /*handle*/) {
    g.queueMtx.unlock();
}
void cb_set_signal_semaphore(void* /*handle*/, VkSemaphore /*sem*/) {
    // We don't ping-pong images; nothing to signal.
}

// --- setup ------------------------------------------------------------------

bool CreateInstance(const retro_hw_render_context_negotiation_interface_vulkan* nego) {
    const VkApplicationInfo* app = nullptr;
    if (nego && nego->get_application_info) {
        app = nego->get_application_info();
    }
    VkApplicationInfo fallback{};
    fallback.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    fallback.pApplicationName = "soh3d_harness";
    fallback.pEngineName = "soh3d_harness";
    fallback.apiVersion = VK_API_VERSION_1_1;
    if (!app)
        app = &fallback;

    // VkPhysicalDeviceFeatures2 (used by the core's create_device) needs a 1.1
    // instance; the app info requests 1.1 already, but be defensive.
    VkApplicationInfo bumped = *app;
    if (bumped.apiVersion < VK_API_VERSION_1_1)
        bumped.apiVersion = VK_API_VERSION_1_1;

    // Enable VK_KHR_surface if present so the swapchain DEVICE extension the
    // core adds has its instance-level dependency satisfied (we never make a
    // surface — headless).
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    if (extCount)
        vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data());
    std::vector<const char*> enabled;
    for (const auto& e : exts) {
        if (!std::strcmp(e.extensionName, VK_KHR_SURFACE_EXTENSION_NAME)) {
            enabled.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            break;
        }
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &bumped;
    ci.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    ci.ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data();

    VkResult r = vkCreateInstance(&ci, nullptr, &g.instance);
    if (r != VK_SUCCESS) {
        VKLOG("vkCreateInstance failed: %d\n", (int)r);
        return false;
    }
    return true;
}

bool PickPhysicalDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g.instance, &n, nullptr);
    if (!n) {
        VKLOG("no Vulkan physical devices\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(g.instance, &n, devs.data());
    VkPhysicalDevice fallback = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            g.gpu = d;
            VKLOG("using discrete GPU: %s\n", p.deviceName);
            return true;
        }
    }
    g.gpu = fallback;
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(g.gpu, &p);
    VKLOG("using GPU: %s\n", p.deviceName);
    return true;
}

bool CreatePerFrameObjects() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g.queueFamily;
    if (vkCreateCommandPool(g.device, &pci, nullptr, &g.cmdPool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(g.device, &ai, &g.cmd) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(g.device, &fi, nullptr, &g.fence) != VK_SUCCESS)
        return false;
    return true;
}

void DestroyLinearImage() {
    if (g.linImg) {
        vkDestroyImage(g.device, g.linImg, nullptr);
        g.linImg = VK_NULL_HANDLE;
    }
    if (g.linMem) {
        vkFreeMemory(g.device, g.linMem, nullptr);
        g.linMem = VK_NULL_HANDLE;
    }
    g.linW = g.linH = 0;
}

bool EnsureLinearImage(uint32_t w, uint32_t h) {
    if (g.linImg && g.linW == w && g.linH == h)
        return true;
    DestroyLinearImage();

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM; // same as core output → plain copy
    ici.extent = { w, h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g.device, &ici, nullptr, &g.linImg) != VK_SUCCESS) {
        VKLOG("linear image create failed\n");
        return false;
    }

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(g.device, g.linImg, &mr);
    uint32_t mt =
        FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) {
        VKLOG("no host-visible memory type\n");
        DestroyLinearImage();
        return false;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(g.device, &mai, nullptr, &g.linMem) != VK_SUCCESS) {
        VKLOG("linear image memory alloc failed\n");
        DestroyLinearImage();
        return false;
    }
    vkBindImageMemory(g.device, g.linImg, g.linMem, 0);
    g.linW = w;
    g.linH = h;
    return true;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAcc,
                  VkAccessFlags dstAcc, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = srcAcc;
    b.dstAccessMask = dstAcc;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

bool Init(const retro_hw_render_context_negotiation_interface_vulkan* nego) {
    if (!nego || !nego->create_device) {
        VKLOG("negotiation interface missing create_device\n");
        return false;
    }
    if (!CreateInstance(nego))
        return false;
    if (!PickPhysicalDevice())
        return false;

    retro_vulkan_context ctx{};
    // Headless: no surface. Ask the core for exactly the GPU we picked.
    bool created = nego->create_device(&ctx, g.instance, g.gpu, VK_NULL_HANDLE, vkGetInstanceProcAddr,
                                       /*required_device_extensions*/ nullptr, 0,
                                       /*required_device_layers*/ nullptr, 0,
                                       /*required_features*/ nullptr);
    if (!created) {
        VKLOG("core create_device failed\n");
        return false;
    }
    g.gpu = ctx.gpu;
    g.device = ctx.device;
    g.queue = ctx.queue;
    g.queueFamily = ctx.queue_family_index;

    if (!CreatePerFrameObjects()) {
        VKLOG("per-frame Vulkan object creation failed\n");
        return false;
    }

    // Publish the HW-render interface the core will fetch on
    // GET_HW_RENDER_INTERFACE.
    g.iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    g.iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    g.iface.handle = &g;
    g.iface.instance = g.instance;
    g.iface.gpu = g.gpu;
    g.iface.device = g.device;
    g.iface.get_device_proc_addr = vkGetDeviceProcAddr;
    g.iface.get_instance_proc_addr = vkGetInstanceProcAddr;
    g.iface.queue = g.queue;
    g.iface.queue_index = g.queueFamily;
    g.iface.set_image = cb_set_image;
    g.iface.get_sync_index = cb_get_sync_index;
    g.iface.get_sync_index_mask = cb_get_sync_index_mask;
    g.iface.set_command_buffers = cb_set_command_buffers;
    g.iface.wait_sync_index = cb_wait_sync_index;
    g.iface.lock_queue = cb_lock_queue;
    g.iface.unlock_queue = cb_unlock_queue;
    g.iface.set_signal_semaphore = cb_set_signal_semaphore;

    g.ok = true;
    VKLOG("Vulkan frontend ready (queue family %u)\n", g.queueFamily);
    return true;
}

const retro_hw_render_interface_vulkan* Interface() {
    return g.ok ? &g.iface : nullptr;
}

bool HaveImage() {
    std::lock_guard<std::mutex> lk(g.imgMtx);
    return g.haveImage;
}

bool Readback(std::vector<uint8_t>& out, uint32_t w, uint32_t h, size_t& pitch) {
    VkImage src;
    VkImageLayout srcLayout;
    {
        std::lock_guard<std::mutex> lk(g.imgMtx);
        if (!g.ok || !g.haveImage || g.srcImage == VK_NULL_HANDLE)
            return false;
        src = g.srcImage;
        srcLayout = g.srcLayout;
    }
    const uint32_t sw = w, sh = h;
    if (!sw || !sh)
        return false;

    if (!EnsureLinearImage(sw, sh))
        return false;

    vkResetFences(g.device, 1, &g.fence);
    vkResetCommandBuffer(g.cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g.cmd, &bi);

    // Core's image: declared layout → TRANSFER_SRC.
    ImageBarrier(g.cmd, src, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    // Linear staging: UNDEFINED → TRANSFER_DST.
    ImageBarrier(g.cmd, g.linImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { sw, sh, 1 };
    vkCmdCopyImage(g.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.linImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region);

    // Linear staging: make writes visible to the host.
    ImageBarrier(g.cmd, g.linImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_HOST_BIT);
    // Note: we deliberately leave the core's image in TRANSFER_SRC. Its render
    // pass uses initialLayout=UNDEFINED, so the next frame re-transitions it
    // regardless — no restore needed.

    vkEndCommandBuffer(g.cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    {
        std::lock_guard<std::mutex> lk(g.queueMtx);
        if (vkQueueSubmit(g.queue, 1, &si, g.fence) != VK_SUCCESS) {
            VKLOG("readback submit failed\n");
            return false;
        }
    }
    vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);

    // Read the linear image row by row, swizzling R8G8B8A8 → XRGB8888
    // (LE bytes B,G,R,X) so downstream consumers (compositor, PPM dumps) that
    // treat g_az_buf as XRGB8888 stay correct.
    VkImageSubresource sub{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout lay{};
    vkGetImageSubresourceLayout(g.device, g.linImg, &sub, &lay);

    void* mapped = nullptr;
    if (vkMapMemory(g.device, g.linMem, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        VKLOG("map failed\n");
        return false;
    }
    const size_t dstPitch = static_cast<size_t>(sw) * 4;
    if (out.size() < dstPitch * sh)
        out.resize(dstPitch * sh);
    const uint8_t* base = static_cast<const uint8_t*>(mapped) + lay.offset;
    for (uint32_t y = 0; y < sh; y++) {
        const uint8_t* srcRow = base + static_cast<size_t>(y) * lay.rowPitch;
        uint8_t* dstRow = out.data() + static_cast<size_t>(y) * dstPitch;
        for (uint32_t x = 0; x < sw; x++) {
            const uint8_t* p = srcRow + static_cast<size_t>(x) * 4; // R,G,B,A
            dstRow[x * 4 + 0] = p[2];                               // B
            dstRow[x * 4 + 1] = p[1];                               // G
            dstRow[x * 4 + 2] = p[0];                               // R
            dstRow[x * 4 + 3] = 0xFF;                               // X
        }
    }
    vkUnmapMemory(g.device, g.linMem);

    pitch = dstPitch;
    return true;
}

} // namespace HarnessVk
