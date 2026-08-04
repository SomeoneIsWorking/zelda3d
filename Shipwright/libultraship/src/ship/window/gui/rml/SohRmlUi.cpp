#include "SohRmlUi.h"
#include <vector>

#include "RmlUi_Platform_SDL.h"
#ifdef ENABLE_SDL3GPU
#include "RmlRenderInterfaceSdl3Gpu.h"
#endif

#include <fstream>
#include <iterator>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/EventListener.h>
// SDL3-MIGRATION: SDL2 -> SDL3 includes.
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>

#include "ship/Context.h"
#include "ship/window/Window.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "libultraship/bridge/consolevariablebridge.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Zelda3D render toggles live as extern "C" ints in libultraship's zelda3d_gl.cpp (the live state the
// GL pass reads each frame). The RML rows flip these directly for an immediate, visible effect and
// persist the choice to a CVar (which the GL pass also reads to seed the global at first use).
extern "C" {
}

// Debug-menu warp request: a row with `warp="<entrance>"` sets this to the target entrance index;
// zelda3d.c's per-frame Zelda3D_ReplPoll consumes it (it has the PlayState) and triggers the scene
// transition. -1 = nothing pending. Defined here (libultraship) so soh can extern-reference it.
extern "C" int gZelda3dMenuWarp = -1;

// Debug-menu restart request: a row with `restart="1"` sets this to 1; zelda3d.c's per-frame
// Zelda3D_ReplPoll consumes it (it has the PlayState) and returns to the title screen.
extern "C" int gZelda3dMenuRestart = 0;
// Launcher choice, consumed by the zelda3d layer (which owns process control -- this layer must not
// exec anything). 0 = nothing chosen yet, 1 = Ocarina of Time, 2 = Majora's Mask, 3 = quit.
// Set from the launcher document's `action="..."` rows exactly as the warp/restart rows above work.
extern "C" int gZelda3dLauncherAction = 0;
// The live SohRmlUi (there is exactly one, owned by Fast3dGui), so the C shims at the bottom of this
// file can drive the launcher from the zelda3d REPL without exposing RmlUi types.
namespace Ship {
class SohRmlUi;
}
static Ship::SohRmlUi* sLiveRmlUi = nullptr;

// Link render/anim mode, cycled by the `linkmode` row: 0 = N64 model + N64 anim, 1 = 3DS model +
// N64-retarget anim, 2 = 3DS model + 3DS-own CSAB anim. zelda3d.c's Zelda3D_ReplPoll applies it to
// gZelda3dLinkOn/gZelda3dLinkAnimSrc (and seeds it from the current mode on the first frame). DEFINED
// here in libultraship because charcompare also links against it.
extern "C" int gZelda3dMenuLinkMode = 0;

// Time-of-day applied on the NEXT Debug-menu warp, cycled by the `warptime` row: 0 = scene default
// (clock runs), 1 = Day, 2 = Night. zelda3d.c reads this when it consumes gZelda3dMenuWarp and sets
// gZelda3dForceTime before the transition (so the new scene's Play_Init picks the right day/night set).
extern "C" int gZelda3dMenuWarpTime = 0;

// Generated stair step size, cycled by the `stairsize` row: 0 = Small, 1 = Medium, 2 = Large.
// zelda3d.c's Zelda3D_ReplPoll maps it to a step rise (Zelda3D_SetStairRiserY) and seeds it from the
// current rise on the first frame.
extern "C" int gZelda3dMenuStairSize = 1;

// Debug-warp era applied to the NEXT Level-Select / Boss / Dungeon warp: 0 = Default (keep the
// current age), 1 = Child (past), 2 = Adult (future). zelda3d.c sets gSaveContext.linkAge from this
// before the warp, so Play_Init picks the child vs adult scene-setup layer (the past/future variant
// of the destination). Composes with gZelda3dMenuWarpTime (day/night).
extern "C" int gZelda3dMenuWarpAge = 0;

// Live on-screen diagnostics text (Link coords / scene / yaw / floor). Owned here (libultraship,
// no PlayState) and rewritten every frame by zelda3d.c's Zelda3D_ReplPoll, which DOES have the
// PlayState. The "Diag" RML pane's #diagtext element is refreshed from this buffer each frame
// (SohRmlUi::RefreshDiag), so a screenshot of that tab reports coords without the REPL FIFO.
extern "C" char gZelda3dDiagText[512] = "(waiting for game state...)";

// Unique id for blocking game input while the RML menu is open (sequence continues the existing
// *_BLOCK_ID constants in gfx_dxgi.cpp / InputEditorWindow.cpp). Without this, SoH polls the
// controller/keyboard directly and the game keeps responding under the open menu.
#define ZELDA3D_RML_MENU_BLOCK_ID 95237931

