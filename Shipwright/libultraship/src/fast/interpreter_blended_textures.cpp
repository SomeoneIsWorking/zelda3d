#define NOMINMAX

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#include "fast/interpreter.h"
#include "fast/lus_gbi.h"
#include "fast/resource/type/Light.h"
#include "fast/zelda3d_submission.h"
#include "fast/backends/gfx_window_manager_api.h"
#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "ship/window/gui/Gui.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/Utils.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/zelda3d_hostiface.h"
#include "libultraship/libultra/os.h"
#include <spdlog/fmt/fmt.h>

#include "interpreter_geometry_observation.h"
#include "interpreter_runtime_state.h"
#include "interpreter_texture_decode.h"
#include "interpreter_viewport_math.h"

#ifdef _WIN32
#include <windows.h>
#endif

constexpr size_t MAX_TRI_BUFFER = 256;
constexpr size_t TEXTURE_CACHE_MAX_SIZE = 1024;

namespace Fast {

int32_t gfx_check_image_signature(const char* imgData) {
    uintptr_t i = (uintptr_t)(imgData);

    if ((i & 1) == 1) {
        return 0;
    }

    // Filter addresses that are obviously not valid string pointers before
    // attempting to dereference for the "__OTR__" check.
    if (i == 0 || i < 0x10000) {
        return 0;
    }
#if UINTPTR_MAX > 0xFFFFFFFFu
    // On 64-bit: filter kernel/sentinel addresses. Upper bound covers all
    // user-space layouts (x86_64 47-bit canonical, ARM64 48-bit VA, etc.).
    if (i > 0x0000FFFFFFFFFFFFull) {
        return 0;
    }
#endif

    return Ship::Context::GetRawInstance()->GetResourceManager()->OtrSignatureCheck(imgData);
}

void Interpreter::RegisterBlendedTexture(const char* name, uint8_t* mask, uint8_t* replacement) {
    if (gfx_check_image_signature(name)) {
        name += 7;
    }

    if (gfx_check_image_signature(reinterpret_cast<char*>(replacement))) {
        Fast::Texture* tex = std::static_pointer_cast<Fast::Texture>(
                                 Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(
                                     reinterpret_cast<char*>(replacement)))
                                 .get();

        replacement = tex->ImageData;
    }

    mMaskedTextures[name] = MaskedTextureEntry{ mask, replacement };
}

void Interpreter::UnregisterBlendedTexture(const char* name) {
    if (gfx_check_image_signature(name)) {
        name += 7;
    }

    mMaskedTextures.erase(name);
}

// The registry holds raw pointers into game-core and per-game resource lifetimes,
// so a run boundary must clear it before either owner is unloaded.
size_t Interpreter::ClearBlendedTextures() {
    const size_t inherited = mMaskedTextures.size();
    mMaskedTextures.clear();
    return inherited;
}

} // namespace Fast
