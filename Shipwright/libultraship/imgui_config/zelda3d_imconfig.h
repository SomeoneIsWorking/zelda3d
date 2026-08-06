#pragma once

// Dear ImGui compile-time configuration for this port.
//
// Included via -DIMGUI_USER_CONFIG, which imgui.h pulls in *in addition to* upstream's own
// imconfig.h. So this file only has to carry what we change; everything else stays at upstream
// defaults and does not have to be re-synced when ImGui is updated. Keeping our settings here
// rather than editing a vendored imconfig.h is the whole point -- the previous arrangement forked
// upstream's file to flip two lines, which is a merge conflict every version bump.
//
// ImGui is the DEVELOPER-OVERLAY stack in this port. The shipped, game-facing UI is RmlUi. That
// split is deliberate and follows Dusklight (docs/dusklight-adoption.md): shipped UI and debug UI
// have different requirements and should not share a framework.

// Operator overloads for ImVec2/ImVec4 (+, -, *, ...). SoH's dev-tool code and the group-panel
// helpers in GuiWindow use these; upstream ships them commented out.
#define IMGUI_DEFINE_MATH_OPERATORS

// Texture handles are passed as raw pointers, not the ImU64 default.
//
// This is load-bearing and easy to get wrong: ~77 call sites in this tree hand ImGui a pointer
// (Fast3D's texture registry entries, the HUD glyph tiles, RmlUi-side textures). Building with the
// ImU64 default compiles most of them and then fails scattered across unrelated files, which reads
// like the call sites are wrong rather than the config. It is the config.
#define ImTextureID void*