namespace Ship {

// Curated toggle rows: an RML row carrying `toggle="<id>"` maps to one render feature. Each entry
// names the persisted CVar and the live extern global the GL pass reads; the menu keeps both in sync.
struct ToggleSpec {
    const char* id;
    const char* cvar;
    int* live;
};
// EMPTY since the custom shadow-map/SSAO/half-Lambert effects were removed (2026-07-16, user
// directive: OoT3D lighting and shading only — there is nothing left to toggle). The machinery
// stays for future REAL settings rows.
static const std::vector<ToggleSpec> kToggles;
static const ToggleSpec* FindToggle(const Rml::String& id) {
    for (const auto& t : kToggles) {
        if (id == t.id) {
            return &t;
        }
    }
    return nullptr;
}
// Current on/off state for a toggle: prefer the live global (reflects REPL changes); when it is
// still uninitialised (-1, before the first GL frame) fall back to the persisted CVar.
static bool ToggleState(const ToggleSpec& t) {
    return *t.live >= 0 ? *t.live != 0 : CVarGetInteger(t.cvar, 1) != 0;
}
static void SetToggleValueText(Rml::Element* row, bool on) {
    if (Rml::Element* val = row->QuerySelector("value")) {
        val->SetInnerRML(on ? "On" : "Off");
    }
}

// Curated knob rows: an RML row carrying `knob="<id>"` adjusts a CVar integer in a [min,max] range
// with a given step. Enter/Right increments, Left/Shift-Enter decrements. The displayed <value> is
// "N%" (percentage of max). These wire the Audio tab's volume sliders to SoH's live CVars — the
// audioMgr reads gSettings.Volume.* every retrace, so the change is audible on the next audio tick.
struct KnobSpec {
    const char* id;
    const char* cvar;
    int defaultVal; // initial CVar value if not yet persisted
    int minVal;
    int maxVal;
    int step;
};
static const KnobSpec kKnobs[] = {
    { "vol-master",     "gSettings.Volume.Master",    40,   0, 100, 10 },
    { "vol-music",      "gSettings.Volume.MainMusic", 100,  0, 100, 10 },
    { "vol-sfx",        "gSettings.Volume.SFX",       100,  0, 100, 10 },
};
static const KnobSpec* FindKnob(const Rml::String& id) {
    for (const auto& k : kKnobs) {
        if (id == k.id) {
            return &k;
        }
    }
    return nullptr;
}
static int KnobValue(const KnobSpec& k) {
    int v = CVarGetInteger(k.cvar, k.defaultVal);
    return std::max(k.minVal, std::min(k.maxVal, v));
}
static void SetKnobValueText(Rml::Element* row, const KnobSpec& k) {
    if (Rml::Element* val = row->QuerySelector("value")) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", KnobValue(k));
        val->SetInnerRML(buf);
    }
}
// Step the knob by one step in the given direction (+1 or -1). Returns true if the value changed
// (not already at the limit in that direction), false if the knob was already at its end — so the
// caller can fall through to tab switching when the user keeps pressing Left/Right past the end.
static bool StepKnob(const KnobSpec& k, int direction) {
    int cur = KnobValue(k);
    int next = cur + direction * k.step;
    next = std::max(k.minVal, std::min(k.maxVal, next));
    if (next == cur) {
        return false; // already at limit
    }
    CVarSetInteger(k.cvar, next);
    CVarSave();
    return true;
}

// Curated cycle rows: an RML row carrying `cycle="<id>"` steps through a fixed list of labels and
// writes the selected index into a live menu global (consumed by zelda3d.c's Zelda3D_ReplPoll). Unlike
// the on/off toggles these have N states. The displayed `<value>` is the current label.
struct CycleSpec {
    const char* id;
    int* live;                 // menu global (also defined as extern "C" above)
    const char* labels[4];     // labels[0..count-1]
    int count;
};
static const CycleSpec kCycles[] = {
    { "linkmode", &gZelda3dMenuLinkMode, { "N64", "3DS \xC2\xB7 N64 anim", "3DS \xC2\xB7 3DS anim", nullptr }, 3 },
    { "warptime", &gZelda3dMenuWarpTime, { "Default", "Day", "Night", nullptr }, 3 },
    { "warpage", &gZelda3dMenuWarpAge, { "Default", "Child \xC2\xB7 past", "Adult \xC2\xB7 future", nullptr }, 3 },
    { "stairsize", &gZelda3dMenuStairSize, { "Small", "Medium", "Large", nullptr }, 3 },
};
static const CycleSpec* FindCycle(const Rml::String& id) {
    for (const auto& c : kCycles) {
        if (id == c.id) {
            return &c;
        }
    }
    return nullptr;
}
static void SetCycleValueText(Rml::Element* row, const CycleSpec& c) {
    int idx = *c.live;
    if (idx < 0 || idx >= c.count) {
        idx = 0;
    }
    if (Rml::Element* val = row->QuerySelector("value")) {
        val->SetInnerRML(c.labels[idx]);
    }
}

