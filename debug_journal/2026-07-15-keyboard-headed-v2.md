# Headed keyboard input still dead after WantCaptureKeyboard swap (v2)

## Symptom

Physical keyboard input does not reach the game in a real headed Wayland/GTK session — START
doesn't skip the title, no in-game keys work either. Does NOT reproduce headless (the REPL
injects pad state directly, bypassing the real SDL event -> ControlDeck path entirely, so
headless testing is structurally blind to this bug class). Commit 65acc6c5 changed
`ControlDeck::KeyboardGameInputBlocked()` from an ImGui-ActiveId heuristic to
`AllGameInputBlocked() || ImGui::GetIO().WantCaptureKeyboard` — user retested headed, still
broken.

## Root finding: ImGui is a compile-only stub — reading its IO state was ALWAYS dead code

Traced the whole software gating chain end to end:

```
SDL_EVENT_KEY_DOWN (gfx_sdl3.cpp HandleEvents/HandleSingleEvent)
  -> OnKeydown(scancode) -> TranslateScancode -> mOnKeyDown(key)
  -> Fast3dWindow::KeyDown(scancode)
  -> ControlDeck::ProcessKeyboardEvent(KEY_DOWN, scancode)   [Ship layer, per-port fan-out]
  -> Controller::ProcessKeyboardEvent -> ControllerButton::ProcessKeyboardEvent
  -> KeyboardKeyToButtonMapping::ProcessKeyboardEvent -> mKeyPressed = true
(next frame)
  LUS::ControlDeck::WriteToOSContPad()
    if (AllGameInputBlocked()) return;      <-- global early-out, ALL devices
    controller->ReadToPad(...) -> ControllerButton::UpdatePad
    -> KeyboardKeyToButtonMapping::UpdatePad
         if (KeyboardGameInputBlocked()) return;   <-- keyboard-only gate
         if (!mKeyPressed) return;
         padButtons |= mBitmask;
```

`Shipwright/libultraship/imgui_shim/imgui_stub.cpp` (added when ImGui was removed as a
renderer/UI dependency — RmlUi is the real menu now) replaces the *entire* Dear ImGui library
with no-ops:

- `ImGui::GetIO()` → `ZeroRef<ImGuiIO>()`: a `new char[sizeof(ImGuiIO)]()` value-initialized to
  all zero bytes — never the constructed default, never touched again. Every bool field
  (including `WantCaptureKeyboard`, `WantTextInput`) is hard-wired `false` forever.
- `ImGui::GetCurrentContext()` → `ZeroPtr<ImGuiContext>()`: same trick, a permanently-zeroed
  struct, so `->HoveredWindow` etc. are always null.
- `ImGui_ImplSDL3_NewFrame()` is a no-op, and **`ImGui::NewFrame()` is never called anywhere in
  the codebase** — grepped the whole tree, the only hits are comments/doc strings. `Gui::
  StartFrame()` / `Gui::ImGuiBackendNewFrame()` (`ship/window/gui/Gui.cpp`) are explicitly
  commented "ImGui removed: no NewFrame / backend new-frame."

So `ImGui::GetIO().WantCaptureKeyboard` was **provably always `false`** at runtime — the
65acc6c5 fix was a no-op in practice (`KeyboardGameInputBlocked()` was already behaviorally
`AllGameInputBlocked()` before this session's change). The user's retest correctly falsified it,
but not because the *logic* of that fix was wrong — because it wasn't touching anything live.
The real blocker (or drop point) is elsewhere. Re-derivation dead end noted here so no future
session "fixes" this by touching ImGui IO flags again — they are structurally inert.

### Related landmine found, NOT fixed this session (flagged for follow-up)

`ControlDeck::MouseGameInputBlocked()` (same file) does:
```cpp
ImGuiWindow* window = ImGui::GetCurrentContext()->HoveredWindow; // always nullptr (stub)
if (window == NULL) { return true; }                             // -> ALWAYS returns true
```
This means **mouse game input is unconditionally blocked** by the same dead-ImGui-read bug
class. Out of scope for this keyboard task (no user report of a mouse-driven game input gap),
but it's a live bug and should be fixed the same way (drop the ImGui read, gate on the real
menu/`IsInteractiveMenuOpen()`) in a follow-up pass.

