#include "interpreter_runtime_state.h"

#include <array>
#include <cstring>
#include <stack>
#include <string>

namespace {
std::weak_ptr<Fast::Interpreter> sInterpreterInstance;
} // namespace

// Preserve the historical linkage used by external Fast3D diagnostics while
// centralizing mutation in this execution-context owner.
std::stack<std::string> currentDir;

namespace Fast {

UcodeHandlers gUcodeHandlerIndex = ucode_f3dex2;

const static uint32_t f3dex2AttrHandler[] = {
    F3DEX2_G_MTX_PROJECTION, F3DEX2_G_MTX_LOAD,  F3DEX2_G_MTX_PUSH,  F3DEX_G_MTX_NOPUSH,
    F3DEX2_G_CULL_FRONT,     F3DEX2_G_CULL_BACK, F3DEX2_G_CULL_BOTH,
};

const static uint32_t f3dexAttrHandler[] = { F3DEX_G_MTX_PROJECTION, F3DEX_G_MTX_LOAD,   F3DEX_G_MTX_PUSH,
                                             F3DEX_G_MTX_NOPUSH,     F3DEX_G_CULL_FRONT, F3DEX_G_CULL_BACK,
                                             F3DEX_G_CULL_BOTH };

static constexpr std::array ucode_attr_handlers = {
    &f3dexAttrHandler,  // ucode_f3db
    &f3dexAttrHandler,  // ucode_f3d
    &f3dexAttrHandler,  // ucode_f3dex
    &f3dexAttrHandler,  // ucode_f3exb
    &f3dex2AttrHandler, // ucode_f3ex2
    &f3dex2AttrHandler, // ucode_s2dex
};

uint32_t GetUcodeAttribute(Attribute attr) {
    const auto ucode_map = ucode_attr_handlers[gUcodeHandlerIndex];
    // assert(ucode_map->contains(attr) && "Attribute not found in the current ucode handler");
    return (*ucode_map)[attr];
}

Interpreter* GetInterpreterInstance() {
    return sInterpreterInstance.lock().get();
}

void GfxSetInstance(std::shared_ptr<Interpreter> interpreter) {
    sInterpreterInstance = std::move(interpreter);
}

void PushCurrentDirectory(const char* path) {
    const size_t length = std::strlen(path);
    for (size_t i = length; i-- > 0;) {
        if (path[i] == '/' || path[i] == '\\') {
            currentDir.push(std::string(path).substr(0, i));
            return;
        }
    }
    currentDir.push(path);
}

void ResetCurrentDirectory() {
    currentDir = {};
}

void gfx_set_target_ucode(UcodeHandlers ucode) {
    gUcodeHandlerIndex = ucode;
}

} // namespace Fast
