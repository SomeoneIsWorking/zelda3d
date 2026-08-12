---
id: 9
title: MM solo teardown corrupts the heap after the core returns -- "corrupted size vs. prev_size"
status: fixed
symptom: `zelda3d mm` boots Clock Town, quits cleanly, the core returns 0 to the launcher, and the process then aborts (exit 134) with "corrupted size vs. prev_size" during engine teardown. OoT is unaffected because DeinitOTR calls _exit(0) and never tears down.
tags: n3,heap,teardown,sdl3gpu
created: 2026-08-06
updated: 2026-08-12
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

- ~~**Double release in `~GfxRenderingAPISdl3Gpu`.** Step-traced under `ZELDA3D_SDL3GPU_DEBUG=1`
  (the trace is committed); every release loop completes, and the deferred-release path clears
  `mTextures[i]` at defer time (`t = TextureSDL3{}`), so no handle is released twice.~~
  **WRONG -- this WAS the bug.** See the fix below. The reasoning failed in a way worth naming: it
  checked the paths that release a handle the object *created*, and the duplicate was a handle the
  object had merely *borrowed*. A step trace proves each loop RAN; it cannot prove two loops did not
  touch the same pointer, and "every release loop completes" was read as if it could. Note also that
  the loops did complete, every time -- the abort was always somewhere else -- which is exactly how
  a step trace comes to certify the code it is standing in.
- **`RmlRenderInterfaceSdl3Gpu::Shutdown()` destroying the borrowed device.** It does not — it only
  releases its own objects and nulls `mDevice`.
- **Double `Rml::Shutdown()`.** Guarded by `sRmlLibraryInitialised`.

## Known-related, probably not the cause

Vulkan validation (enabled by `ZELDA3D_SDL3GPU_DEBUG=1`) reports leaked child objects at
`vkDestroyDevice`, e.g. `VkImage 0x6850000000685 has not been destroyed`. That is the deliberate
shortcut in `~GfxRenderingAPISdl3Gpu` — `mSoh3d`/`mHud` are reset with the comment "their GPU
resources are owned by the device and freed at SDL_DestroyGPUDevice below". It is a genuine leak
worth closing, but a leak does not corrupt the C heap.

**Measured 2026-08-12 — and the first measurement was wrong by 40x.** The count as printed is
**10**: 5 `VkImage`, 5 `VkBuffer`. That is not the leak, it is the validation layer's
`duplicate_message_limit`, which defaults to 10 *per VUID* and says so in a line easy to read past
("this will be the last time reporting it"). Lifted, the real figure is **409 objects**: 362
`VkImageView`, 41 `VkImage`, 3 `VkPipeline`, 2 `VkShaderModule`, 1 `VkBuffer`.

```sh
mkdir -p scratch/vklayer
printf 'khronos_validation.duplicate_message_limit = 0\n' > scratch/vklayer/vk_layer_settings.txt
VK_LAYER_SETTINGS_PATH=$PWD/scratch/vklayer/vk_layer_settings.txt ZELDA3D_SDL3GPU_DEBUG=1 \
    tools/zelda3d_sequence.sh mm
```

Owner: the `g_*` file statics in `fast/zelda3d_sdl3gpu.cpp` and `fast/zelda3d_hud_sdl3gpu.cpp`
(shaders, pipelines, samplers, per-model VBOs, the texture cache) — none of which is released.

**Deliberately NOT fixed here, and the reason matters.** The GPU device is created exactly once per
process, so at teardown these are leaked into a process that is exiting: the OS reclaims them and
nothing observable follows. The real hazard is not the leak, it is that those statics are
ENGINE-lifetime handles into a device that ~GfxRenderingAPISdl3Gpu destroys — the same shape as
[issue 0016](0016-a-game-core-is-not-re-runnable-run-scoped-state.md)'s run-scoped globals, one level
up. The day anything recreates the device (a renderer restart, a backend switch), every one of them
dangles. So this is a lifetime bug waiting on a second device, not a leak worth chasing today, and
whoever adds device recreation must fix it first.

## INTERMITTENT -- do not trust a single green run

After Dear ImGui was restored as a real library, `solo mm` exited 0 twice and 134 once, all three
runs with the same "corrupted size vs. prev_size". Restoring ImGui changed the heap layout; it did
not fix the corruption. Any future "this is fixed" claim needs several consecutive runs, not one.

## A hole in fix (1), found 2026-08-11 by the ASAN build

Fix (1) above -- "teardown ran at `__cxa_finalize`, fixed by calling `DestroyInstance()` explicitly
from the launcher" -- only covers cores that RETURN. MM's `RunExtract` calls `exit(0)` directly
(`BenPort.cpp:583`), so the launcher never regains control and `Context` is destroyed at
`__cxa_finalize` after all, where `~Context()` logs through an spdlog registry the exit handlers have
already freed. Reproduced 3/3 under ASAN; written up as
[issue 0017](0017-context-destructor-logs-through-a-freed-spdlog-r.md).

