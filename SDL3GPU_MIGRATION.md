# SDL3 GPU unified-renderer migration (branch `sdl3gpu`)

**Directive (user, 2026-06-25):** make the **SDL3 GPU API** the project's *only* renderer and
**remove everything else** — OpenGL, DirectX11, Metal, the custom Vulkan backend, AND the raylib
window backend. raylib was "a bad call"; SDL3 GPU is the foundation. (Memory: soh3d-renderer-sdl3gpu.)

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
  Remove the raylib branch/worktree (`git worktree remove ../soh3d-raylib`; delete branch). Drop SDL2.
- **P5 — Verify full path** headless, capture evidence, commit + push `sdl3gpu`, then fast-forward `main`.

## Status log
- P0 done.
