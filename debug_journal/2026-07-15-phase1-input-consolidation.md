# 2026-07-15 — Phase 1: `zelda3d/input/` consolidation + keyboard-bug investigation

Phase 1 of the approved `zelda3d/` reorg (Phase 0 = b2c54138, tables/assets). Scope: build the
ONE input module, consolidate the "is input blocked" decision, and root-cause the keyboard bug
where title/Play (in-game) don't react to physical keys but file-select does. No build/run this
session per directive — verified by grep/read only; verification is deferred to the end of all
phases.

## What landed: `zelda3d/input/zelda3d_input.h` + `.cpp`

New files:
- `Shipwright/soh/src/zelda3d/input/zelda3d_input.h`
- `Shipwright/soh/src/zelda3d/input/zelda3d_input.cpp`

Moved in (pure code motion, no behavior change):
- `Zelda3D_InjectKey` — was orphaned in `zelda3d_model.cpp` (next to the .3ds model loader, unrelated).
- `Zelda3D_WalkInject` — the whole per-frame injector (walkhold/btnhold/ztarget re-assert/#71
  pause-nav/#16 FP_REPRO), moved verbatim from `zelda3d.c`.
- The `walkhold`/`btnhold` REPL command BODIES (`Zelda3D_Input_HandleWalkHoldCmd` /
  `Zelda3D_Input_HandleBtnHoldCmd`) + their backing globals (`gZelda3dWalkHoldFrames/StickX/StickY`,
  `gZelda3dBtnHoldFrames/Mask/First`), now `static` inside the new module (nothing else referenced
  them). `zelda3d.c`'s REPL dispatcher still does the `strcmp(cmd, "walkhold"/"btnhold")` routing —
  splitting the whole 149-command REPL chain is explicitly out of scope for this phase (the empty
  `zelda3d/repl/` dir from Phase 0 is where that belongs, Phase 2+).
- `Zelda3D_XboxBtnEnabled`, `Zelda3D_InputDevice`, `Zelda3D_HotbarSlot`, `Zelda3D_HotbarSync` +
  their backing globals (`gZelda3dXboxBtn`, `gZelda3dInputDevice`, `gZelda3dHotbarOn/Items/Active/
  FireB`) — these stay `extern`-declared in `zelda3d.h` (many other files read them directly:
  `z_parameter.c`, `Controller.cpp`), only the DEFINITIONS moved.
- A new consolidated `Zelda3D_DbgInputEnabled()` — was a private `static` lambda duplicated
  verbatim in both `Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp` and
  `Shipwright/libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp`. Now one
  definition; both call sites forward-declare it `extern "C"` locally (libultraship does not
  `#include` soh headers — one-directional dependency, same pattern as `Zelda3D_MeasureResult` /
  the native-HUD entry point in `Gui.cpp`).

**Stayed in `zelda3d.c`** (deliberately, with a comment explaining why): `gZelda3dGCam`,
`gZelda3dZTargetActor`, `gZelda3dPauseTarget` (now non-`static` — `Zelda3D_WalkInject`, now in a
different TU, still reads them) — their own REPL handlers (`gcam`/`ztarget`/`pause`) are unaffected
by this pass and weren't named in the task scope.

Callers updated: `z_play.c:1836` (`Zelda3D_WalkInject` call, now via `#include
"zelda3d/input/zelda3d_input.h"`), `z_parameter.c` (added the same include, dropped its two local
`extern` decls for `Zelda3D_InputDevice`/`Zelda3D_XboxBtnEnabled`), `zelda3d.c` (added the include,
dropped the local `extern int Zelda3D_InjectKey(...)` in the `key` REPL handler), `zelda3d.h`
(removed the 4 moved function prototypes, left the backing-global `extern` decls in place, with a
pointer comment to the new header).

Verified via grep: no definitions of the moved symbols remain at their old locations; every caller
either already got the extern via `zelda3d.h`'s unaffected variable decls or now includes
`input/zelda3d_input.h` directly.

## Block-decision consolidation

Was ALREADY mostly fixed same-day, before this phase, by two prior sessions (65acc6c5, 583ecfb0 —
see `debug_journal/2026-07-15-keyboard-input-blocked-regression.md` and
`2026-07-15-keyboard-headed-v2.md`): `Ship::ControlDeck::KeyboardGameInputBlocked()`
(`Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp:139-159`) now returns
`AllGameInputBlocked()` only — the old ImGui-stub read (`ImGui::GetIO().WantCaptureKeyboard`,
provably always `false` since ImGui is a compile-only stub) was deleted outright, not patched.
`AllGameInputBlocked()` (line 128) is `!mGameInputBlockers.empty()`, and the ONLY live registrant
is `SohRmlUi::SetVisible()` (`ZELDA3D_RML_MENU_BLOCK_ID`) — `InputEditorWindow`'s block can never
fire (gated on `ImGui::IsPopupOpen()`, itself dead). So there is now genuinely ONE source of truth
(`mGameInputBlockers`), read from two call sites (`Ship::ControlDeck::ProcessKeyboardEvent`,
event-time diagnostic only; `LUS::ControlDeck::WriteToOSContPad` / `KeyboardKeyToButtonMapping::
UpdatePad`, poll-time gating) — that's by design (global early-exit + per-device-type gate), not a
duplicate-logic defect. This phase's contribution here: consolidating the *diagnostic* duplication
(the `Zelda3dDbgInputEnabled()` lambda copy-pasted in both ControlDeck.cpp files) into the new
module, and extending the diagnostic into the poll-time path (below) — the block-decision logic
itself needed no further change.

## Poll-time diagnostic extension

`Shipwright/libultraship/src/ship/controller/controldevice/controller/mapping/keyboard/
KeyboardKeyToButtonMapping.cpp` — `UpdatePad()` now logs (per-scancode, on-change only, via
`ZELDA3D_DBG_INPUT=1`): `keyPressed` (the latched `mKeyPressed` bool), `KeyboardGameInputBlocked`,
and whether the bit was actually OR'd into `padButtons`. Previously the diagnostic only covered
event-time (`ProcessKeyboardEvent`, proves the SDL event reached `ControlDeck`) and a global
per-frame `AllGameInputBlocked`-changed log (`LUS::ControlDeck::WriteToOSContPad`) — neither could
tell you whether a SPECIFIC key's latched press survived to the next poll. This closes that gap.

## Keyboard bug: root cause NOT pinned this session — here's why, and the concrete next test

The task brief states: title screen AND in-game Play don't react to physical keys, but
file-select does. This is NEW information relative to the prior two sessions' journals (which
only established "title + Play both dead", not the file-select contrast) — it also directly
CONTRADICTS the `2026-07-15-keyboard-input-blocked-regression.md` v3 addendum's closing theory
("the OS/compositor never delivers keyboard focus to this window at all"), because a
compositor-level focus failure would break file-select identically.