// RmlUi runtime is process-global (Rml::Initialise / Rml::Shutdown). Track init so a second
// SohRmlUi (e.g. after a backend switch) does not double-initialise the library.
static bool sRmlLibraryInitialised = false;

// Click handler bound to one <tab>: switches the menu to that tab's index (mouse parity with the
// Left/Right keyboard/D-pad tab nav). Owned by SohRmlUi::mTabClickListeners.
class TabClickListener : public Rml::EventListener {
  public:
    TabClickListener(SohRmlUi* ui, int index) : mUi(ui), mIndex(index) {}
    void ProcessEvent(Rml::Event& /*event*/) override {
        mUi->SetActiveTab(mIndex);
    }

  private:
    SohRmlUi* mUi;
    int mIndex;
};

SohRmlUi::SohRmlUi() = default;

SohRmlUi::~SohRmlUi() {
    Shutdown();
}

bool SohRmlUi::Init(void* sdlWindow, void* glContext, int width, int height, bool vulkan, bool sdl3gpu) {
    if (mInitialised) {
        return true;
    }

    mSdlWindow = sdlWindow;
    mVulkan = vulkan;
    mSg = sdl3gpu;
    mWidth = width > 0 ? width : 1;
    mHeight = height > 0 ? height : 1;

    // SDL3 GPU is the only renderer (P4); the menu always records through the SDL3 GPU interface,
    // appending its geometry as ops into the backend's unified op-list. (mVulkan/GL3 paths removed.)
    Rml::RenderInterface* renderInterface = nullptr;
#ifdef ENABLE_SDL3GPU
    mSgRenderInterface = std::make_unique<RmlRenderInterfaceSdl3Gpu>();
    mSgRenderInterface->SetViewport(mWidth, mHeight);
    renderInterface = mSgRenderInterface.get();
#else
    SPDLOG_ERROR("[SohRmlUi] ENABLE_SDL3GPU is off; the RmlUi menu has no render interface");
    return false;
#endif
    mSg = true;

    mSystemInterface = std::make_unique<SystemInterface_SDL>();
    mSystemInterface->SetWindow(static_cast<SDL_Window*>(sdlWindow));

    // SDL starts a process with text input (IME) ENABLED by default. During gameplay no RmlUi text
    // field is focused, so the live IME makes Wayland/KDE compositors pop an "alternative
    // character"/accent-compose widget when a key is held (e.g. holding S shows ś š ş ß §), which
    // swallows the keypress before the game reads it -> held movement keys appear dead. RmlUi only
    // (re)enables text input when a text field is actually focused (ActivateKeyboard) and disables it
    // on blur, so clearing the startup default-on state here is sufficient and keeps the invariant
    // "text input is ON iff editing a field". Reuses DeactivateKeyboard() for the SDL2/3-correct call.
    mSystemInterface->DeactivateKeyboard();

    // Interfaces must be installed before Rml::Initialise().
    Rml::SetSystemInterface(mSystemInterface.get());
    Rml::SetRenderInterface(renderInterface);

    if (!sRmlLibraryInitialised) {
        if (!Rml::Initialise()) {
            SPDLOG_ERROR("[SohRmlUi] Rml::Initialise failed");
            mSystemInterface.reset();
            return false;
        }
        sRmlLibraryInitialised = true;
    }

    // Font + document live next to the executable (copied there at build time). Absolute paths
    // resolve through RmlUi's default file interface regardless of the process working directory.
    const std::string fontPath = Context::GetPathRelativeToAppBundle("assets/rml/LatoLatin-Regular.ttf");
    if (!Rml::LoadFontFace(fontPath, true)) {
        SPDLOG_ERROR("[SohRmlUi] Failed to load font face: {}", fontPath);
    }
    LoadLauncherFonts();

    mContext = Rml::CreateContext("zelda3d", Rml::Vector2i(mWidth, mHeight));
    if (!mContext) {
        SPDLOG_ERROR("[SohRmlUi] Rml::CreateContext failed");
        Shutdown();
        return false;
    }

    const std::string docPath = Context::GetPathRelativeToAppBundle("assets/rml/zelda3d_test.rml");
    mDocument = mContext->LoadDocument(docPath);
    if (!mDocument) {
        SPDLOG_ERROR("[SohRmlUi] Failed to load document: {}", docPath);
        Shutdown();
        return false;
    }
    // Start hidden; the menu is shown on demand via ToggleVisible() (Phase 2). The document stays
    // loaded either way — we gate update/render + input on mVisible.
    mDocument->Show();
    // The OoT/MM launcher is a SECOND document in the same context. It is loaded here and left
    // hidden; ShowLauncher() swaps which of the two is up. Failing to load it is NOT fatal -- the
    // game must still boot without a launcher -- so this warns and continues.
    const std::string launcherPath = Context::GetPathRelativeToAppBundle("assets/rml/zelda3d_launcher.rml");
    mLauncherDoc = mContext->LoadDocument(launcherPath);
    if (!mLauncherDoc) {
        SPDLOG_WARN("[SohRmlUi] Failed to load launcher document: {} (game still boots)", launcherPath);
    }
    // Scale the dp-authored sheet to this display's content scale (HiDPI).
    ApplyDensityRatio();
    // Mouse parity: clicking a <tab> switches to it (keyboard/D-pad Left/Right already do).
    AttachTabClickHandlers();
    // The in-game HUD is native SoH Fast3D (Interface_Draw / HealthMeter_Draw); RmlUi is for the
    // ESC menu only.

    SPDLOG_INFO("[SohRmlUi] RmlUi initialised ({}x{}) — {} (SDL3 GPU)", mWidth, mHeight, docPath);
    mInitialised = true;
    sLiveRmlUi = this; // reachable from the C REPL shims above
    // Debug: open the menu at startup (deterministic verification via the screenshot harness, no
    // input injection needed). Normal use opens it with ESC / the Start button.
    if (const char* e = std::getenv("ZELDA3D_RMLUI_OPEN"); e && e[0] == '1') {
        SetVisible(true);
    }
    // Same deal for the launcher: ZELDA3D_LAUNCHER=1 brings it up at startup so it can be captured
    // headlessly with no input injection. This is the DEBUG entry point; the real one is the
    // zelda3d layer showing it before the game is under way.
    if (const char* e = std::getenv("ZELDA3D_LAUNCHER"); e && e[0] == '1') {
        ShowLauncher(true);
    }
    return true;
}

