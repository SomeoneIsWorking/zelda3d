# Wiring Majora's Mask into Zelda3D — the integration plan

> **⚠️ SUPERSEDED 2026-07-01 — this whole doc describes the ABANDONED recompilation path.**
> The project pivoted to a **fully native MM** (native MM decomp on libultraship, the "2 Ship 2
> Harkinian"/2S2H model — same way soh3d gets OoT). There is **no Zelda64Recomp, no MIPS, no RDRAM
> transcoder** anymore. See `MM_NATIVE.md` for the live plan and the `mm-renderer-topology` memory
> for the decision. Everything below (the recomp seams, the transcoder, milestones 3/4a/4b/4c) is
> kept only as historical reference; do NOT build on it.

**Status:** plan + verified groundwork (2026-07-01). Topology **settled** (see
`ARCHITECTURE.md` → "Topology — SETTLED"). This is the keystone doc for the MM side;
it captures the two interface seams so they are never re-derived, the unification
mechanism, and the milestone ladder. Asset half is **verified on real data**; the
engine re-host is the remaining work.

## Goal (restated)

Zelda3D = one PC app hosting **both OoT and MM**, each drawn with its **3DS models**
(OoT3D / MM3D) under **one renderer**, plus gameplay enhancements + intuitive controls.
OoT already works this way in **soh3d** (SoH game logic, OoT3D CMB models, unified
SDL3-GPU renderer). MM is the analog: **Zelda64Recomp's MM game logic**, **MM3D** CMB
models, **the same soh3d renderer**. Not the prebuilt N64 binary; not a second renderer.

We are **free to modify both upstream projects** to unify them (user, 2026-07-01) — the
seams below are starting points, not constraints.

## The two seams (verified by source map, 2026-07-01)

### A. MM side — where MM's graphics leave the game (Zelda64Recomp)

The recompiled MM core has **zero dependency on RT64**. The entire renderer surface is one
pure-virtual class; swapping renderers is swapping one factory function pointer.

- Interface: `ultramodern::renderer::RendererContext` (pure virtual) —
  `lib/N64ModernRuntime/ultramodern/include/ultramodern/renderer_context.hpp:75`.
  Methods that matter:
  - `send_dl(const OSTask* task)` — **:86**. `task->t.data_ptr` is a pointer into the
    recomp's **8 MB big-endian RDRAM** holding a genuine **N64 F3DEX2 display list**.
    `ucode`/`ucode_data` identify the microcode. This is the frame's geometry.
  - `update_screen()` — **:87** (VI swap / present).
  - `update_config` :83, `enable_instant_present` :85, `shutdown` :88,
    `get_display_framerate` :89, `get_resolution_scale` :90.
- Where it's called (the interception point): the dedicated **Gfx thread** dequeues an
  `M_GFXTASK` `OSTask` and calls `renderer_context->send_dl(&task)` at
  `lib/N64ModernRuntime/ultramodern/src/events.cpp:370`. Task-type fork (`M_GFXTASK` →
  Gfx thread) at `events.cpp:553` (`submit_rsp_task`). Non-gfx microcode (audio `aspMain`,
  jpeg) takes the SP-task thread — leave that untouched.
- Current implementer (what we replace): `RT64Context` —
  `include/zelda_render.h:19`, `src/main/rt64_render_context.cpp`. Its `send_dl`
  (`:321`) calls `app->processDisplayLists(RDRAM, data_ptr & 0x3FFFFFF, 0, true)` (`:325`)
  — RT64's GBI interpreter reading directly from RDRAM.
- The swap: the factory is registered once via
  `ultramodern::renderer::callbacks_t{ .create_render_context = ... }` at
  `src/main/main.cpp:686`. Replace that one function pointer with a soh3d-backed factory
  and the unchanged MM core renders through soh3d. RT64 files become droppable:
  `rt64_render_context.cpp`, `zelda_render.h`, the `lib/rt64` link in `CMakeLists.txt`.
- Game core linkage: `RecompiledFuncs` (generated MIPS→C) + `PatchesLib` (`patches/`,
  hand-written C overrides — **this is how we inject MM-side draw substitution**, the MM
  analog of SoH editing `Actor_Draw`) + `librecomp` + `ultramodern`. One executable today
  (`CMakeLists.txt:343`), but the game core never references RT64.
- Loop/input: outer host loop `recomp.cpp:818`; game logic on its own thread
  (`recomp.cpp:805`); input `src/game/input.cpp` (`poll_inputs` :469) registered at
  `main.cpp:701`; window `main.cpp:138`.

### B. soh3d side — where the renderer accepts a frame

The renderer is libultraship's **Fast3D** stack built as the **static lib `libultraship`**
(`libultraship/src/CMakeLists.txt:3`). The game executable links it; the game logic lives
in the executable's sources, talking to the renderer over a narrow C-ABI. **None of the
renderer is OoT-specific.**

- Backend vtable: `GfxRenderingAPI` (`libultraship/include/fast/backends/gfx_rendering_api.h:30`);
  sole backend `GfxRenderingAPISdl3Gpu` (`.../gfx_sdl3gpu.h:108`,
  `src/fast/backends/gfx_sdl3gpu.cpp`). Unified "N64 tris + 3DS CMB in one pass" =
  deferred op-list `mOps` replayed in `FinishRender`; the CMB appenders are
  `AppendSoH3DModelDraw/HudDraw/Fullscreen/OwnPass` (`gfx_sdl3gpu.h:138..155`).
- The F3DEX HLE (gfx_pc lineage): `Interpreter::Run` in
  `libultraship/src/fast/interpreter.cpp`, driven per-frame by
  `Fast3dWindow::DrawAndRunGraphicsCommands` (`libultraship/src/fast/Fast3dWindow.cpp:178`):
  `StartFrame` → `Interpreter::Run(commands)` → `EndFrame` → `FinishRender`.
- CMB substitution (the 3DS-model path), all **game-driven via custom GBI opcodes**:
  - Game decides per actor: `SoH3D_TryDrawActor` (`soh/src/soh3d/soh3d.c:2233`), hooked at
    `Actor_Draw` (`soh/src/code/z_actor.c:2819`). Keys on actor id (`sModelTable` :1995),
    id+params special cases, and object id → OoT3D ZAR (`kSoH3dObjectZars` :200, auto path).
  - Game emits the draw: `SoH3D_EmitModelDraw` (`soh3d.c:1792`) → opcode
    `OTR_G_SOH3D_DRAW (0x41)`; pass bracket `OTR_G_SOH3D_RENDERPASS (0x4b)`; auto-scale
    `OTR_G_SOH3D_MEASURE (0x4a)`.
  - Interpreter handles them: `interpreter.cpp:4378` (`SoH3D_GL_Submit`), `:4413`
    (`SoH3D_GL_RenderPass`), `:4445` (measure).
  - Backend-agnostic host API: `SoH3D_GL_Submit/RenderPass/SetModelProvider`
    (`libultraship/include/fast/soh3d_gl.h`, impl `soh3d_gl.cpp`) → `SoH3D_Sg_*`
    (`soh3d_sdl3gpu.cpp:1073…`) → `AppendSoH3DModelDraw`.
  - **`SoH3DModelProvider`** callback `int(int modelId, const SoH3DGlGroup**, int*,
    const SoH3DGlTex**, int*)` — the renderer pulls model geometry through this; decoded
    from CMB by `soh3d_model.cpp` (`SoH3D_EnsureModelProvider` → `SetModelProvider`,
    `:1023`). Vertex/material structs `SoH3DGlVtx`/`SoH3DGlGroup`/`SoH3DGlTex`
    (`soh3d_gl.h:20,38,80`). **`SoH3DGlVtx` is byte-compatible with our
    `SoH3D::CmbVertex`** (`src/cmb3d/cmb.h:84` says so explicitly) — the provider is a thin
    bridge over the cmb3d core.

### The unification point (the crux) — a swizzle-aware DL transcoder

**Correction (2026-07-01, verified in source).** An earlier draft of this section claimed the
seam was merely "point soh3d's address resolver at RDRAM (big-endian aware)" — that is wrong
and would have sent the next session down a dead end. The two sides do *not* share a memory
representation, only a command grammar:

- **soh3d's `Interpreter::Run`** (`interpreter.cpp:5397`) walks a host array of `F3DGfx`
  commands and reads everything it points at as **ordinary contiguous host-native-endian
  structs at host pointers**: `GfxSpVertex` reads `v->ob[0]` directly (`interpreter.cpp:1607`);
  `SegAddr` (`:3352`) resolves a segmented operand to `mSegmentPointers[seg] + offset` where
  the segment bases are **host pointers**. No byte-swap anywhere.
- **The recomp's RDRAM** is the standard recompiler **swizzled layout**: an aligned 32-bit
  word reads native-LE (`MEM_W` is a plain `*(int32_t*)`, `recomp.h:95`), but 16-bit and
  8-bit accesses are XOR-swizzled within the word (`MEM_H` uses `^2`, `MEM_B` uses `^3`,
  `recomp.h:98`). RT64 consumes this directly by declaring its read structs in *swizzled
  field order* — see `rt64_rsp.h:36` `struct Vertex { int16_t y; int16_t x; … color{a,b,g,r} }`
  (halves swapped, bytes reversed). soh3d's `F3DVtx` is in natural order, so a raw
  reinterpret of recomp RDRAM through soh3d's interpreter yields byte-swapped garbage.

So feeding recomp RDRAM to soh3d's interpreter is **not** an address-resolver swap. The
faithful, bounded bridge is a **swizzle-aware F3DEX2 transcoder**:

> Walk the recomp's F3DEX2 DL out of RDRAM through swizzle-aware accessors (the `^2`/`^3`
> convention) and emit an equivalent **host-native** `F3DGfx` command array for soh3d's
> `Interpreter::Run`, materializing each referenced block (vertices, matrices, sub-DLs,
> textures) into un-swizzled host buffers and rewriting the command's address operand to the
> host pointer. This mirrors RT64's `RSP` DL walker (`rt64_rsp.cpp`) — same job, soh3d
> commands as the output instead of RT64 draw calls — and reuses 100% of soh3d's
> shader/combiner/SDL3-GPU backend.

