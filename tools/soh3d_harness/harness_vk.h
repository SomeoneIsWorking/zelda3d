// harness_vk.h — minimal libretro Vulkan HW-render FRONTEND for the SBS harness.
//
// Azahar's libretro core can render with Vulkan (renderer_vulkan) at an
// arbitrary internal resolution factor — but only if the FRONTEND (this
// harness) plays the libretro Vulkan HW-render role: create a VkInstance,
// negotiate a VkDevice via the core's create_device callback, expose a
// retro_hw_render_interface_vulkan, and read the core's rendered VkImage back
// to a CPU buffer for the software SBS compositor.
//
// The core hands us a single shared output image (R8G8B8A8_UNORM, TILING_
// OPTIMAL, usage incl. TRANSFER_SRC) via set_image, then calls
// retro_video_refresh(RETRO_HW_FRAME_BUFFER_VALID). We copy that image to a
// host-visible linear image and swizzle it into g_az_buf as XRGB8888 (the byte
// order every downstream harness consumer already expects), so nothing else in
// the compositor / PPM-dump path changes.
//
// See tools/soh3d_harness/main.cpp for how this is wired into the libretro
// lifecycle (SET_HW_RENDER / negotiation capture, post-load context_reset,
// VideoRefresh dispatch).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Fwd-declared libretro Vulkan types (defined in libretro_vulkan.h). main.cpp
// includes the full header; here we only need the negotiation-interface and
// hw-render-interface pointers.
struct retro_hw_render_context_negotiation_interface_vulkan;
struct retro_hw_render_interface_vulkan;

namespace HarnessVk {

// Create the frontend VkInstance + negotiate the device with the core. `nego`
// is the negotiation interface the core registered via
// SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE. Returns false (and logs) on any
// Vulkan failure — the caller should then abort the Vulkan bring-up.
bool Init(const retro_hw_render_context_negotiation_interface_vulkan* nego);

// The interface handed back to the core on GET_HW_RENDER_INTERFACE. Valid only
// after a successful Init(). Never null once Init() succeeded.
const retro_hw_render_interface_vulkan* Interface();

// True once the core has pushed at least one frame via set_image.
bool HaveImage();

// Copy the most-recent core-rendered image into `out` as XRGB8888
// (tightly packed, pitch = width*4). `w`/`h` are the frame dimensions from the
// video_refresh callback (the view create_info carries no extent). Sets pitch.
// Returns false if no image has been presented yet or on a Vulkan error.
// Called from VideoRefresh when data == RETRO_HW_FRAME_BUFFER_VALID.
bool Readback(std::vector<uint8_t>& out, uint32_t w, uint32_t h, size_t& pitch);

} // namespace HarnessVk