// Fonts the launcher stylesheet asks for by family name. RmlUi has no @font-face, so every family
// used in RCSS must be registered here first or the element silently renders nothing but a warning.
//
// The interesting one is `chiaro`. That is Zelda64Recomp's heading face and recomp.rcss asks for it
// in a dozen places (headings, menu row labels, the version label). We do not ship it -- no licence
// for it exists upstream, see ATTRIBUTION.md -- so rather than editing a GENERATED stylesheet in a
// dozen places (which regenerating the Sass would silently undo), Lato is registered UNDER THE NAME
// `chiaro`. Every `font-family: chiaro` then resolves to a real face and the sheet stays pristine.
// Dropping the licensed ChiaroNormal/ChiaroBold.otf into assets/rml/ and pointing these two calls at
// them is the whole change needed to restore the original look.
void SohRmlUi::LoadLauncherFonts() {
    struct FaceSpec {
        const char* file;
        const char* family; // nullptr = use the font's own family name
        Rml::Style::FontWeight weight;
    };
    static const FaceSpec kFaces[] = {
        { "assets/rml/LatoLatin-Bold.ttf", nullptr, Rml::Style::FontWeight::Bold },
        // Chiaro stand-ins (see above).
        { "assets/rml/LatoLatin-Regular.ttf", "chiaro", Rml::Style::FontWeight::Normal },
        { "assets/rml/LatoLatin-Bold.ttf", "chiaro", Rml::Style::FontWeight::Bold },
        // Controller/prompt glyphs, SIL OFL 1.1 and shipped with its licence.
        { "assets/rml/promptfont/promptfont.ttf", "promptfont", Rml::Style::FontWeight::Normal },
    };
    for (const FaceSpec& f : kFaces) {
        const std::string path = Context::GetPathRelativeToAppBundle(f.file);
        bool ok;
        if (f.family == nullptr) {
            ok = Rml::LoadFontFace(path, false, f.weight);
        } else {
            // The family-override overload takes a byte span, so the file is read here -- and the
            // buffer MUST OUTLIVE RmlUi. Its contract is explicit ("the pointed to 'data' must
            // remain available until after the call to Rml::Shutdown"): FreeType keeps a pointer
            // into it rather than copying. A local vector here crashes the process moments later,
            // which is exactly what it did before this was a static.
            static std::vector<std::vector<Rml::byte>> sFontBuffers;
            std::ifstream in(path, std::ios::binary);
            sFontBuffers.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::vector<Rml::byte>& data = sFontBuffers.back();
            ok = !data.empty() &&
                 Rml::LoadFontFace(Rml::Span<const Rml::byte>(data.data(), data.size()), f.family,
                                   Rml::Style::FontStyle::Normal, f.weight, false);
        }
        // A missing face is reported, never swallowed: the symptom otherwise is text that simply
        // does not draw, with the cause buried in an RmlUi warning per element per frame.
        if (!ok) {
            SPDLOG_ERROR("[SohRmlUi] launcher font FAILED to load: {} (family {})", path,
                         f.family ? f.family : "<own>");
        }
    }
}

