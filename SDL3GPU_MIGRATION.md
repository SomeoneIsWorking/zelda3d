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
- **P3 — Port SoH3D OoT3D model paths.** `soh3d_vk.cpp` (model provider, per-model VkBuffer→GPU buffer,
  bone/uniform ring, AO depth-prepass, sun-shadow pass, AO composite) + `soh3d_hud_vk.cpp` → SDL3 GPU.
  Fold `soh3d_gl.cpp`'s logic in, then drop it. GATE: OoT3D models + HUD + shadows/AO render.
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
