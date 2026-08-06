# Dusklight as the structural lead for Zelda3D's UI

`https://github.com/TwilitRealm/dusklight` — a shipping PC port of Twilight Princess (zeldaret/tp
decomp + **Aurora** engine + **Borealis** app services). **CC0**, so ideas *and* code may be taken
freely. Cloned at `~/repo/dusklight`; `git pull` before consulting.

User directive (2026-08-06): *"Use dusklight as inspiration for our port, it has very good codebase
and UI"* — and it is the lead for **code structure and UI**, not just a reference.

> **Read this before designing any UI subsystem.** Take the SHAPE, not a copy-paste: their code is
> written against the TP decomp's types. The value is the design decision and the failure mode it
> avoids.

## Consulting it is only real if the submodules are initialised

`extern/aurora` and `extern/borealis` ship **uninitialised** in a fresh clone (`git submodule
status` shows a leading `-`). Every grep into them then returns zero hits, which reads exactly like
"the engine does not do this" — a silent-negative instrument. This bit this session: a search for
the ImGui backend came back empty and nearly became the conclusion "Aurora has no ImGui backend".

```
cd ~/repo/dusklight && git submodule update --init --depth 1 extern/aurora extern/borealis
git submodule status          # both lines must NOT start with '-'
```

## The headline decision: TWO UI stacks, deliberately

| Stack | Where | What it drives |
|---|---|---|
| **RmlUi** | `src/dusk/ui/` | the shipped, game-facing UI (menus, settings, mod manager, achievements) |
| **Dear ImGui** | `src/dusk/imgui/` | developer overlays only (console, save editor, heap/process/camera overlays, actor spawner) |

They are not merged, on purpose: shipped UI and debug UI have different requirements and should not
share a framework.

**This reverses the direction Zelda3D was heading.** ImGui here had been reduced to a no-op header
shim (`Shipwright/libultraship/imgui_shim/`) and was slated for deletion. Under the Dusklight model
ImGui **stays** — the shim should be replaced by *real* Dear ImGui plus a renderer backend, and the
dev-tool windows become a maintained suite rather than inert scaffolding.

### Why the shim is actively harmful, not merely inert

Its stubs return zeroed, never-null storage. That is a sane contract for a stub, but it silently
turns every **live predicate** that reads ImGui state into a constant. Four real bugs in this repo
had that single shape: mouse input blocked every frame, mouse buttons unbindable, rumble
suppressed, floating windows never drawn. None logged anything. A shim under a dev-tool UI is
cheap; a shim under a predicate the game branches on is a bug generator.

## Restoring real ImGui — the concrete gap

Aurora's wiring (`extern/aurora/lib/imgui.cpp`) is the model: a **platform** backend plus a
**renderer** backend chosen per graphics API.

```
ImGui_ImplSDL3_InitForSDLRenderer(window::get_sdl_window(), renderer);
ImGui_ImplSDLRenderer3_Init(renderer);        // or ImGui_ImplWGPU_Init(&info)
... ImGui_ImplSDL3_ProcessEvent / NewFrame / RenderDrawData(data, pass)
```

We are SDL3 + **SDL3 GPU** (the only backend). Dear ImGui ships `imgui_impl_sdl3` and
`imgui_impl_sdlgpu3` upstream, so both halves exist — this is wiring, not a port.

What has to change, in order:

1. `Shipwright/libultraship/cmake/dependencies/common.cmake` — replace the `imgui_shim/` include
   dirs with a real Dear ImGui target: core (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
   `imgui_widgets.cpp`), `misc/cpp/imgui_stdlib.cpp`, and backends `imgui_impl_sdl3.cpp` +
   `imgui_impl_sdlgpu3.cpp`.
2. `Shipwright/libultraship/src/CMakeLists.txt:86` — drop `imgui_shim/imgui_stub.cpp`.
3. `Ship::Gui` (`src/ship/window/gui/Gui.cpp`) — `Init`, `StartFrame`, `EndFrame` and `DrawMenu`
   were **emptied out**, not just bypassed. Restore CreateContext / font atlas / NewFrame / Render.