// C shims: the zelda3d REPL drives the launcher but must not include RmlUi headers. A file-static
// pointer to the live instance is enough -- there is exactly one, owned by Fast3dGui.
extern "C" void Zelda3D_LauncherShow(int show) {
    if (sLiveRmlUi) {
        sLiveRmlUi->ShowLauncher(show != 0);
    }
}
extern "C" int Zelda3D_LauncherIsVisible(void) {
    return (sLiveRmlUi && sLiveRmlUi->IsLauncherVisible()) ? 1 : 0;
}

void SohRmlUi::ShowLauncher(bool show) {
    if (!mInitialised || !mContext || mLauncherDoc == nullptr || show == mLauncherVisible) {
        return;
    }
    mLauncherVisible = show;
    // Same input block the ESC menu uses -- the game is running behind the launcher and must not
    // see the keys that are driving it.
    if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetControlDeck()) {
        if (show) {
            ctx->GetControlDeck()->BlockGameInput(ZELDA3D_RML_MENU_BLOCK_ID);
        } else {
            ctx->GetControlDeck()->UnblockGameInput(ZELDA3D_RML_MENU_BLOCK_ID);
        }
    }
    if (show) {
        if (mDocument) {
            mDocument->Hide(); // never both at once
        }
        mLauncherDoc->Show();
        mContext->Update();
        // Focus the first focusable row so a controller/keyboard can drive it immediately, matching
        // how the ESC menu focuses its first row on open.
        Rml::ElementList rows;
        mLauncherDoc->GetElementsByTagName(rows, "button");
        for (Rml::Element* r : rows) {
            if (!r->GetAttribute<Rml::String>("action", "").empty()) {
                r->Focus();
                break;
            }
        }
    } else {
        mLauncherDoc->Hide();
    }
}

void SohRmlUi::SetVisible(bool visible) {
    if (visible == mVisible) {
        return;
    }
    mVisible = visible;
    // Block/unblock game input so the game doesn't react to keys/buttons while the menu is up
    // (SoH reads the controller by polling, so consuming SDL events alone isn't enough).
    if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetControlDeck()) {
        if (mVisible) {
            ctx->GetControlDeck()->BlockGameInput(ZELDA3D_RML_MENU_BLOCK_ID);
        } else {
            ctx->GetControlDeck()->UnblockGameInput(ZELDA3D_RML_MENU_BLOCK_ID);
        }
    }
    if (mVisible && mContext) {
        // Lay out, then apply the active tab (shows its pane, sets selected/active classes) and drop
        // focus onto that pane's first row so a controller/keyboard can drive it immediately.
        mContext->Update();
        SetActiveTab(mActiveTab);
    } else if (mContext) {
        if (Rml::Element* focus = mContext->GetFocusElement()) {
            focus->Blur();
        }
    }
}

void SohRmlUi::ScrollFocusIntoView() {
    if (!mContext) {
        return;
    }
    if (Rml::Element* f = mContext->GetFocusElement()) {
        // Nearest = only scroll when the row is off-screen (no jump while it's already visible).
        f->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
    }
}

void SohRmlUi::FocusNext() {
    if (mContext) {
        mContext->ProcessKeyDown(Rml::Input::KI_TAB, 0);
        mContext->ProcessKeyUp(Rml::Input::KI_TAB, 0);
        ScrollFocusIntoView();
    }
}

void SohRmlUi::FocusPrev() {
    if (mContext) {
        mContext->ProcessKeyDown(Rml::Input::KI_TAB, Rml::Input::KM_SHIFT);
        mContext->ProcessKeyUp(Rml::Input::KI_TAB, Rml::Input::KM_SHIFT);
        ScrollFocusIntoView();
    }
}