**That is a different path from this bug** (this one fires on a normal quit, in `Config::Save`), so
0017 is not a fix for 0009 -- but it does mean the stated invariant here is too weak. It is not "the
launcher destroys the Context"; it is "no path may leave the Context to static destruction".

## Next step

Build with `-fsanitize=address`. Nothing cheaper has located the writer: valgrind is not installed
on this machine, glibc malloc checking does not move the detection point, and the release binary is
`-O2 -DNDEBUG` with no line info (`addr2line` resolves to `??:?`, and the system SDL3 exports a
single symbol so `nm` is useless too). ASAN is the instrument that names the writing store.

**Correction 2026-08-12: the system SDL3 IS symbolizable, via debuginfod.** This note's claim that
`addr2line` resolves to `??:?` and `nm` is useless (SDL3 exports one symbol, `SDL_DYNAPI_entry`) is
true of the local files and was taken to mean the SDL frames could not be read at all. They can:

```sh
DEBUGINFOD_URLS=https://debuginfod.fedoraproject.org/ eu-addr2line -f -C -e /lib64/libSDL3.so.0 0x23a8b7
```

turns three anonymous `libSDL3.so.0+0x...` frames into `VULKAN_ReleaseWindow` -> `VULKAN_Wait` ->
`VULKAN_INTERNAL_PerformPendingDestroys`, which is what identified SDL's DEFERRED destroy queue and
therefore why the abort site had nothing to do with the offending release. Fedora ships debuginfod
enabled; it was available for this bug's entire history.

**Correction 2026-08-11: the sanitizer option now EXISTS** — this note used to say one had to be
added. `cmake -S . -B scratch/build-asan -G Ninja -DZELDA3D_SANITIZE=address` (see the block at the
top of the root `CMakeLists.txt`). Separate build dir; the machine's ~15 GB allows one build at a
time.

Strong prior on where to look, from [issue 0008](0008-second-game-core-sigsegvs-in-sdl-acquiregpucomma.md):
that bug was the same family — a destructor `free()`ing members no constructor had `calloc()`ed,
which handed glibc foreign pointers and corrupted the free lists. Look first for acquire/release
mismatches on the MM shutdown path (`BenGui::Destroy` → `DeinitOTR`), not for GPU state.

## Why this matters

It is a blocker to a core unwinding all the way to process exit, which is the premise of "one app,
both games".

**Correction 2026-08-11: the masking described below is GONE, and the gate now sees this bug.** This
paragraph used to read "the sequence gate cannot see it: OoT runs last and `_exit(0)`s before
teardown". That `_exit(0)` was removed in the one-binary consolidation, so every core now unwinds and
the process reaches teardown on every sequence. `oot,mm,oot` accordingly exits 134 *intermittently*
(observed 0 and 134 from identical binaries with identical per-core results), and `mm` alone still
reproduces it while `oot` alone exits 0. The old warning still holds in its general form: do not let
a green sequence run stand in for a clean teardown — here it means the sequence EXIT CODE is not a
gate, and the per-core "ran a game and returned 0" lines are. (The sequence gate has its own separate failure now; see
[issue 0010](0010-oot-after-mm-crashes-in-imgui-newframe-setcurren.md).)

**Correction 2026-08-12: the sequence exit code IS a gate again**, now that the bug it could not
survive is fixed -- `mm`, `mm,oot` and the switch test all exit 0 consistently. Keep the per-core
"ran a game" lines regardless: they are what caught a run where both cores returned 0 without either
playing (issue 0016 instance 9), which no exit code can see.


## FIXED 2026-08-12 -- one handle released twice, four different abort sites

`mDummySampler` is `GetOrCreateSampler(false, CLAMP, CLAMP)` -- a **borrowed** pointer to a sampler
that `mSamplerCache` owns. `~GfxRenderingAPISdl3Gpu` released the whole cache and then released
`mDummySampler` again. One handle, two releases, out of ~300 per run.

Why that produced a corruption with no fixed address: SDL3's Vulkan backend does not destroy a
released resource, it **queues** it (`VULKAN_INTERNAL_PerformPendingDestroys`). The duplicate sat in
the pending-destroy list until something flushed it, and the flush happened during
`SDL_ReleaseWindowFromGPUDevice` -> `VULKAN_Wait`. So every abort site this issue collected was a
victim: `Config::Save`'s nlohmann destroy, `SDL_DestroyGPUDevice` on llvmpipe, `SDL_DestroyWindow`'s
X11 reply buffer. The site moved when anything changed the heap layout, which is what "INTERMITTENT"
above really was -- not a race.

**Evidence.** Before: `tools/zelda3d_sequence.sh mm` aborted 3/3 (exit 134) once the config
use-after-free below was out of the way. After: **3/3 exit 0**, each run reaching Clock Town
(`posinfo scene=111`) and returning 0, with `released 299-343 handle(s) this process, 0 of them
released more than once`. `mm,oot` exits 0 (845 handles, 0 duplicates) and
`tools/zelda3d_switch_test.sh` passes all four assertions.