**Key simplification (verified 2026-07-01):** soh3d's interpreter dispatches on the raw
opcode byte (`gfx_step`, `interpreter.cpp:5106`) and ships a **full native F3DEX2 handler
table** — `f3dex2Handlers` (`:4979`) covers `G_VTX`, `G_TRI1/2`, `G_QUAD`, `G_MTX`, `G_POPMTX`,
`G_DL`, `G_ENDDL`, `G_GEOMETRYMODE`, `G_TEXTURE`, `G_MOVEMEM`, `G_MOVEWORD`, `G_SETOTHERMODE_L/H`,
plus the RDP `rdpHandlers`. So a genuine MM F3DEX2 display list can be executed by soh3d's
interpreter **command-for-command** — we do **not** re-encode opcodes. The transcoder's real
job narrows to:
  1. Copy the command stream out of RDRAM into a host `F3DGfx[]` (each 32-bit word reads
     native via `RdramView`).
  2. Track the F3DEX2 segment table as the DL sets it (`gsSPSegment` = `G_MOVEWORD` /
     `G_MW_SEGMENT`) and resolve every **address-bearing operand** to a physical RDRAM offset.
  3. Materialize each referenced block (vertices `n*16`B, matrix `64`B, viewport/lights via
     `G_MOVEMEM`, sub-DL targets, texture images via `G_SETTIMG`) into an **un-swizzled** host
     buffer and rewrite the operand to that host pointer — even, so soh3d's `SegAddr` returns
     it verbatim. RT64's `rt64_rsp.cpp` (per-opcode operand sizes + `fromSegmentedMasked`) is
     the behavioral reference for which operands are addresses and how much each reads.