void SohRmlUi::ActivateFocused() {
    if (!mContext) {
        return;
    }
    Rml::Element* focus = mContext->GetFocusElement();
    if (!focus) {
        return;
    }
    // If the focused element is a container row (the whole row takes focus for a clear highlight),
    // toggle the control it wraps; otherwise activate the focused element directly. This lets a
    // controller "A"/Enter flip a checkbox while focus rests on the readable row, not the tiny box.
    // Debug warp rows: `warp="<entrance>"` requests a scene transition (level select / boss fight).
    // Launcher rows: `action="start_oot|start_mm|quit"`. Recorded for the zelda3d layer and the
    // launcher is dismissed; the actual game start / process swap happens there, not here.
    {
        const Rml::String action = focus->GetAttribute<Rml::String>("action", "");
        if (!action.empty()) {
            if (action == "start_oot") {
                gZelda3dLauncherAction = 1;
            } else if (action == "start_mm") {
                gZelda3dLauncherAction = 2;
            } else if (action == "quit") {
                gZelda3dLauncherAction = 3;
            } else {
                // An unknown action is a TYPO IN THE DOCUMENT, not a no-op to swallow silently --
                // it would read as "the button does nothing" and send someone into the C++.
                SPDLOG_ERROR("[SohRmlUi] launcher row has unknown action=\"{}\" -- ignored", action);
                return;
            }
            ShowLauncher(false);
            return;
        }
    }
    const Rml::String warp = focus->GetAttribute<Rml::String>("warp", "");
    if (!warp.empty()) {
        gZelda3dMenuWarp = std::atoi(warp.c_str());
        SetVisible(false); // close the menu so the transition is visible
        return;
    }
    // Restart row: `restart="1"` returns to the title screen (consumed in zelda3d.c, which has the
    // PlayState — same indirection as the warp rows above).
    if (!focus->GetAttribute<Rml::String>("restart", "").empty()) {
        gZelda3dMenuRestart = 1;
        SetVisible(false);
        return;
    }
    // Curated multi-state cycle rows (e.g. Link render/anim mode, warp time-of-day): step to the
    // next state in place rather than "clicking" a row.
    {
        const Rml::String cid = focus->GetAttribute<Rml::String>("cycle", "");
        if (const CycleSpec* c = cid.empty() ? nullptr : FindCycle(cid)) {
            int idx = *c->live;
            if (idx < 0 || idx >= c->count) {
                idx = 0;
            }
            *c->live = (idx + 1) % c->count;
            SetCycleValueText(focus, *c);
            return;
        }
    }
    // Curated CVar knob rows (e.g. volume): Enter/Activate increments by one step. Wraps at max.
    {
        const Rml::String kid = focus->GetAttribute<Rml::String>("knob", "");
        if (const KnobSpec* k = kid.empty() ? nullptr : FindKnob(kid)) {
            // At max: wrap around to min (so Enter keeps cycling the whole range).
            if (!StepKnob(*k, +1)) {
                CVarSetInteger(k->cvar, k->minVal);
                CVarSave();
            }
            SetKnobValueText(focus, *k);
            return;
        }
    }
    // Curated CVar toggle rows take priority: flip the feature in place rather than "clicking" a row.
    if (ToggleFocusedRow()) {
        return;
    }
    if (Rml::Element* control = focus->QuerySelector("input, select, button")) {
        control->Click();
    } else {
        focus->Click();
    }
}

void SohRmlUi::RefreshToggleRows() {
    if (!mDocument) {
        return;
    }
    Rml::ElementList rows;
    mDocument->GetElementsByTagName(rows, "select-button");
    for (Rml::Element* row : rows) {
        const Rml::String id = row->GetAttribute<Rml::String>("toggle", "");
        if (const ToggleSpec* t = id.empty() ? nullptr : FindToggle(id)) {
            SetToggleValueText(row, ToggleState(*t));
        }
        const Rml::String cid = row->GetAttribute<Rml::String>("cycle", "");
        if (const CycleSpec* c = cid.empty() ? nullptr : FindCycle(cid)) {
            SetCycleValueText(row, *c);
        }
        const Rml::String kid = row->GetAttribute<Rml::String>("knob", "");
        if (const KnobSpec* k = kid.empty() ? nullptr : FindKnob(kid)) {
            SetKnobValueText(row, *k);
        }
    }
}

void SohRmlUi::RefreshDiag() {
    if (!mDocument) {
        return;
    }
    Rml::Element* el = mDocument->GetElementById("diagtext");
    if (el == nullptr) {
        return;
    }
    // gZelda3dDiagText is a plain C string filled by zelda3d.c each frame; '\n' separates fields. RML
    // ignores raw newlines, so translate them to <br/> for the on-screen multi-line readout.
    Rml::String text;
    for (const char* p = ::gZelda3dDiagText; *p != '\0'; ++p) {
        if (*p == '\n') {
            text += "<br/>";
        } else {
            text += *p;
        }
    }
    if (text != el->GetInnerRML()) {
        el->SetInnerRML(text);
    }
}

bool SohRmlUi::ToggleFocusedRow() {
    if (!mContext) {
        return false;
    }
    Rml::Element* focus = mContext->GetFocusElement();
    if (!focus) {
        return false;
    }
    const Rml::String id = focus->GetAttribute<Rml::String>("toggle", "");
    const ToggleSpec* t = id.empty() ? nullptr : FindToggle(id);
    if (!t) {
        return false;
    }
    const bool next = !ToggleState(*t);
    *t->live = next ? 1 : 0;            // immediate effect (GL pass reads this next frame)
    CVarSetInteger(t->cvar, next ? 1 : 0); // persist the choice
    CVarSave();
    SetToggleValueText(focus, next);
    return true;
}

bool SohRmlUi::StepFocusedKnob(int direction) {
    if (!mContext) {
        return false;
    }
    Rml::Element* focus = mContext->GetFocusElement();
    if (!focus) {
        return false;
    }
    const Rml::String kid = focus->GetAttribute<Rml::String>("knob", "");
    const KnobSpec* k = kid.empty() ? nullptr : FindKnob(kid);
    if (!k) {
        return false;
    }
    // Returns false if already at the limit — caller falls through to tab switching.
    if (!StepKnob(*k, direction)) {
        return false;
    }
    SetKnobValueText(focus, *k);
    return true;
}