4. `Fast::Fast3dGui` — `ImGuiBackendInit/Shutdown`, `ImGuiBackendNewFrame`, `ImGuiWMInit` and
   `ImGuiRenderDrawData` are all no-ops. `Fast3dGui::ImGuiRenderDrawData` documents the real gap:
   *"No ImGui SDL3-GPU renderer backend is stood up (P4)."*

**Watch the pass model.** Our renderer is a single unified op stream (N64 + 3DS + HUD + RmlUi in one
pass — see `docs/codemap.md`). `RmlRenderInterfaceSdl3Gpu` *collects* geometry during
`Rml::Context::Render()` and appends it as one draw op rather than opening its own pass.
`ImGui_ImplSDLGPU3_RenderDrawData(data, cmd_buf, render_pass)` takes a pass it is handed, so ImGui
can reuse ours — but it must be handed the live pass, not open a second one.

Also note `imgui_shim/imconfig.h:144` does `#define ImTextureID void*`, overriding imgui.h's `ImU64`
default. Real ImGui must keep that override or ~77 call sites need casts.

## The UI component model worth adopting

`src/dusk/ui/component.hpp` — one small abstraction the whole game UI is built from:

- **`Component`** wraps an `Rml::Element* root`, and *owns* its children (`vector<unique_ptr<Component>>`)
  and its event listeners (`vector<unique_ptr<ScopedEventListener>>`). Listeners are RAII —
  destroying a component detaches them, so there is no manual teardown to forget.
- `add_child<T>(args...)` constructs in place and returns a reference; `selected()`/`disabled()` map
  onto RmlUi pseudo-classes rather than shadow state, so CSS stays the source of truth.
- **`FluentComponent<Derived>`** (CRTP) gives chainable `listen(...)` / `on_nav_command(...)`.
- **`NavCommand`** (`nav_types.hpp`) is a small enum — `Up/Down/Left/Right/Next/Previous/Confirm/
  Cancel/Menu` — so keyboard, gamepad and mouse all funnel into *one* handler per widget.
- Widgets are one pair of files each: `button`, `bool_button`, `number_button`, `modal`,
  `menu_bar`, `overlay`, `controller_config`, `graphics_tuner`, `mods_window`, `logs_window`. Each
  takes a `Props` struct of `std::function` getters/setters (`getValue`/`setValue`/`isDisabled`/
  `isModified`/`valueOverride`) — the widget owns presentation, the caller owns the data.

**Our gap:** `SohRmlUi` is a single class hand-rolling focus, tab switching, toggle rows and knob
stepping (`FocusNext`, `PrevTab`, `ToggleFocusedRow`, `StepFocusedKnob`, `AttachTabClickHandlers`,
…). That is the exact surface Dusklight decomposes into `Component` + `NavCommand` + per-widget
`Props`. Migrating it is incremental: introduce `Component`/`NavCommand`, then move one widget kind
at a time, leaving the monolith as the fallback until it is empty.

## Top-level file layout

`src/dusk/` is flat and **one pair of files per feature** — `achievements`, `autosave`,
`frame_interpolation`, `game_clock`, `gyro`, `mouse`, `speedrun`, `texture_replacements`,
`touch_camera`, … with only `audio/`, `ui/`, `imgui/`, `mods/` as subdirectories. No
`core/everything.cpp`.

This is the same rule already in `CLAUDE.md` ("per-behavior modules, NOT one giant soh3d.c"),
independently arrived at by a shipping port. It is evidence for the rule, not a new one.

## Frame interpolation

`src/dusk/frame_interpolation.{h,cpp}` — record-and-replace, not substitute-and-re-issue. See the
summary in the global `CLAUDE.md`; read the file before designing interpolation here.
