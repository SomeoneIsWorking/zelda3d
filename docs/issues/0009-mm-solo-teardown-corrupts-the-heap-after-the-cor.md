---
id: 9
title: MM solo teardown corrupts the heap after the core returns -- "corrupted size vs. prev_size"
status: open
symptom: `zelda3d mm` boots Clock Town, quits cleanly, the core returns 0 to the launcher, and the process then aborts (exit 134) with "corrupted size vs. prev_size" during engine teardown. OoT is unaffected because DeinitOTR calls _exit(0) and never tears down.
tags: n3,heap,teardown,sdl3gpu
created: 2026-08-06
updated: 2026-08-06
---

## What is already fixed (commit 88db228e) and is NOT this bug

Two separate teardown defects were found and fixed while chasing this one. Both were real, both are
gone, and neither was the corruption:

1. **Teardown ran at `__cxa_finalize`.** The launcher left `Context`'s static `unique_ptr` to static
   destruction, so `Rml::Shutdown()` ran while RmlUi's own statics were being finalised — aborting in
   `Rml::StyleSheetFactory`'s destructor. Fixed by calling `Ship::Context::DestroyInstance()`
   explicitly from the launcher once the core returns.
2. **The GPU device was destroyed after its driver was unloaded.** `Interpreter::Destroy()` called
   `mWapi->Destroy()`, which ends in `SDL_Quit()` — and `Fast3dWindow` ran that *before*
   `delete mRenderingApi`. `SDL_DestroyGPUDevice` then called through a dangling pointer inside
   `VULKAN_DestroyDevice`. Fixed by giving `Fast3dWindow` the teardown order: render API first (it
   needs a live window for `SDL_ReleaseWindowFromGPUDevice`), window backend second.

After both fixes, `~GfxRenderingAPISdl3Gpu` reaches its final `done` step on hardware.

## The remaining bug

The abort is detected inside `Ship::GameSession::End()` → `Ship::Config::Save()` →
`nlohmann::json_value::destroy`. **That is the victim, not the culprit.** glibc reports
"corrupted size vs. prev_size" at the first `free()` that touches a damaged chunk, so the write that
damaged it happened earlier.

Evidence that the detection point is arbitrary rather than meaningful:

- On **llvmpipe** the same corruption is detected *earlier*, inside `SDL_DestroyGPUDevice`
  (`libvulkan_lvp.so`), before `Config::Save` is ever reached.
- On **radv** it gets past device destroy and is detected in `Config::Save`.
- `GLIBC_TUNABLES=glibc.malloc.check=3` does **not** move the detection point, so the damaged chunk
  is not being caught any sooner by chunk validation.

An earlier version of this investigation concluded "lavapipe bug on device destroy". That was wrong
— it is one corruption with two different victims, and the hardware run disproves the driver theory.

## Ruled out

- **Double release in `~GfxRenderingAPISdl3Gpu`.** Step-traced under `ZELDA3D_SDL3GPU_DEBUG=1`
  (the trace is committed); every release loop completes, and the deferred-release path clears
  `mTextures[i]` at defer time (`t = TextureSDL3{}`), so no handle is released twice.
- **`RmlRenderInterfaceSdl3Gpu::Shutdown()` destroying the borrowed device.** It does not — it only
  releases its own objects and nulls `mDevice`.
- **Double `Rml::Shutdown()`.** Guarded by `sRmlLibraryInitialised`.

## Known-related, probably not the cause

Vulkan validation (enabled by `ZELDA3D_SDL3GPU_DEBUG=1`) reports leaked child objects at
`vkDestroyDevice`, e.g. `VkImage 0x6850000000685 has not been destroyed`. That is the deliberate
shortcut in `~GfxRenderingAPISdl3Gpu` — `mSoh3d`/`mHud` are reset with the comment "their GPU
resources are owned by the device and freed at SDL_DestroyGPUDevice below". It is a genuine leak
worth closing, but a leak does not corrupt the C heap.

## INTERMITTENT -- do not trust a single green run

After Dear ImGui was restored as a real library, `solo mm` exited 0 twice and 134 once, all three
runs with the same "corrupted size vs. prev_size". Restoring ImGui changed the heap layout; it did
not fix the corruption. Any future "this is fixed" claim needs several consecutive runs, not one.

## Next step

Build with `-fsanitize=address`. Nothing cheaper has located the writer: valgrind is not installed
on this machine, glibc malloc checking does not move the detection point, and the release binary is
`-O2 -DNDEBUG` with no line info (`addr2line` resolves to `??:?`, and the system SDL3 exports a
single symbol so `nm` is useless too). ASAN is the instrument that names the writing store; the
build system currently has no sanitizer option, so one has to be added (a separate build dir — the
machine's ~15 GB RAM allows only one build at a time, `-j4`).

Strong prior on where to look, from [issue 0008](0008-second-game-core-sigsegvs-in-sdl-acquiregpucomma.md):
that bug was the same family — a destructor `free()`ing members no constructor had `calloc()`ed,
which handed glibc foreign pointers and corrupted the free lists. Look first for acquire/release
mismatches on the MM shutdown path (`BenGui::Destroy` → `DeinitOTR`), not for GPU state.

## Why this matters

It is a blocker to a core unwinding all the way to process exit, which is the premise of "one app,
both games". Note that the sequence gate cannot see it: OoT runs last and `_exit(0)`s before
teardown, so `tools/zelda3d_sequence.sh mm,oot` never exercises this path at all. That masking is
exactly why the bug survived this long — do not let a green sequence run stand in for a clean
teardown. (The sequence gate has its own separate failure now; see
[issue 0010](0010-oot-after-mm-crashes-in-imgui-newframe-setcurren.md).)