## What did NOT turn out to be broken (ruled out by trace)

- `AllGameInputBlocked()` (the RmlUi menu blocker): `SohRmlUi::SetVisible()` is the only live
  registrant of `ZELDA3D_RML_MENU_BLOCK_ID`, starts `mVisible = false`, only flips on ESC/Start
  toggle. `InputEditorWindow`'s block (both LUS and soh copies) can never fire — it's gated on
  `ImGui::IsPopupOpen()` inside `UpdateElement()`, which is never called either
  (`Gui::DrawMenu()` is a no-op — "the registered GuiWindows are inert scaffolding").
- SDL event routing: `HandleSingleEvent` always runs `OnKeydown`/`OnKeyup` unconditionally after
  offering the event to RmlUi/ImGui — RmlUi consuming the event (`return true` in
  `HandleWindowEvents`) only skips the (also-dead) `ImGui_ImplSDL3_ProcessEvent` call, not the
  `switch` in `HandleSingleEvent` that drives `OnKeydown`. No early-return / focus filter found in
  `GfxWindowBackendSDL3::HandleEvents()`.
- Scancode translation (`TranslateScancode` / `mSdlToLusTable`) — unmodified upstream SoH
  machinery, not touched by any recent change; not implicated by the trace.

## Fix landed this session

Per user authorization ("if keyboard problem is ImGui just delete it") and the psxport reference
pattern (`../psxport/runtime/recomp/pad_input.cpp` gates keyboard reads purely on
`rml_overlay.wantsKeyboard()`, never ImGui state):

1. **`Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp`** —
   `KeyboardGameInputBlocked()` now returns `AllGameInputBlocked()` only. The
   `ImGui::GetIO().WantCaptureKeyboard` read is removed outright (it was dead code reading a
   stub, not a working check that regressed).
2. **New seam**: `Ship::Gui::IsInteractiveMenuOpen()` (virtual, default `false`) —
   `Shipwright/libultraship/include/ship/window/gui/Gui.h` /
   `Shipwright/libultraship/src/ship/window/gui/Gui.cpp`. Overridden in
   `Fast::Fast3dGui::IsInteractiveMenuOpen()` (`fast/Fast3dGui.h`/`.cpp`) to return
   `mRml && mRml->IsVisible()` — the actual live signal for "is the real menu open," reusable
   anywhere ImGui's dead `GetMenuOrMenubarVisible()` was previously (wrongly) relied on.
3. **Diagnostic, env-gated `ZELDA3D_DBG_INPUT=1`** (two halves, both log-on-change/log-on-event,
   not a spammy per-frame trace):
   - `Ship::ControlDeck::ProcessKeyboardEvent` (`ship/controller/controldeck/ControlDeck.cpp`)
     logs every real SDL key event with the decisive state at that instant: scancode, whether any
     mapping consumed it, `AllGameInputBlocked`, `KeyboardGameInputBlocked`, and
     `IsInteractiveMenuOpen` (RmlUi visibility). **If this line never prints while physically
     pressing keys, the drop is upstream of ControlDeck** (SDL isn't delivering the event at all —
     the leading suspect given every software gate downstream is proven either dead or correctly
     wired: Wayland/GTK keyboard focus not landing on the SDL window).
   - `LUS::ControlDeck::WriteToOSContPad` (`libultraship/controller/controldeck/ControlDeck.cpp`)
     logs whenever `AllGameInputBlocked()` changes value, so a stuck-`true` global blocker (no
     matter what registered it) is visible even if no key event ever fires.

## Headed test recipe for the user