Traced this session, statically:
- `Graph_Update` -> `GameState_ReqPadData(gameState)` -> `GameState_Update(gameState)`
  (`Shipwright/soh/src/code/graph.c:297-303`) is the ONLY place `gameState->input[0]` gets filled,
  and it's completely generic — identical code path for title, Play, and file-select GameStates.
  `SohState_ApplyInputOverride` right after it is a weak symbol, no-op in the standalone `soh.elf`
  (only the harness build — `tools/soh3d_harness/soh_state.cpp` — defines it), confirmed not the
  drop for a normal user run.
- File-select (`z_file_choose.c:791-830`) and Play's debug/kaleido input reads
  (`z_kaleido_scope.c`) both read `this->state.input[0]` — the SAME struct, same delivery.
- Title (spot99) runs INSIDE the Play gamestate (memory: "Title scene = spot99, cs ported") — so
  whatever's Play-specific naturally also touches title, which is CONSISTENT with the reported
  title+Play symmetry, but doesn't explain the file-select asymmetry (file-select is a genuinely
  different top-level GameState).
- Checked every registrant of `BlockGameInput`/`mGameInputBlockers`
  (`grep -rn "BlockGameInput\|AllGameInputBlocked\|KeyboardGameInputBlocked"` across `Shipwright/`)
  — only `SohRmlUi::SetVisible()` (real) and `InputEditorWindow` (dead, ImGui-gated). Nothing
  gamestate-specific found; nothing that would set the block flag during title/Play and clear it
  only via file-select's code path.
- `Zelda3D_WalkInject` (moved this session) only touches `input[0]` when a REPL debug harness is
  ACTIVE (`gZelda3dWalkHoldFrames`/`gZelda3dBtnHoldFrames`/`gZelda3dPauseTarget`/`gZelda3dFpRepro`
  all default off/-1/0) — a no-op for a normal headed play session, ruled out as the standalone-user
  root cause (task's own candidate C note already flagged this as unlikely for exactly this reason).
- `SohRmlUi::ProcessSdlEvent` only toggles the menu on `SDLK_ESCAPE` or gamepad START
  (`SohRmlUi.cpp:578-596`) — the keyboard Start-equivalent (Enter, v2 scheme) does NOT touch
  `SetVisible`, so an accidental menu-open from pressing the expected title-skip key is not the
  mechanism either.

**No code-level differential between file-select's input read and title/Play's was found.** Every
gate that exists is either provably a single shared global (`AllGameInputBlocked`) or is not
gamestate-specific. That means either (a) the "file-select works" observation needs re-confirming
on the CURRENT tree (post 65acc6c5/583ecfb0 — it's possible the user's contrasting report predates
those fixes and file-select was never actually different, just tested last), or (b) the divergence
is inside Play/title's OWN *consumption* of `input[0]` after delivery (e.g. the title
cutscene correctly suppressing non-skip input while `TitlePresentation::isActive()` — real N64
behavior for a scripted demo, not a bug — but that doesn't explain a report of regular in-game Play
also being dead), which this session did not find a concrete mechanism for.