### What actually found it, and what did not

Three instruments, in the order they were tried:

1. **The abort backtrace** -- pointed at four different innocent frees. Useless by construction here.
2. **The step trace** (`ZELDA3D_SDL3GPU_DEBUG=1`) -- reported the destructor running to `done` and
   the abort landing *after* it, which is how this issue came to rule the destructor out. It was
   answering "did each loop run", never "did two loops release the same handle".
3. **A duplicate-release check at the release itself** -- named the handle, its type, and the tag it
   was first released under, in one run. It is committed (`NoteGpuRelease` in `gfx_sdl3gpu.cpp`) and
   prints its denominator every teardown, pass or fail, so "no duplicates" stays distinguishable
   from "nobody looked". It also skips the second release, so a future ownership mistake of this
   shape becomes a logged ERROR naming the offender instead of a corrupted heap and a false lead.

The generalisable point: SDL3's deferred destroy means a release-side ownership bug has **no**
symptom at the release site. Any check that watches the destroy has already lost the offender's
stack. Watch the release.

### The config use-after-free that was hiding underneath

Before this was reachable, ASAN reported a heap-use-after-free in
`Ben::HasDisplayOverlayModeInConfig` (`2ship/2s2h/config/ConfigUpdaters.cpp`), during `InitOTR`:

```cpp
const auto& cvars = conf->GetNestedJson()["CVars"];   // temporary destroyed at the semicolon
return cvars.contains("gDisplayOverlay") && ...;      // reads freed memory
```

`Config::GetNestedJson()` returns **by value**, and lifetime extension does not apply to a reference
bound to a *subobject* of a temporary. Fixed by holding the value, which also drops a second full
rebuild of the nested config. Every other caller in the tree already took it by value; this was the
only site. Worth stating that it was NOT this issue's corruption -- a read of freed memory does not
damage a chunk -- but it was in the same library as one of the victims, which is exactly the kind of
coincidence that costs a session if the two are not separated deliberately.


## The switch gate was certifying a process that never tore down

Found 2026-08-12 while adding the duplicate-release assertion to
`tools/zelda3d_switch_test.sh`: it reported UNKNOWN, because the renderer's accounting line was
absent -- the teardown had not run at all.

The gate ended by sending `quit` to whichever core FIFO existed and letting its EXIT trap `kill -9`
the process. But after the round trip the app sits at the CHOOSER, where `quit` is play-gated; every
green run's log ended `quit: no playstate (non-Play gamestate; play-gated command)`. So the app was
killed, not exited, on every run -- and a gate that kills the process cannot observe a teardown bug
of any kind, including this one.

It now quits through `launcher pick quit` (the chooser's own Quit action) and asserts the process
**exit code**, so the abort this issue is about would fail it. After four core runs in one process:
exit 0, `released 1801 handle(s) this process, 0 of them released more than once`.

Worth generalising: `kill -9` in a gate's cleanup is fine as a backstop, but when it is also the
normal path, everything after the last assertion is untested by construction.


## FIXED 2026-08-12 — the renderer's own GPU objects are now released before the device

The destructor said these objects "are owned by the device and freed at `SDL_DestroyGPUDevice`". The
validation layer disagreed, and it was right: `Fast::Zelda3DRenderer` holds the per-model textures and
vertex buffers, the pipeline caches, and the shader set as plain members, and nothing ever released
them. `mSoh3d.reset()` destroyed the C++ object and left every SDL/Vulkan handle behind.

`Zelda3DRenderer::releaseGpuResources(note)` now hands them all back, called from
`~GfxRenderingAPISdl3Gpu` while the device is still alive. It takes the backend's duplicate-release
accounting as a parameter (that function is file-static in the other translation unit), so a borrowed
pointer -- the shared dummy texture, say -- cannot be freed twice: `note()` returns false for anything
already released, and the gate asserts that count is zero.

### Measured in BOTH directions, same command, same scene

    pre-fix   968 x VUID-vkDestroyDevice-device-   (850 VkImageView, 96 VkImage, 48 VkShaderModule,
                                                    10 VkBuffer, 8 VkPipeline)
    post-fix    0 x VUID-vkDestroyDevice-device-

The pre-fix figure was obtained by stashing the change and rebuilding, rather than trusting the
earlier note -- which is also how the count is now known to be 968 rather than the 409 recorded
above; that older number came from a different sequence. The only VUIDs left in a post-fix run are 44
`VUID-VkShaderModuleCreateInfo-pCode-` shader-code warnings, unrelated to object lifetime.

Handle accounting corroborates it and shows nothing was freed twice:

    oot,oot     1024 -> 1142 handles released, 0 duplicates
    mm,oot,mm   1205 -> 1373 handles released, 0 duplicates
    switch_test 1829 -> 1948 handles released, 0 duplicates
