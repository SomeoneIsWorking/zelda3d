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

## v3: psxport comparative sweep (2026-07-15) — every psxport-derived lead already covered or moot; NO fix landed this session

User's lead: "psxport also had the same issue and it was fixed there." Did a file:line comparison of
psxport's SDL keyboard path (`../psxport/runtime/recomp/pad_input.cpp`,
`../psxport/runtime/recomp/gpu_gpu.cpp`) against soh3d's (`gfx_sdl3.cpp`, `Fast3dGui.cpp`,
`KeyboardKeyToAnyMapping.cpp`, `Fast3dWindow.cpp`). Result: **no unapplied psxport fix found** — every
real psxport input fix is either architecturally moot for soh3d's SDL3 build, or already landed in the
v1/v2 session above. No guess-patch applied (would violate the no-bandaid rule); this is a report, not
a fix.

### psxport's actual historical Wayland/KDE input fixes (traced via `git -C ../psxport log`)

1. **`dc8df469` "stop SDL/IME text-input from stealing WASD on Linux/KDE (#18)"** — SDL2-era bug: SDL
   left text input ON by default, so KDE's IME compose/accent-picker ate held WASD. Fix: call
   `SDL_StopTextInput()` every frame unless a UI field wants the keyboard.
   **Moot for soh3d**: psxport's own later comment (`pad_input.cpp:178-181`, added when psxport migrated
   to SDL3 in `3fd50c4f`) says outright: *"SDL3 leaves text input OFF by default (it is per-window and
   opt-in) ... so there is no IME/compose widget to suppress here any more (the old GH#18
   SDL_StopTextInput dance is unnecessary)."* soh3d is already SDL3-only (`gfx_sdl3.cpp`), so this bug
   class never applies. soh3d's `SohRmlUi::Init` (`SohRmlUi.cpp:243-250`) also proactively calls
   `DeactivateKeyboard()` once at startup, which is stricter than psxport ever needed for SDL3 — a
   confirmed non-issue, not a gap.
2. **`0eddfabb` "fix imgui keyboard-steal (WASD dead)"** — psxport was gating the keyboard read on
   `io.WantCaptureKeyboard` (true whenever the overlay is merely visible/hovered), not
   `io.WantTextInput` (true only when a text field is focused) — over-blocking gameplay keys any time
   the dev overlay was on screen. **Already fixed in soh3d's v1/v2 session** (this same journal, above):
   `ControlDeck::KeyboardGameInputBlocked()` had its `ImGui::GetIO().WantCaptureKeyboard` read deleted
   outright (proven dead — ImGui is a compile-only stub, `GetIO()` always returns a zeroed struct) and
   now gates purely on `AllGameInputBlocked()` (the real RmlUi-menu-open signal via the new
   `IsInteractiveMenuOpen()` seam). No live over-blocking source remains.
3. **`d76aad80` "fix WASD dead from drifting controller"** — phantom/drifting SDL gamepad ORing a
   direction into the pad mask. Not applicable — this is about gamepad axis handling, not keyboard
   delivery, and there's no evidence of a connected gamepad in the user's headed repro.

**No psxport commit anywhere touches Wayland window creation, keyboard focus/grab, or an SDL hint.**
Confirmed via `grep -rn "WAYLAND\|SetWindowKeyboardGrab\|RaiseWindow\|SDL_HINT" ../psxport/runtime
../psxport/game` — zero hits. psxport's `SDL_CreateWindow` call (`gpu_gpu.cpp:463-465`) is a plain
`SDL_CreateWindow(title, 960, 720, SDL_WINDOW_RESIZABLE|SDL_WINDOW_FULLSCREEN)` — no flags or hints
soh3d's `gfx_sdl3.cpp:404-438` doesn't already have (both just use `SDL_WINDOW_RESIZABLE` +
backend-surface flag). `SDL_Init` is `SDL_Init(SDL_INIT_VIDEO)` in both, byte-identical subsystem set.

### The one real architectural difference: event-driven latch (soh3d) vs per-frame state poll (psxport)

