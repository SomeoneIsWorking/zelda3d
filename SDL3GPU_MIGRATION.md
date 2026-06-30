# SDL3 GPU unified-renderer migration (branch `sdl3gpu`)

**Directive (user, 2026-06-25):** make the **SDL3 GPU API** the project's *only* renderer and
**remove everything else** — OpenGL, DirectX11, Metal, and the custom Vulkan backend. SDL3 GPU is
the foundation. (Memory: soh3d-renderer-sdl3gpu.)

This branch is an isolated worktree (`../soh3d-sdl3gpu`) off `main`, so it cannot collide with the
parallel Gohma port (#120) building on `main`. Build dir here: `Shipwright/build-cmake` (its own).

## Why SDL3 GPU (settled — do not re-litigate)
Low-level GPU abstraction (explicit pipelines/command buffers/SPIR-V) over Vulkan/Metal/D3D12 under
one cross-platform API. The existing **Vulkan backend already runtime-compiles the N64 color-combiner
GLSL → SPIR-V via glslang** — SDL3 GPU consumes SPIR-V, so we REUSE that path (no build-time shader
precompile needed) and port the Vulkan backend to SDL3 GPU calls. The Vulkan backend is the source
of truth to port from; GL/DX11/Metal get deleted, not ported.

## Architecture facts (from the renderer recon)
Abstract interfaces that STAY (add an SDL3 GPU impl, keep the vtable shape):
- `Shipwright/libultraship/include/fast/backends/gfx_rendering_api.h` — `GfxRenderingAPI`
- `Shipwright/libultraship/include/fast/backends/gfx_window_manager_api.h` — `GfxWindowBackend`
- `interpreter.cpp` color-combiner / `CCFeatures` — backend-agnostic, untouched.

Concrete backends (PORT vs REMOVE):
- `fast/backends/gfx_vulkan.cpp` (3080) + `gfx_vulkan.h` (358) — **PORT → `gfx_sdl3gpu`**.
- `fast/backends/gfx_opengl.cpp` (1120), `gfx_direct3d11.cpp` (1469), `gfx_direct3d_common.cpp`,
  `gfx_metal.cpp` (1298), `gfx_metal_shader.cpp` (285) — **REMOVE**.
- Window backends: `fast/backends/gfx_sdl2.cpp` (927) → **migrate to SDL3** (`gfx_sdl3.cpp`);
  `fast/backends/gfx_dxgi.cpp` (1144) — **REMOVE**.
- Backend select: `fast/Fast3dWindow.cpp` + `.h` (enum `WindowBackend`, `SOH3D_VULKAN` env) — collapse
  to a single SDL3 GPU path.
- SoH3D custom OoT3D draw: `fast/soh3d_vk.cpp` (2076), `fast/soh3d_hud_vk.cpp` (658) — **PORT**;
  `fast/soh3d_gl.cpp` (1716) — port its logic into the unified SDL3 GPU path then remove the GL one.
- Shader templates `fast/shaders/{opengl,directx,metal}/` — REMOVE; keep the runtime combiner→GLSL gen.

SDL surface (~40 files) — project is on **SDL2 2.32**, system has **SDL3 3.4.10 + SDL_gpu.h**:
- CMake: `soh/CMakeLists.txt:303-720` (`find_package(SDL2)`, `SDL2::SDL2`, `SDL2_net`).
- imgui `imgui_impl_sdl2` in `fast/Fast3dGui.cpp` → `imgui_impl_sdl3`.
- RmlUi `ship/window/gui/rml/RmlUi_Platform_SDL.*` → SDL3.
- audio `ship/audio/SDLAudioPlayer.cpp`; crash handler; `os.cpp`/`os_vi.cpp`.
- Controller/input subsystem `ship/controller/.../sdl/*` + `physicaldevice/*` — the bulk; SDL3 renamed
  game-controller→gamepad, event-loop returns bool, `SDL_GetKeyboardState`, joystick id types, etc.

## Phases (tree must BUILD + run headless at the end of each; old backends removed LAST)
- **P0** ✅ worktree + plan (this doc).
- **P1 — SDL2 → SDL3 project migration.** Swap CMake to SDL3; migrate every SDL2 API site (input,
  audio, RmlUi, imgui_impl_sdl3, crash, window). Keep the **GL** backend alive (SDL3 still does GL
  contexts) to verify the SDL swap independently. GATE: headless run (`SOH3D_HEADLESS=1
  tools/soh3d_game.sh`) boots + REPL responds + input mapping intact.
- **P2 — `gfx_sdl3gpu` GfxRenderingAPI backend.** Port `gfx_vulkan.cpp`: `SDL_GPUDevice`,
  `SDL_GPUGraphicsPipeline` (cache by id0/id1/state like the VK pipeline cache), `SDL_GPUCommandBuffer`,
  `SDL_GPURenderPass`, `SDL_GPUTexture`, `SDL_GPUBuffer`, `SDL_CreateGPUShader` from the **existing
  glslang SPIR-V** (reuse `BuildVkShaderSource`). New SDL3 GPU swapchain in the window backend. GATE:
  N64 Fast3D world renders at GL parity (A/B a known scene headless).
- **P3 ✅ — UNIFY N64 + 3DS into ONE pass through the single SoH3D renderer** (user directive 2026-06-26,
  memory soh3d-unified-renderer-one-pass). There must be NO N64-vs-3DS rendering split and NO separate
  SoH3D pass. The SDL3 GPU backend already records a DEFERRED op-list replayed once in `FinishRender`;
  make OoT3D CMB model draws, HUD, and RmlUi append as ops into that SAME stream, replayed in ONE render
  pass alongside N64 triangles. DELETE the `BeginSoH3DPass`/`BeginSoH3DOffscreen` interleaving handshake
  (don't port it). Port the content of `soh3d_vk.cpp` (model provider, per-model GPU buffer, bone/uniform
  ring, AO depth-prepass, sun-shadow pass, AO composite) + `soh3d_hud_vk.cpp` into op-emitting draws on
  the unified renderer; fold `soh3d_gl.cpp`'s logic in, then drop it. Add `RmlRenderInterfaceSdl3Gpu` +
  a `FAST3D_SDL_GPU` case in `Fast3dGui` so the menu draws as ops too. GATE: OoT3D models + HUD + RmlUi +
  shadows/AO all render in one pass, no separate-pass handshake.
- **P4 — REMOVE everything else.** Delete gfx_opengl/dx11/metal/dxgi + headers + shader templates +
  soh3d_gl.cpp; strip `ENABLE_OPENGL/DX11/VULKAN`, the `WindowBackend` enum down to one, `SOH3D_VULKAN`.
  Drop SDL2.
- **P5 — Verify full path** headless, capture evidence, commit + push `sdl3gpu`, then fast-forward `main`.

## Status log
- P0 done.
- **P1 done (SDL2 → SDL3 whole-project migration).** Tree builds clean and boots headless under SDL3
  on the GL backend; `soh.elf` links `libSDL3.so.0` only (no `libSDL2`). GATE PASSED: headless boot +
  REPL responds (`cmd "ainfo"` → valid reply) + GL renderer renders the Kokiri scene (Link+Saria, HUD;
  mean RGB ~140/140/104, 87% non-black). Evidence: `scratch/screenshots/p1_sdl3_boot.png`.
  - **CMake:** `find_package(SDL2)`→`SDL3 REQUIRED`, `SDL2::SDL2`→`SDL3::SDL3` in `soh/CMakeLists.txt`,
    `soh/charcompare/CMakeLists.txt`, `libultraship/cmake/dependencies/{common,linux}.cmake`
    (imgui backend `imgui_impl_sdl2`→`imgui_impl_sdl3`, `ImGui PUBLIC SDL3::SDL3`).
  - **Window/GL:** `gfx_sdl2.cpp` + `gfx_sdl.h` fully migrated (CreateWindow no x/y; `SDL_GetDisplayForWindow`/
    `SDL_DisplayID`; `SDL_GetDesktopDisplayMode`/`SDL_GetCurrentDisplayMode` return pointers + float
    `refresh_rate`; fullscreen via `SDL_SetWindowFullscreenMode`; `SDL_GetWindowSizeInPixels`; float mouse
    state; per-window relative-mouse; event enum/field renames; `SDL_GL_DestroyContext`). `Fast3dWindow.cpp`
    needed no SDL calls migrated (enums only). `gfx_opengl.h`/`soh3d_gl.cpp` includes → SDL3.
  - **imgui / RmlUi / audio:** `Fast3dGui.cpp` → `ImGui_ImplSDL3_*`; RmlUi shim driven down its SDL3 branch
    (`RMLUI_SDL_VERSION_MAJOR=3`); `SDLAudioPlayer.cpp` rewritten to SDL3 audio streams
    (`SDL_OpenAudioDeviceStream`/`SDL_PutAudioStreamData`/`SDL_ResumeAudioStreamDevice`).
  - **Input subsystem:** both controller trees (`ship/` + legacy `libultraship/`) migrated
    `SDL_GameController*`→`SDL_Gamepad*`, enum/event/field renames, `SDL_GetJoysticks` enumeration,
    property-based HasLED/HasRumble. Numeric button/axis enum values are stable so saved bindings still
    resolve. Not live-key-tested headless (no physical device), but the SDL3 event pump runs every frame
    without issue and keyboard-glyph input device is active.
  - **Misc:** CrashHandler/os/os_vi/Context/main.c/Extract.cpp (messagebox `buttonid`→`buttonID`) migrated;
    `FileDropMgr::SetDroppedFile` now takes `const char*` (SDL3 `SDL_DropEvent::data` is const).
  - **Deferred / stubbed (documented):**
    - **SDL_net / networking (Anchor co-op, CrowdControl, Sail):** there is no SDL3_net, and SDL2_net
      cannot link into an SDL3 binary (SDL2 & SDL3 share the `SDL_h_` master include guard, so pulling
      `<SDL2/SDL_net.h>` silently neutralized every later `<SDL3/SDL.h>` and broke the whole soh tree's
      view of `SDL_Gamepad`). Replaced the `<SDL2/SDL_net.h>` dependency with a self-contained no-op shim
      (`soh/Network/SDLNetShim.{h,cpp}`): networking compiles + links but is DISABLED at runtime. The
      SDL2_net CMake link was dropped. **TODO:** native/SDL3 networking transport to restore the feature.
    - **Vulkan backend:** kept compiled (ENABLE_VULKAN on) because other modules (`soh3d_gl.cpp`,
      `SohRmlUi.cpp`) reference Vk symbols unconditionally; only its 3 SDL surface calls were migrated
      (`SDL_Vulkan_GetInstanceExtensions` new signature, `SDL_Vulkan_CreateSurface` +allocator arg,
      `SDL_Vulkan_GetDrawableSize`→`SDL_GetWindowSizeInPixels`). GL remains the active runtime renderer.
      The full Vulkan→SDL3-GPU port is Phase 2.
    - **Mobile/Mac (`MobileImpl.cpp`, `macUtils.mm`):** migrated for cleanliness but not compiled on
      Linux, so not compile-verified here. Windows-only `<SDL_syswm.h>` (gone in SDL3) left under `#ifdef`.
- **P2 done (`gfx_sdl3gpu` GfxRenderingAPI backend — N64 Fast3D world at GL geometry parity).**
  Builds clean; boots headless under `SOH3D_SDL3GPU=1` on the SDL3 GPU backend.
  - **(a) Device + init OK.** Log: `gfx_sdl3gpu.cpp: SDL3 GPU backend: device driver = vulkan` +
    `SDL3 GPU backend initialized (P2: N64 Fast3D world)`. Headless box has no DRI3
    (`MESA: vulkan: No DRI3 support detected — required for presentation`), so there is no swapchain
    present; frames come from the **offscreen FB-0 readback** path, not present.
  - **(b) Scene geometry renders — GL parity (ignoring brightness).** Kokiri Forest (entrance 238,
    time 0x6000): Link (upright, shield, tunic), Navi, signposts, minimap, HUD item slots, hearts,
    rupee count, "Kokiri Forest" title all render with correct shapes/positions/textures, matching the
    GL backend. Quantitative: 640×480, **99.7% non-black**, mean RGB ~(187,187,140). Evidence:
    `scratch/screenshots/p2_sdl3gpu_kokiri.png` (SDL3 GPU) vs `p2_gl_match.png` (GL) — same scene,
    same geometry. The readback path (`ReadFramebufferToCPU`/`GetPixelDepth`/`WriteFbPpm`) is wired
    like gfx_vulkan: copy FB-0 color → `SDL_GPUTransferBuffer` via a copy pass
    (`SDL_DownloadFromGPUTexture`), fence-wait, map, write RGBA. Both the scripted `SOH_FRAMEDUMP`
    dump and the REPL `shot` on-demand dump produce frames.
  - **(c) RmlUi — NOT rendered on SDL3 GPU; graceful absence, no crash (deferred to P3).** With
    `SOH3D_RMLUI_OPEN=1 SOH3D_SDL3GPU=1` the scene renders normally but the menu never appears (no
    RmlUi log lines, no crash). Evidence: `scratch/screenshots/p2_rmlui.png` (plain scene). Root
    cause: `Fast3dGui::ImGuiBackendInit()` has no `FAST3D_SDL_GPU` case, so `mRml` is never created
    (only `FAST3D_SDL_OPENGL` → GL3 interface and `FAST3D_SDL_VULKAN` → `RmlRenderInterfaceVk` exist).
    Making it work is **P3-scale, not a small fix**, because: (1) it needs a new
    `RmlRenderInterfaceSdl3Gpu` (full port of the 1458-line `RmlRenderInterfaceVk` — compiled
    geometry, generated/font textures, scissor, stencil clip-mask, layer stack, opacity filter); and
    (2) the Vk interface records into a *live* command-buffer/render-pass obtained via
    `BeginSoH3DPass`, but the SDL3 GPU backend uses **deferred op-list recording** (`mOps` replayed
    only inside `FinishRender`), so `BeginSoH3DPass`/`BeginSoH3DOffscreen` are P2 stubs returning
    `false` — there IS no live pass mid-frame to record into. An SDL3 GPU RmlUi interface must either
    append menu geometry as ops to `mOps`, or the backend must add a post-replay overlay pass. This
    shares the SoH3D-pass infrastructure that P3 builds (alongside the `soh3d_vk` port), so it lands
    with P3.
  - **(d) KNOWN RESIDUAL (not addressed — user does not care):** the SDL3 GPU render is slightly
    dimmer/different gamma vs GL. Best hypothesis: sRGB/gamma handling — the color-target texture
    format (linear `R8G8B8A8_UNORM` vs an `_SRGB` target) and/or `SetSrgbMode` write-encoding differ
    from the GL path. One-line residual; do not iterate on it.
  - **(e) Stubbed for P3:** the SoH3D custom OoT3D draw paths — `soh3d_vk.cpp` (per-model GPU buffers,
    bone/uniform ring, AO depth-prepass, sun-shadow pass, AO composite) + `soh3d_hud_vk.cpp`, with
    `soh3d_gl.cpp`'s logic folded in then dropped. The backend's `BeginSoH3DPass`/`BeginSoH3DOffscreen`
    hooks + a live (or appended-op) recording model are the prerequisite. RmlUi-on-SDL3GPU (above)
    rides on the same infrastructure.
- **P3 DONE — UNIFY N64 + 3DS into ONE pass. All 4 milestones (M1 models, M2 HUD, M3 menu+clip-mask,
  M4 shadows+AO) land on the unified op model. NO separate-pass handshake:
  `BeginSoH3DPass`/`BeginSoH3DOffscreen` return false (legacy handshake deleted, not ported).**
  - **The unified op model (how `BeginSoH3DPass` is truly gone).** The SDL3 GPU backend's deferred
    op-list (`mOps`, replayed once in `FinishRender`) gained two external-op kinds + appenders
    (`gfx_sdl3gpu.{h,cpp}`): `AppendSoH3DInPass(fn)` / `AppendSoH3DInPassFb(fb, fn)` record a callback
    INSIDE a framebuffer's render pass (interleaves depth-correctly with N64 geometry, same
    color+depth target); `AppendSoH3DOwnPass(fn)` records a callback that runs its OWN render pass
    (offscreen targets), with the main fb pass ended first (SDL3 GPU passes don't nest). `ReplayOps`
    handles both: for `OP_EXT_IN_PASS` it ensures the target fb's pass is open then invokes the
    callback with `(cmd, pass)`; for `OP_EXT_OWN_PASS` it ends any open pass then invokes `(cmd)`.
    The SoH3D modules own their own GPU resources (created via `GpuDevice()`), resolve
    pipelines/textures/UBOs at record time, and capture them into the callback. There is no live
    command buffer handed out mid-frame — everything is an op in the one stream, replayed in one pass.
    Frame ordering (proven from `Fast3dWindow::DrawAndRunGraphicsCommands`): `Interpreter::Run`
    records N64 + OoT3D-model-renderpass ops, then `gui->EndDraw()` → `Gui::EndFrame` records HUD +
    RmlUi ops, then `Interpreter::EndFrame` → `FinishRender` replays them all into fb 0 (whose color
    is blitted to the swapchain / read back headless). On SDL3 GPU the present is ONLY a blit of fb 0,
    so HUD + menu must be ops into fb 0 (they are) — there is no separate swapchain ImGui composite.
  - **M1 ✅ OoT3D CMB models render in the unified pass.** New `soh3d_sdl3gpu.cpp` (`SoH3D_Sg_*`):
    model provider, per-model SDL_GPUBuffer, pipeline cache, per-draw UBO pushed via
    `SDL_Push{Vertex,Fragment}UniformData`. One in-pass op per model, replayed in fb 0's pass
    alongside the N64 triangles. Model shaders = `soh3d_vk.cpp`'s verbatim, retargeted to SDL3 GPU's
    SPIR-V set model (vertex UBO set 1, fragment samplers set 2, fragment UBO set 3). `soh3d_gl.cpp`
    dispatches to `SoH3D_Sg_*` when the SDL3 GPU backend is live. Verified Kokiri Forest e238 t0x6000:
    Link/Saria/grass/fences/buildings render; 98.3% non-black. `scratch/screenshots/p3_m1_kokiri.png`.
    Commit `5e422da`.
  - **M2 ✅ OoT3D PC HUD renders in the unified pass.** New `soh3d_hud_sdl3gpu.cpp` (`Fast::SgHud_*`):
    collects the frame's HUD quads (Begin..Draw..End), uploads to a ring vertex buffer, appends ONE
    op into fb 0. `soh3d_hud_vk.cpp`'s `SoH3D_Hud_*` C-ABI delegates to `SgHud_*` when SDL3 GPU is
    live; `SoH3D_Hud_Available()` true activates the PC HUD (N64 HUD draws gate off). New backend
    accessors `AppendSoH3DInPassFb` / `MainFbSize` / `FrameRecording`. Verified: hearts, item hotbar,
    rupee counter render over the scene. `scratch/screenshots/p3_m2_hud.png`. Commit `752fac8`.
  - **M3 ✅ RmlUi ESC menu renders in the unified pass.** New `RmlRenderInterfaceSdl3Gpu.{h,cpp}`:
    collects menu geometry during `Rml::Context::Render()` and appends ONE op into fb 0. `SohRmlUi`
    gains an sdl3gpu mode; `Fast3dGui` gains a `FAST3D_SDL_GPU` case in `ImGuiBackendInit` (creates
    `mRml`) and `RenderRmlMenu` (calls `UpdateAndRender`). Verified `SOH3D_RMLUI_OPEN=1`: styled menu
    (tabs, toggle rows, help text, rounded panel, radial glow) over HUD + scene.
    `scratch/screenshots/p3_m3_rmlui.png`. Commit `8a96953`. **Feature gap:** stencil clip-mask
    (`EnableClipMask`/`RenderToClipMask` are no-ops) + the layer stack/opacity filters
    (`PushLayer`/`CompositeLayers` render straight to fb 0) are NOT ported — see remaining-M3 below.
  - **M4 ✅ dynamic sun-shadows + screen-space AO render in the unified pass (commit `aac4830`).**
    Ported from `soh3d_vk.cpp` onto the op model in `soh3d_sdl3gpu.cpp`. The shadow map (sun POV)
    and AO depth (camera) render as `AppendSoH3DOwnPass` ops — each owns its own SDL3 GPU render pass
    into PRIVATE offscreen targets, appended BEFORE the visible model in-pass ops; the model frag
    PCF-samples the shadow map; the SSAO composite is a full-screen `AppendSoH3DInPass` op
    (multiply blend) AFTER the model draws. All replay in the SAME command buffer as the N64 + model
    ops (SDL3 GPU inserts the write→sample barriers automatically) — no separate-pass handshake. Per
    the P3-plan gotcha, depth is rendered into an **R32_FLOAT color target** (writing
    `gl_FragCoord.z`) and sampled as a plain `sampler2D .r` (avoiding D32-as-sampler pitfalls); a
    transient D32_FLOAT target backs each pass's z-test. 2048² shadow map; AO depth sized to fb 0.
    The dispatcher (`soh3d_gl.cpp` SDL3-GPU branch) now mirrors the Vulkan
    shadow→prepass→model→composite sequence. `SoH3D_Sg_BeginShadowPass` gates on
    `gSoH3dShadowEnable && gSoH3dShadowHasFocus` (so `shadow 0` actually disables it). Verified
    headless Kokiri e238 t0x6000: `shadow 1` casts a directional sun shadow beside Link/Saria;
    `ao 1` darkens contact/crease areas; both toggle off cleanly (shadow_only 126 vs off 172 mean
    RGB; ao_only 124 vs off 145). `scratch/screenshots/p3_m4_{shadow_ao,shadow_only,ao_only,off}.png`.
  - **M3 polish ✅ RmlUi stencil clip-mask (commit `08c2f18`).** Rounded `overflow:hidden` clips (the
    menu panel) now clip correctly, matching `RmlRenderInterfaceVk`. Because SDL3 GPU cannot clear
    the stencil mid-pass, the menu records its draws as an `AppendSoH3DOwnPass` op that LOADs fb 0's
    color and binds a PRIVATE D24S8/D32S8 depth-stencil target cleared to 0 at pass begin — so fb 0's
    own D32_FLOAT depth is untouched (`GetPixelDepth` still reads it as raw float, no regression).
    `EnableClipMask`/`RenderToClipMask` paint each region with a monotonically-INCREMENTING stencil
    ref via ALWAYS+REPLACE (no per-region clear); normal draws test EQUAL via a second pipeline
    variant. New backend accessor `MainFbColorTexture()`. Verified (`SOH3D_RMLUI_OPEN=1`):
    `RenderToClipMask` fires every menu frame (one ref=1 rounded-panel clip); the rounded corner
    clips child content to the arc; the menu matches the Vulkan reference (incl. the pre-existing
    top-pane layout overlap, which is present in the VK reference too — a menu RML quirk, NOT a clip
    defect). `scratch/screenshots/p3_m3_clipmask.png` vs `p3_m3_clipmask_vk_ref.png`.
    - **Layer stack / opacity filters: intentionally left no-op.** The curated menu RCSS
      (`soh3d_test.rcss`) deliberately uses NO `box-shadow` / `filter:opacity()` / element `opacity`
      (documented in-file: "the Vulkan render interface can't do [it] yet"), so the
      `PushLayer`/`CompositeLayers`/`CompileFilter` path is unexercised on BOTH backends. Porting the
      offscreen RGBA8 layer pool would be speculative, unverifiable code for a feature the menu avoids
      by design — deferred until a menu element actually needs it.
  - **P3 COMPLETE.** OoT3D models (M1) + HUD (M2) + RmlUi menu w/ clip-mask (M3) + shadows & AO (M4)
    all render in ONE unified pass on the SDL3 GPU backend, no N64-vs-3DS split, no separate-pass
    handshake (`BeginSoH3DPass`/`BeginSoH3DOffscreen` stay `false`). The only documented residual is
    the GL-vs-SDL3GPU brightness/gamma one-liner (P2 item (d), user does not care) + the unexercised
    RmlUi layer/opacity filters above. **Do NOT start P4 here** — the orchestrator sequences backend
    removal.
  - **P4 (separate phase) — delete GL/DX11/Metal/Vulkan backends + `soh3d_gl.cpp` + `soh3d_hud_vk.cpp`'s
    Vulkan body + `RmlRenderInterfaceVk` + shader templates; collapse `WindowBackend`; drop SDL2.
- **P4 DONE — SDL3 GPU is the project's ONLY renderer; everything else removed.** Builds clean; boots
  headless with SDL3 GPU as the **default** (no `SOH3D_SDL3GPU=1`) and renders the full unified scene.
  Commits `9208590` (removal + untangle) + `db5fa51` (ENABLE_* ifdef purge). ~**22.7k lines / 26 files**
  deleted.
  - **Deleted (files):** the GfxRenderingAPI backends `gfx_opengl.*`, `gfx_direct3d11.*`,
    `gfx_direct3d_common.*`, `gfx_dxgi.*`, `gfx_metal.*`, `gfx_metal_shader.*`, `gfx_vulkan.*`; the SoH3D
    Vulkan draw/HUD twins `soh3d_vk.*` + `soh3d_hud_vk.*`; the RmlUi Vulkan + GL3 interfaces
    `RmlRenderInterfaceVk.*`, `RmlUi_Renderer_GL3.*`, `RmlUi_Include_GL3.h`; the `{opengl,directx,metal}`
    shader templates. (`gfx_sdl3gpu.*` + `soh3d_sdl3gpu.*` + `soh3d_hud_sdl3gpu.*` +
    `RmlRenderInterfaceSdl3Gpu.*` are the live replacements.)
  - **Backend selection:** `Fast3dWindow` registers + selects ONLY `FAST3D_SDL_GPU` and no longer reads
    `SOH3D_VULKAN`/`SOH3D_SDL3GPU` (gating removed). The `WindowBackend` enum **values** are kept (the
    dead labels are referenced by leftover case-labels / `interpreter.cpp` / `SohMenu.cpp`, harmless),
    but only the SDL3 GPU one is registered/default/in `windowBackendsMap`. `Fast3dGui`/`SohRmlUi` route
    exclusively to the SDL3 GPU RmlUi interface; their GL/Vulkan/DX/Metal switch arms are gone.
  - **`soh3d_gl.cpp` — KEPT, not deleted (had to stay).** It is NOT a removable backend: it owns the
    `SoH3D_GL_*` model/pose/collection **host API** that the whole soh3d tree links against (anim, model,
    interpreter). The `SoH3D_Sg_*` SDL3 GPU module only replaced the *backend draw*, not this host. So the
    file was **stripped of its direct-OpenGL render body** (shaders, program, `drawOne`, GL shadow/AO FBOs,
    GL state save/restore, the GL `RenderPass` fallback — ~1040 lines) and de-gated from `#ifdef
    ENABLE_OPENGL`; it is now the backend-agnostic SoH3D draw host + the SDL3 GPU dispatch. The historical
    `_GL_` symbol prefix is retained to avoid churning ~30 call sites (it no longer implies OpenGL).
  - **Ported off the removed Vulkan backend onto SDL3 GPU:** the `SoH3D_Hud_*` C-ABI (now in
    `soh3d_hud_sdl3gpu.cpp`, delegating to `Fast::SgHud_*`) and the geomscan AABB bridge
    `SoH3D_GeomScanDump` (now in `soh3d_sdl3gpu.cpp` — per-model local AABB at upload + per-draw world
    AABB capture published at `BeginPass`, for the #115/#120 audit). Re-added the `gSoH3dStateCheck`
    symbol (was in the deleted GL body, still written by soh3d.c).
  - **Had to STAY (+ why):** **glslang** (the SDL3 GPU backend compiles the per-combiner GLSL → SPIR-V at
    runtime — it is a hard dep); the **ImGui OpenGL3 backend** (`imgui_impl_opengl3`, vendored by ImGui's
    own cmake) is still compiled but never called, so `libGLdispatch` is still pulled in transitively
    (harmless, not our code). The **Vulkan loader / MoltenVK** deps were dropped (no `find_package(Vulkan)`,
    no `Vulkan::Vulkan` link); `soh.elf` links **no `libvulkan`**. Apple/Metal `#ifdef __APPLE__` case
    stubs in `Fast3dGui`/`gfx_sdl2` were left in place but now reference removed types — **Apple is
    unsupported after P4** (not built/tested here); a future Apple bring-up rides on SDL3 GPU + MoltenVK.
  - **CMake:** dropped `ENABLE_OPENGL` / `ENABLE_VULKAN` / `ENABLE_DX11` defines + `find_package(Vulkan)` +
    the `Vulkan::Vulkan` link; `ENABLE_SDL3GPU` now requires only glslang (FATAL_ERROR if missing); removed
    the stale backend-source FILTER lists. **GATE:** `grep -rE
    'ENABLE_OPENGL|ENABLE_VULKAN|ENABLE_METAL|ENABLE_DX11|gfx_opengl|gfx_vulkan' libultraship/src` →
    only **2 historical comments** in `gfx_sdl3gpu.cpp` (naming the `gfx_vulkan.cpp` template it was ported
    from). Clean.
  - **Render proof (default backend, no `SOH3D_SDL3GPU`):** Kokiri Forest e238 t0x6000 — log
    `gfx_sdl3gpu.cpp: SDL3 GPU backend: device driver = vulkan` + `... backend initialized`, **0** GL
    backend. 640×480, 97.2% non-black, mean RGB (155,155,112): OoT3D Link + Saria + grass/fences/buildings
    + the PC HUD (hearts/magic/hotbar/rupees) + Link's contact shadow render;
    `scratch/screenshots/p4_sdl3gpu_only.png`. `shadow 1`/`ao 1` toggle on (`p4_sdl3gpu_shadow_ao.png`).
    `SOH3D_RMLUI_OPEN=1` → the styled RmlUi ESC menu (tabs, toggle rows, rounded clip-masked panel) over
    HUD + scene; `p4_sdl3gpu_rmlui.png`. All in ONE unified SDL3 GPU pass.
  - **NOT done here (left for the orchestrator):** P5 fast-forward to `main`. The documented GL-vs-SDL3GPU
    brightness/gamma one-liner residual is untouched, as instructed.

- **P5 DONE (2026-06-30) — merged to `main`; SDL3 GPU is the project's sole renderer.** `main` had advanced
  with the parallel Gohma work (#120 bone-cap, #123 draw-offset, boss_goma module), so the branches had
  diverged — a plain fast-forward was impossible. Resolution: merged `main` into `sdl3gpu` (commit
  `04fe82a`), then fast-forwarded `main` onto it. Conflicts: `soh3d_vk.cpp` (deleted here / modified on
  main) → kept deleted; `soh3d_gl.cpp` → kept the stripped host. **The #120 bone-cap fix's renderer half
  lived in the now-deleted `soh3d_vk.cpp`; re-applied in the sole renderer by making the SDL3 GPU shader's
  `uBones[N]`, the `SgUbo` struct, and the upload loops/clamp all derive from `SOH3D_GL_MAX_BONES` (=64) —
  a SINGLE source of truth, NO hardcoded 32/64, NO Gohma-specific code in the GPU layer (user directive).**
  Verified: builds clean in both worktrees, boots headless on SDL3 GPU as default, Kokiri renders, Gohma
  arena geomscan shows the skinned actor AABB tight (ext 227) — the #120 tube stays fixed.
- **RmlUi follow-up DONE (2026-06-30, `cdfbadf`) — full-window modal + HiDPI on the SDL3 GPU renderer.**
  Menu was px-authored, panel capped at 1088x768, density ratio never set → small island + no DPI scaling.
  Fixed: `ApplyDensityRatio()` syncs the context density ratio to `SDL_GetWindowDisplayScale`; sheet
  re-authored in `dp`; window max-size caps removed so the panel fills the body content box at any size.
  Verified headless (ratio 1 vs 2): the dp body inset scales 64→128px and the panel keeps filling.
