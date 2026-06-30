// imgui_stub.cpp — no-op implementation of the Dear ImGui API.
//
// The real Dear ImGui library was removed from this build (see cmake/dependencies/common.cmake).
// The SoH ImGui dev-tool / menu code is kept in-tree as inert scaffolding (to be migrated to RmlUi
// incrementally); it still needs to COMPILE (against the vendored ImGui headers in imgui_shim/) and
// LINK. This file provides every ImGui symbol that code references as a no-op so the link succeeds.
//
// IMPORTANT: the libultraship framework's ImGui *driving* (context create / NewFrame / Render / the
// per-frame window-draw loop) is neutered (Gui.cpp / Fast3dGui.cpp), so none of these stubs execute
// at runtime — the live UI is RmlUi. Because nothing here runs, functions that must return a
// reference to an ImGui struct return zero-initialized static storage (sidestepping ImGui's
// uncompiled constructors); pointer-returning functions return a pointer to zeroed static storage
// (never null) so any stray dereference reads zero instead of faulting.

#include <imgui.h>
#include <imgui_internal.h>
#include <string>

// ---- helpers: zero-initialized static storage of type T (never executed; see header note).
namespace {
template <typename T> T& ZeroRef() {
    static T* storage = reinterpret_cast<T*>(new char[sizeof(T)]());
    return *storage;
}
template <typename T> T* ZeroPtr() {
    static T* storage = reinterpret_cast<T*>(new char[sizeof(T)]());
    return storage;
}
template <> void* ZeroPtr<void>() {
    static char storage[1] = {};
    return storage;
}
} // namespace

// ============================================================================================
// imgui_stdlib (std::string overloads)
// ============================================================================================
namespace ImGui {
// C-buffer overloads (declared in imgui.h)
bool InputText(const char*, char*, size_t, ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
    return false;
}
bool InputTextMultiline(const char*, char*, size_t, const ImVec2&, ImGuiInputTextFlags, ImGuiInputTextCallback,
                        void*) {
    return false;
}
bool InputTextWithHint(const char*, const char*, char*, size_t, ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
    return false;
}
// std::string overloads (declared in misc/cpp/imgui_stdlib.h)
bool InputText(const char*, std::string*, ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
    return false;
}
bool InputTextMultiline(const char*, std::string*, const ImVec2&, ImGuiInputTextFlags, ImGuiInputTextCallback,
                        void*) {
    return false;
}
bool InputTextWithHint(const char*, const char*, std::string*, ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
    return false;
}
} // namespace ImGui

// ============================================================================================
// SDL3 platform backend (imgui_impl_sdl3) — only the few entry points libultraship calls.
// ============================================================================================
struct SDL_Window;
union SDL_Event;
bool ImGui_ImplSDL3_InitForSDLGPU(SDL_Window*) {
    return true;
}
void ImGui_ImplSDL3_Shutdown() {
}
void ImGui_ImplSDL3_NewFrame() {
}
bool ImGui_ImplSDL3_ProcessEvent(const SDL_Event*) {
    return false;
}

// ============================================================================================
// ImGui:: core API — no-op definitions auto-generated from the linker's undefined-symbol list.
// See scratch/gen_imgui_stub.py (return types looked up from the vendored imgui headers).
// ============================================================================================
namespace ImGui {
#include "imgui_stub_generated.inc"
} // namespace ImGui

// ============================================================================================
// Class methods / globals referenced by the kept UI code.
// ============================================================================================

// The global ImGui context pointer (declared extern in imgui_internal.h). Point it at zeroed
// storage rather than null so a stray GImGui-> read reads zero instead of faulting (never executed).
ImGuiContext* GImGui = ZeroPtr<ImGuiContext>();

void ImDrawList::AddCircleFilled(const ImVec2&, float, ImU32, int) {
}
void ImDrawList::AddCircle(const ImVec2&, float, ImU32, int, float) {
}
void ImDrawList::AddQuadFilled(const ImVec2&, const ImVec2&, const ImVec2&, const ImVec2&, ImU32) {
}
void ImDrawList::AddRectFilled(const ImVec2&, const ImVec2&, ImU32, float, int) {
}
void ImDrawList::AddRect(const ImVec2&, const ImVec2&, ImU32, float, int, float) {
}
void ImDrawList::AddText(ImFont*, float, const ImVec2&, ImU32, const char*, const char*, float, const ImVec4*) {
}
int ImDrawList::_CalcCircleAutoSegmentCount(float) const {
    return 0;
}

ImFont* ImFontAtlas::AddFontDefault(const ImFontConfig*) {
    return ZeroPtr<ImFont>();
}
ImFont* ImFontAtlas::AddFontFromMemoryCompressedBase85TTF(const char*, float, const ImFontConfig*, const ImWchar*) {
    return ZeroPtr<ImFont>();
}
ImFont* ImFontAtlas::AddFontFromMemoryTTF(void*, int, float, const ImFontConfig*, const ImWchar*) {
    return ZeroPtr<ImFont>();
}
bool ImFontAtlas::Build() {
    return true;
}
const ImWchar* ImFontAtlas::GetGlyphRangesJapanese() {
    return ZeroPtr<ImWchar>();
}

ImVec2 ImFont::CalcTextSizeA(float, float, float, const char*, const char*, const char**) {
    return {};
}

ImFontConfig::ImFontConfig() {
}

void ImGuiInputTextCallbackData::DeleteChars(int, int) {
}
void ImGuiInputTextCallbackData::InsertChars(int, const char*, const char*) {
}

void ImGuiStyle::ScaleAllSizes(float) {
}

ImGuiTextFilter::ImGuiTextFilter(const char*) {
}
void ImGuiTextFilter::Build() {
}
bool ImGuiTextFilter::Draw(const char*, float) {
    return false;
}
bool ImGuiTextFilter::PassFilter(const char*, const char*) const {
    return true;
}

ImGuiID ImGuiWindow::GetID(const char*, const char*) {
    return 0;
}