psxport's `Pad::pollSdl()` (`pad_input.cpp:174-182`) does `SDL_PumpEvents(); const bool* ks =
SDL_GetKeyboardState(NULL);` every frame and reads live key state directly — it never depends on
individual `SDL_EVENT_KEY_DOWN`/`KEY_UP` events being correctly latched. soh3d's path is fully
event-driven and stateful: `SDL_EVENT_KEY_DOWN` → `OnKeydown` → `Fast3dWindow::KeyDown` →
`ControlDeck::ProcessKeyboardEvent` → `KeyboardKeyToAnyMapping::ProcessKeyboardEvent` sets
`mKeyPressed = true` (`KeyboardKeyToAnyMapping.cpp:19-36`), and `UpdatePad` just reads that latched bool
next frame (`KeyboardKeyToButtonMapping.cpp:15-23`). This is a real, transferable pattern difference and
was the prime suspect for this session — **but tracing soh3d's own event-drain code shows it isn't
buggy**, so switching to polling would not fix anything currently provably broken:

- `GfxWindowBackendSDL3::HandleEvents()` (`gfx_sdl3.cpp:754-766`) drains the **entire** SDL event queue
  every call via `SDL_PeepEvents(..., SDL_EVENT_FIRST, SDL_EVENT_LAST)` (split only to skip the
  gamepad-add/remove range, itself handled elsewhere) — no per-window-ID filter, no early exit. Every
  queued `SDL_EVENT_KEY_DOWN`/`UP` reaches `HandleSingleEvent` unconditionally.
- `HandleSingleEvent` (`gfx_sdl3.cpp:694-751`) offers the event to RmlUi/ImGui first but **ignores the
  return value** before falling into the `switch` that calls `OnKeydown`/`OnKeyup` — confirmed (again)
  no consume-and-drop path exists for key events.
- `HandleEvents()` is called every single rendered frame, unconditionally, from
  `RunCommands()` (`Shipwright/soh/soh/OTRGlobals.cpp:1948`, `wnd->HandleEvents();` before any drawing) —
  not gated behind any menu/pause/focus state. No starvation path found.
- No `SDL_EVENT_WINDOW_FOCUS_LOST`/`GAINED` handling exists in `gfx_sdl3.cpp` at all (grepped, zero
  hits), and `Fast3dWindow::AllKeysUp()` / the `mOnAllKeysUp` callback it would route through is wired
  (`Fast3dWindow.cpp:109`) but **never invoked** anywhere in the SDL3 backend — so there's no
  focus-transition auto-clear silently eating keys either.

Net: soh3d's event→latch chain is provably correct end-to-end *given that SDL enqueues the KEY_DOWN
event at all*. `SDL_GetKeyboardState` would not diverge from this, because SDL3 populates that array
from the exact same platform-backend event stream — if Wayland never delivers keyboard focus to the
window, both the event path and a poll-based path see nothing. Switching to polling is not a no-op
change (different failure modes for dropped/misordered *individual* events, a real hardening win in
general) but there is no evidence in this codebase that soh3d is currently dropping or misordering
events it does receive — so it would not be a grounded fix for *this* symptom, only a defensive
rewrite. Per the no-bandaid rule, did not land it speculatively.

### Conclusion — the remaining suspect is outside application code, needs the headed diagnostic

Every software-side gate and event-plumbing step downstream of "SDL enqueues the KEY_DOWN event" is now
proven correct in both codebases. The only remaining explanation compatible with all evidence is that
the OS/compositor (Wayland/GTK/KDE, whichever the user runs) is not delivering keyboard focus/events to
this specific SDL window at all — a layer neither soh3d nor psxport's history has any special handling
for (psxport apparently just doesn't hit it, or the user hasn't hit it there under the same compositor).
**This needs the headed `ZELDA3D_DBG_INPUT=1` run from the v2 section above to confirm**: if
`[zelda3d_dbg_input] key event=...` never prints while physically pressing keys, that is the smoking gun
for "SDL/compositor never delivers the event" and the next step is compositor-side (check
`XDG_SESSION_TYPE`, whether the window actually has `xdg_toplevel` activation/focus, try
`SDL_VIDEODRIVER=x11` as an isolation test to see if the bug is Wayland-backend-specific) — not another
pass over soh3d's or psxport's C++.

### Dead ends recorded this session (do not re-derive)

- IME/`SDL_StopTextInput` dance (psxport GH#18) — moot on SDL3 for both projects.
- `WantCaptureKeyboard` vs `WantTextInput` over-blocking (psxport 0eddfabb) — already fixed in soh3d
  v1/v2 (`ImGui::GetIO()` read deleted from `KeyboardGameInputBlocked`).
- Gamepad drift masking keys (psxport d76aad80) — gamepad-specific, not this symptom.
- Wayland-specific SDL hint/flag in psxport — does not exist; psxport's window creation is plain.
- Event queue starvation / focus-loss auto-clear in soh3d's SDL3 backend — traced, not present.