void SohRmlUi::SetActiveTab(int index) {
    if (!mContext || !mDocument) {
        return;
    }
    Rml::ElementList tabs, panes;
    mDocument->GetElementsByTagName(tabs, "tab");
    mDocument->GetElementsByTagName(panes, "pane");
    const int n = (int)std::min(tabs.size(), panes.size());
    if (n == 0) {
        return;
    }
    // Wrap around at the ends so left/right cycles through every tab.
    if (index < 0) {
        index = n - 1;
    } else if (index >= n) {
        index = 0;
    }
    mActiveTab = index;
    for (int i = 0; i < (int)tabs.size(); i++) {
        tabs[i]->SetClass("selected", i == index);
    }
    for (int i = 0; i < (int)panes.size(); i++) {
        panes[i]->SetClass("active", i == index);
    }
    // Reflect each curated row's live CVar/feature state, then lay out and focus into the pane.
    RefreshToggleRows();
    mContext->Update();
    FocusFirstInActivePane();
}

void SohRmlUi::AttachTabClickHandlers() {
    if (!mDocument) {
        return;
    }
    Rml::ElementList tabs;
    mDocument->GetElementsByTagName(tabs, "tab");
    mTabClickListeners.clear();
    mTabClickListeners.reserve(tabs.size());
    for (int i = 0; i < (int)tabs.size(); i++) {
        auto listener = std::make_unique<TabClickListener>(this, i);
        tabs[i]->AddEventListener("click", listener.get());
        mTabClickListeners.push_back(std::move(listener));
    }
}

void SohRmlUi::NextTab() {
    SetActiveTab(mActiveTab + 1);
}

void SohRmlUi::PrevTab() {
    SetActiveTab(mActiveTab - 1);
}

void SohRmlUi::FocusFirstInActivePane() {
    if (!mDocument) {
        return;
    }
    Rml::ElementList panes;
    mDocument->GetElementsByTagName(panes, "pane");
    if (mActiveTab < 0 || mActiveTab >= (int)panes.size()) {
        return;
    }
    // First row that opts into focus (tabindex="auto"); rows in hidden panes (display:none) are not
    // focusable, so Tab navigation naturally stays within the active pane.
    if (Rml::Element* first = panes[mActiveTab]->QuerySelector("[tabindex='auto']")) {
        first->Focus();
        ScrollFocusIntoView(); // reset a previously-scrolled pane back to its first row
    }
}

bool SohRmlUi::ProcessSdlEvent(void* sdlEvent) {
    if (!mInitialised || !mContext || !sdlEvent) {
        return false;
    }
    SDL_Event& ev = *static_cast<SDL_Event*>(sdlEvent);

    // Toggle bindings are always live (so the menu can be opened/closed): ESC on the keyboard, or
    // the Start button on a game controller. (F1 is SoH's existing ImGui menu, left alone.)
    // SDL3-MIGRATION: SDL_KEYDOWN -> SDL_EVENT_KEY_DOWN; ev.key.keysym.sym -> ev.key.key;
    // ev.key.repeat is now bool (== 0 still reads as "not a repeat").
    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE && !ev.key.repeat) {
        ToggleVisible();
        return true;
    }
    // SDL3-MIGRATION: SDL_CONTROLLERBUTTONDOWN -> SDL_EVENT_GAMEPAD_BUTTON_DOWN; ev.cbutton -> ev.gbutton;
    // SDL_CONTROLLER_BUTTON_START -> SDL_GAMEPAD_BUTTON_START.
    if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
        ToggleVisible();
        return true;
    }

    if (!mVisible && !mLauncherVisible) {
        return false;
    }

    // Menu is open: map directional input to focus nav, A/Enter to activate, B/Esc to close; pass
    // everything else (mouse, text, other keys) through the SDL platform shim. Consume it all so the
    // game does not also act on input while the menu is up.
    // SDL3-MIGRATION: event type enums renamed (SDL_KEYDOWN -> SDL_EVENT_KEY_DOWN etc.);
    // ev.key.keysym.sym -> ev.key.key; gamepad: SDL_CONTROLLERBUTTONDOWN -> SDL_EVENT_GAMEPAD_BUTTON_DOWN,
    // ev.cbutton -> ev.gbutton, SDL_CONTROLLER_BUTTON_* -> SDL_GAMEPAD_BUTTON_* (A->SOUTH, B->EAST).
    switch (ev.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (ev.key.key) {
                case SDLK_DOWN:
                    FocusNext();
                    return true;
                case SDLK_UP:
                    FocusPrev();
                    return true;
                case SDLK_RIGHT:
                    // If the focused row is a knob, Right increments it; otherwise switch tabs.
                    if (StepFocusedKnob(+1)) {
                        return true;
                    }
                    NextTab();
                    return true;
                case SDLK_LEFT:
                    // If the focused row is a knob, Left decrements it; otherwise switch tabs.
                    if (StepFocusedKnob(-1)) {
                        return true;
                    }
                    PrevTab();
                    return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    ActivateFocused();
                    return true;
                default:
                    RmlSDL::InputEventHandler(mContext, static_cast<SDL_Window*>(mSdlWindow), ev);
                    return true;
            }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (ev.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    FocusNext();
                    return true;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    FocusPrev();
                    return true;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    if (StepFocusedKnob(+1)) {
                        return true;
                    }
                    NextTab();
                    return true;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    if (StepFocusedKnob(-1)) {
                        return true;
                    }
                    PrevTab();
                    return true;
                case SDL_GAMEPAD_BUTTON_SOUTH: // A
                    ActivateFocused();
                    return true;
                case SDL_GAMEPAD_BUTTON_EAST: // B
                    SetVisible(false);
                    return true;
                default:
                    return true;
            }
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_KEY_UP:
            RmlSDL::InputEventHandler(mContext, static_cast<SDL_Window*>(mSdlWindow), ev);
            return true;
        default:
            return false;
    }
}