**Rejected alternative:** make soh3d's interpreter itself swizzle/endian-aware (touch every
struct read in `interpreter.cpp`). Rejected — it permanently forks libultraship's hot path on
a swizzle that only the MM side needs, is spread across dozens of handlers, and is far more
fragile than a single bounded transcoder. The transcoder keeps the seam narrow and the
renderer pristine.

This keeps the recomp's threading model (Gfx thread → `send_dl`) intact and reuses soh3d's
entire shader/combiner/backend. The CMB substitution for MM then rides the *same*
`OTR_G_SOH3D_DRAW` opcode path, emitted from MM-side `patches/` (the recomp analog of SoH's
`Actor_Draw` hook).

## Build / link shape of the unified app

- soh3d builds with **GCC 16** (ccache → gcc; `Shipwright/build-cmake/CMakeCache.txt`), not
  clang. The unified app targets **GCC**; the recompiled MM C is plain C and compiles under
  GCC. (Zelda64Recomp's BUILDING.md clang default is not required for us; clang/lld are
  dnf-available if a specific step ever needs them.)
- Renderer is already a reusable static lib (`libultraship`). The unified binary links:
  `libultraship` (renderer + RmlUi + SoH3D CMB host) + the MM game core
  (`RecompiledFuncs` + `PatchesLib` + `librecomp` + `ultramodern`) + SDL3 + glslang.
  RT64 and Zelda64Recomp's own RmlUi UI are dropped.
- The seam between them stays a **narrow C-ABI** (the `SoH3D_*`/`RendererContext` surface),
  no leaking of soh3d or recomp internals across it — see coding standards below.

## Milestone ladder

Each milestone is independently verifiable. Build the tooling to verify each; don't skip to
"it runs."