### Headed test recipe (extends the v2 recipe with the new poll-time log)

```
ZELDA3D_DBG_INPUT=1 ./run.sh
```
(NOT `ZELDA3D_HEADLESS=1` — this needs the real window.) At the title screen, press the Start-bound
key (Enter, v2 scheme) a few times; then get into file-select and press the same/another mapped
key; then in-game, press a movement key. Watch stderr for BOTH diagnostic halves now:

- `[zelda3d_dbg_input] key event=... consumed=... AllGameInputBlocked=... KeyboardGameInputBlocked=...`
  (event-time, `Ship::ControlDeck::ProcessKeyboardEvent`) — proves the SDL event arrived.
- `[zelda3d_dbg_input] poll scancode=... keyPressed=... KeyboardGameInputBlocked=... appliedToPad=...`
  (poll-time, NEW this session, `KeyboardKeyToButtonMapping::UpdatePad`) — proves the LATCHED press
  survived to the pad-fill poll and got OR'd into the N64 pad bits.

Compare the poll-time line's `appliedToPad` value between the title/Play attempt and the
file-select attempt for the SAME scancode:
- **If `appliedToPad=1` in BOTH title/Play and file-select**, the bug is NOT in `ControlDeck`/the
  input-mapping layer at all — the byte reaches `input[0].cur.button` either way, and the drop is
  downstream, inside Play/title's own reading of that state (next step: instrument
  `Player_Update`/the title cs skip check directly, not `ControlDeck`).
- **If `appliedToPad=0` (or the poll line never appears) during title/Play but `=1` during
  file-select for the identical key**, that's the smoking gun proving a real, currently-unlocated
  gamestate-conditional gate exists — re-open the `BlockGameInput` registrant search with that
  confirmed asymmetry in hand (grep again post-confirmation; something not caught by this session's
  static sweep is setting/clearing a block conditionally on gamestate).
- **If the event-time line fires but consumed=0** for the key being pressed, check the user's saved
  keymap (`.Zelda3D.ControlDeck.ButtonMappings.*.KeyboardScancode`) against the v2 scheme defaults —
  a stale pre-migration config is also consistent with "some keys/screens seem to work, others
  don't" if only some mappings got remapped.

## Files touched

- New: `Shipwright/soh/src/zelda3d/input/zelda3d_input.h`, `zelda3d_input.cpp`.
- `Shipwright/soh/src/zelda3d/zelda3d.c` — removed moved definitions, added the input-module
  include, REPL `walkhold`/`btnhold` bodies now delegate, `key` handler drops its local extern.
- `Shipwright/soh/src/zelda3d/zelda3d.h` — removed the 4 moved function prototypes (pointer
  comments left), backing-global `extern` decls unchanged.
- `Shipwright/soh/src/zelda3d/zelda3d_model.cpp` — removed `Zelda3D_InjectKey` + its now-unused
  `ship/Context.h`/`ControlDeck.h` includes.
- `Shipwright/soh/src/code/z_play.c`, `Shipwright/soh/src/code/z_parameter.c` — added
  `zelda3d/input/zelda3d_input.h` include; `z_parameter.c` dropped 2 redundant local `extern` decls.
- `Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp`,
  `Shipwright/libultraship/src/libultraship/controller/controldeck/ControlDeck.cpp` — both
  `Zelda3dDbgInputEnabled()` copies now forward to the consolidated `Zelda3D_DbgInputEnabled()`.
- `Shipwright/libultraship/src/ship/controller/controldevice/controller/mapping/keyboard/
  KeyboardKeyToButtonMapping.cpp` — new poll-time per-scancode diagnostic in `UpdatePad()`.
- `docs/codemap.md` — `input/` row marked done with the summary above.

## Not done / explicitly out of scope this phase

- Splitting the 149-command REPL dispatcher itself (`zelda3d/repl/`) — only the two command BODIES
  named in the task moved; the `strcmp` routing chain stays in `zelda3d.c` (Phase 2+, per the
  existing codemap plan).
- The keyboard bug's true root cause for the file-select-vs-title/Play asymmetry — needs the headed
  recipe above; static analysis (this session + 2 prior sessions) has exhausted the code paths that
  can be checked without a live run.