```
ZELDA3D_DBG_INPUT=1 ./run.sh
```
(or however `run.sh` launches the headed binary — do NOT set `ZELDA3D_HEADLESS=1`, this needs the
real window). At the title screen, press START (Enter, per the v2 scheme) a few times, then in
game try a few movement/menu keys. Watch stderr:

- **No `[zelda3d_dbg_input] key event=...` lines at all**, ever, despite pressing keys → SDL is
  not delivering key events to this process. Check window focus (does the window have OS/compositor
  keyboard focus? is another window stealing it? `xdg_toplevel` activation on the WM). This is the
  leading suspect from this session's trace — everything downstream of the SDL event is proven
  either correctly wired or provably inert.
- **Lines print, `consumed=0` every time** → the keyboard scancode being pressed has no mapping
  (default keyboard mapping not applied, or a stale saved config with the pre-v2 scheme scancodes
  pointing at the wrong button). Check the user's config file's `.Zelda3D.ControlDeck.
  ButtonMappings.*.KeyboardScancode` entries against what's actually being pressed.
- **Lines print, `consumed=1`, but `AllGameInputBlocked=1` or `RmlMenuOpen=1`** → the RmlUi menu
  is open (or believes it is) when the user thinks it isn't; check `SohRmlUi::mVisible` desync
  (toggle logic / a stray ESC event / focus-loss auto-open).
- **Lines print, `consumed=1`, `AllGameInputBlocked=0`, `KeyboardGameInputBlocked=0`** → the fix in
  this session should make the key work; if it still doesn't reach the game, the drop is further
  downstream (`ReadToPad`/`UpdatePad` polling path — re-open investigation there, not here).

Also watch for `[zelda3d_dbg_input] AllGameInputBlocked changed: 0 -> 1 (pad fill SKIPPED)` with
no corresponding user action — that would mean something is calling `BlockGameInput()` without
ever calling `UnblockGameInput()`.

## Full ImGui deletion — scope assessment (not done this session)

ImGui the *library* is already gone (replaced by `imgui_shim/imgui_stub.cpp`); what remains is a
large amount of **dead ImGui-calling C++** kept only so it still compiles/links against the
stub headers (SoH's legacy dev menu/enhancements windows, `InputEditorWindow`,
`GamepadGameInputBlocked()`'s `GetMenuOrMenubarVisible()` check, `MouseGameInputBlocked()`'s
`HoveredWindow` check, `DrawFloatingWindows()`/viewport code, etc.). Deleting all of it is a real,
separate project:
- Every file that `#include <imgui.h>` and calls ImGui:: needs an audit for whether the calling
  code path is reachable at all (most of `Gui::DrawMenu()`'s registered windows are not, per this
  session's trace) — safe to delete outright — versus needs a small RmlUi-equivalent kept (e.g.
  `MouseGameInputBlocked()` genuinely needs *some* live signal, just not ImGui's).
  - Recommend as a follow-up card/sweep: grep `#include <imgui` across `Shipwright/`, classify
    each caller as (a) truly dead/unreachable → delete, (b) needs a live replacement → port to the
    `IsInteractiveMenuOpen()`-style seam added this session, (c) still legitimately used (unlikely
    — RmlUi replaced it everywhere that matters).
  - `imgui_shim/` itself (headers + stub) can only go once every last `#include <imgui.h>` caller
    is gone; that's the actual "delete ImGui" milestone.

## Files touched

- `Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp` — decisive fix +
  per-key-event diagnostic.
- `Shipwright/libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp` — per-frame
  `AllGameInputBlocked` change diagnostic.
- `Shipwright/libultraship/include/ship/window/gui/Gui.h` /
  `Shipwright/libultraship/src/ship/window/gui/Gui.cpp` — new `IsInteractiveMenuOpen()` seam.
- `Shipwright/libultraship/include/fast/Fast3dGui.h` /
  `Shipwright/libultraship/src/fast/Fast3dGui.cpp` — override reporting RmlUi visibility.