0. **Groundwork — DONE & verified (2026-07-01).** Topology settled + documented; both seams
   mapped; `.env` wired to the local ROM dir (`$ZELDA3D_ROM_DIR`); MM3D asset core re-verified on real data
   (CMB 1477/1477 ok, 0 bad geometry); soh3d confirmed built (`soh.elf`).
1. ✅ **MM core recompiled — DONE & automated (2026-07-01).** The static recompile (N64 ROM →
   C) is fully working *and* automated behind a lazy, idempotent script:
   `tools/ensure_mm_decomp.sh` (modeled on soh3d's `run.sh ensure_built`): builds the recompiler
   tools (GCC — no clang needed), builds z64decompress, decompresses the ROM, and runs
   `N64Recomp` + the two `RSPRecomp` passes → `RecompiledFuncs/` (339 C files, 17146 funcs) +
   `rsp/*.cpp`. Warm runs are a ~12 ms no-op, so the launcher/run.sh calls it every boot.
   - **No RT64.** We deliberately do **not** build the stock Zelda64Recomp app — it links RT64
     (Vulkan/dxc/D3D12 submodule chain) which the unified target discards. We build only the MM
     **game core** (`RecompiledFuncs` + `PatchesLib` + `librecomp` + `ultramodern`) and link it
     against soh3d's renderer. So rt64's nested submodules are never needed.
2. ✅ **MM3D asset access over the cmb3d core — DONE & verified (2026-07-01).** `src/mm`
   (`mm3d::Assets`): a clean, renderer-free service that resolves MM3D archives (ZAR/GAR, LzS)
   and decodes CMB models to `CmbDrawGroup`s (vertices byte-compatible with the renderer's
   `SoH3DGlVtx`). *Verified:* `tools/mm3d_model_probe` against the real MM3D ROM — 8/8 sampled
   archives decode to finite, well-formed geometry (all CMB v10); named lookup resolves
   MM3D Link. This is the foundation the milestone-5 `SoH3DModelProvider` plugs into.
3. **soh3d renderer accepts an RDRAM F3DEX2 DL.** Implement the unification point: the
   **swizzle-aware F3DEX2 transcoder** above (recomp RDRAM → host-native `F3DGfx`), then a
   `RendererContext` subclass whose `send_dl` transcodes the task DL and calls soh3d's
   `Interpreter::Run`. Build it bottom-up and verify each layer on real-ish data without the
   full game boot:
   - **3a.** Swizzle-aware `RdramView` accessors (`^2`/`^3`), unit-tested against a
     hand-authored swizzled buffer (renderer-free; the milestone-2 pattern). *Done when the
     accessors round-trip every width.*
   - **3b. ✅ DONE & verified (2026-07-01).** `src/n64dl/transcoder.{h,cpp}` — `n64dl::Transcoder`
     walks an F3DEX2 DL out of swizzled RDRAM and emits a host-native `n64dl::Gfx[]` stream
     (layout-compatible with soh3d's `F3DGfx`), command-for-command. It tracks the F3DEX2 segment
     table (`gsSPSegment` → `MOVEWORD`/`G_MW_SEGMENT`), resolves every address operand
     (RT64 `fromSegmented` mirror), materializes vertices (per-field, natural `F3DVtx` order),
     matrices (16 native words), and `G_MOVEMEM` viewport/lights into un-swizzled host blobs, and
     recurses sub-DLs (`G_DL`), rewriting each operand to the materialized host pointer. *Textures
     (`G_SETTIMG`) are a marked STOPGAP — pass-through until load-time sizing (3c+).* Verified by
     `tools/n64dl_transcode_test.cpp` (`ctest`): a hand-authored swizzled DL (segment set, MTX,
     VTX×3, TRI1, call into a sub-DL, TRI1, ENDDL) round-trips — opcodes preserved, operands
     rewritten to host pointers, materialized matrix/vertex values exact in host-LE order, sub-DL
     recursion + root continuation correct.
   - **3c. ✅ DONE & verified (2026-07-01).** `tools/n64dl_present.cpp` — a renderer harness
     that brings up libultraship headlessly with **no game boot** (the minimal
     `CreateUninitializedInstance` → `InitConfiguration` → `InitConsoleVariables` →
     `InitResourceManager({bogus}, {}, 1, /*allowEmptyPaths*/true)` → `InitWindow(Fast3dWindow)`
     sequence — no archive needed, SDL3-GPU shaders are glslang-compiled at runtime; ControlDeck/
     Console/Audio/SohGui skipped; four SoH game-side callbacks stubbed inert). It authors a
     colored-triangle **F3DEX2 DL as a swizzled RDRAM image**, runs it through `n64dl::Transcoder`,
     reinterpret_casts the emitted `n64dl::Gfx[]` to soh3d's `F3DGfx` (ABI `static_assert`ed —
     libultraship is built without `USE_GBI_TRACE`), and renders it via
     `Fast3dWindow::DrawAndRunGraphicsCommands`. The frame is dumped (PPM via the `gSoh3dDumpPending`
     hook) and the harness self-verifies the pixels: each vertex neighbourhood is dominated by its
     own primary — red `rgb(224,15,15)`, green `(16,224,15)`, blue `(15,15,225)` → **PASS**. This
     proves the transcoder on the real GPU end-to-end and de-risks the libultraship headless
     bring-up that milestone 4 needs. Build is opt-in (`ZELDA3D_SOH3D_DIR`); run under Xvfb
     (`xvfb-run … n64dl_present out.ppm`). *Known: screen-space Y is flipped vs the authoring
     coords (a viewport/framebuffer convention, not a defect); a one-shot `_exit` STOPGAP avoids a
     Context-teardown crash that the milestone-4 RendererContext will resolve.*
4. **Register the soh3d factory in place of RT64.** Swap `main.cpp:686`; drop `lib/rt64`.
   *Verify:* MM boots and plays on soh3d's SDL3-GPU renderer with **N64** models.
   Broken into sub-steps (see `docs/MM_MILESTONE4.md` for the full design + source map):
   - **4a.1. ✅ DONE (2026-07-01).** Carve the four MM core libs out of Zelda64Recomp's RT64
     app build. `src/mm_host/CMakeLists.txt` embeds only the self-contained N64ModernRuntime
     subdir + re-declares the generated-source libs; `ZELDA3D_BUILD_MM_HOST` opt-in. Verified:
     ultramodern, librecomp and the 339-file RecompiledFuncs build & archive under GCC 16 with
     zero RT64/SDL/renderer dependency.
   - **4a.2. TODO.** PatchesLib — needs the MIPS `patches.elf` chain (clang -target mips +
     ld.lld, dnf-installable; not yet installed). Extend `tools/ensure_mm_decomp.sh`.
   - **4b. ✅ DONE & verified (2026-07-01).** `src/mm_host/soh3d_render_context.{h,cpp}` — the
     `Soh3dRenderContext` that replaces `RT64Context`. `send_dl` = `RdramView` + `Transcoder` +
     libultraship draw, via the narrow `soh3d_backend` seam (isolates libultraship's headers
     from ultramodern's OSTask world). Lives on the gfx thread (matches libultraship's
     single-thread model). Verified by `tools/mm_render_context_test`: the context renders the
     transcoded triangle through the real `create_render_context → send_dl → update_screen`
     path — R/G/B vertices correct, PASS under Xvfb. `cmake/Soh3dRenderer.cmake` factors the
     libultraship link set into one `soh3d::renderer` target.
   - **4c. TODO.** zelda3d host `main` + callbacks: replay the recomp's renderer-agnostic
     registration (register_game(MM)/overlays/patches/config), install our renderer factory +
     SDL3-or-stub window/input/audio callbacks (NOT the recomp's SDL2 host — SDL2/SDL3 can't
     co-link), provision the ROM (`recomp::select_rom` + `start_game`, bypassing the RmlUi
     launcher). *Verify:* MM boots and renders its title/intro with N64 models on soh3d.
5. **MM3D substitution hook.** MM-side `patches/` analog of `SoH3D_TryDrawActor` /
   `SoH3D_EmitModelDraw`, emitting `OTR_G_SOH3D_DRAW`, backed by the milestone-2 provider +
   an MM object-id→MM3D-ZAR table. *Verify:* MM actors draw with **MM3D** models.
6. **Enhancements + intuitive controls.** Port the MM-relevant time-savers and the soh3d
   custom enhancements pattern (e.g. press-to-skip cutscene cameras); unify input on one
   control path. *Verify:* on real play.
7. **One launcher, one binary.** Fold the launcher (`src/render/`) onto soh3d's SDL3-GPU
   RmlUi path; both games selectable in-process. Retire the throwaway "hand off to each
   game's build" stopgap.

## Coding standards (this is a real PC game — keep it clean)

- Narrow, well-named seams. The MM↔renderer boundary is a small C-ABI; no soh3d or recomp
  internals leak across it. Renderer-neutral types on our side of the bridge.
- No giant files (global rule). Split by responsibility: `src/mm/` for the MM host glue,
  reuse `src/cmb3d/` for assets, soh3d's `libultraship` for the renderer.
- No magic constants / offsets, no stopgaps slipped in as fixes (global "no bandaids").
  Any genuine stopgap is marked `// STOPGAP:` with the proper fix named.
- Verify each milestone on real data before calling it done.