void SohRmlUi::ApplyDensityRatio() {
    if (!mContext || !mSdlWindow) {
        return;
    }
    // SDL3 reports the display content scale (1.0 on a 96-dpi monitor, 1.5/2.0 on HiDPI / fractional
    // scaling). RmlUi multiplies every `dp` unit by this ratio, so the sheet — authored in dp — renders
    // at a consistent physical size across displays. A debug override lets the headless harness verify
    // the scaling math (the Xvfb display always reports 1.0).
    float scale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(mSdlWindow));
    if (const char* e = std::getenv("ZELDA3D_RML_DPI")) {
        float v = (float)atof(e);
        if (v > 0.0f) {
            scale = v;
        }
    }
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    if (scale != mDpRatio) {
        mDpRatio = scale;
        mContext->SetDensityIndependentPixelRatio(scale);
    }
}

void SohRmlUi::Resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == mWidth && height == mHeight)) {
        return;
    }
    mWidth = width;
    mHeight = height;
    if (mContext) {
        mContext->SetDimensions(Rml::Vector2i(mWidth, mHeight));
    }
}


void SohRmlUi::UpdateAndRender() {
    if (!mInitialised || !mContext || (!mVisible && !mLauncherVisible)) {
        return;
    }

    // Track the live window size so the context + render target follow window resizes. The RmlUi
    // geometry is appended into the SAME unified op-list / framebuffer (fb 0) the game renders into,
    // so RmlUi's coordinate space MUST match that framebuffer's size — otherwise the menu/HUD is laid
    // out and rasterized at a different scale than the game and slides out of view.
    //
    // Use the engine Window as the single source of truth for that size (Window::GetWidth/GetHeight)
    // — the exact same call the interpreter uses to size fb 0 — instead of re-querying SDL here. That
    // guarantees RmlUi and the framebuffer agree by construction on EVERY platform, with no per-OS
    // branch to drift. (Window::GetDimensions already encapsulates the points-vs-pixels choice that a
    // HiDPI display forces — e.g. retina, where the pixel size is 2x the point size — so duplicating
    // an #ifdef here would just risk the two sizings diverging.) HiDPI physical element sizing is
    // handled separately by ApplyDensityRatio's dp ratio.
    if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetWindow()) {
        Resize((int)ctx->GetWindow()->GetWidth(), (int)ctx->GetWindow()->GetHeight());
        ApplyDensityRatio();
    }

    RefreshDiag();
    mContext->Update();

    // Render the ESC menu context — append the geometry as ops into the SDL3 GPU backend's unified
    // op-list (fb 0). SDL3 GPU is the only renderer (P4); the GL3 and Vulkan render paths were removed.
#ifdef ENABLE_SDL3GPU
    if (mSgRenderInterface) {
        mSgRenderInterface->SetViewport(mWidth, mHeight);
        if (mSgRenderInterface->BeginFrame()) {
            mContext->Render();
            mSgRenderInterface->EndFrame();
        }
    }
#endif
}

void SohRmlUi::Shutdown() {
    if (mContext) {
        Rml::RemoveContext(mContext->GetName());
        mContext = nullptr;
        mDocument = nullptr;
    }
    if (sRmlLibraryInitialised) {
        Rml::Shutdown(); // releases textures/geometry through the render interface; keep it alive here
        sRmlLibraryInitialised = false;
    }
#ifdef ENABLE_SDL3GPU
    if (mSgRenderInterface) {
        mSgRenderInterface->Shutdown();
        mSgRenderInterface.reset();
    }
#endif
    mSystemInterface.reset();
    mInitialised = false;
}

} // namespace Ship
