---
id: 25
title: Host title screen crashed in the unified dual-texture pipeline; a sampler count that disagreed with the shader
status: resolved
symptom: Advancing the SoH3D host to the title-cutscene cursor that draws the wordmark faulted inside the Vulkan driver while creating that model's unified pipeline, and `tools/title_host_capture.py` reported only that the harness closed its stdout. The oracle side of the same comparison was reachable and cached, so the host-vs-oracle title A/B could not run at all.
state_items: S003
tags: renderer,sdl3gpu,unified,dual-texture,title,wordmark,parity-tooling
created: 2026-09-27
updated: 2026-09-27
---

## Root cause

**A sampler count that disagreed with the shader it described.**

`getUnifiedPipeline` computed the fragment stage's sampler count as `0` for the untextured variant,
`3` for `kGenericTev`, and `1` for *everything else* — so `kDualTex` and `kDualTexFog` were declared
as binding **one** sampler while their generated GLSL declares **two** (`set=2 binding=0 uTex0` plus
`set=2 binding=1 uTex1`, from the `@if(o_tex0)` / `@if(o_tex1)` sampler block in
`fast/backends/unified_shader.cpp`).

SDL3 builds a shader's bind-group layout from the count the caller passes to `SDL_CreateGPUShader`, not
from the SPIR-V. The pipeline handed to the driver therefore had a layout with fewer sampler bindings
than the shader read. That is not a validation error, which is exactly why
`ZELDA3D_SDL3GPU_DEBUG=1` reported nothing before the fault, and on a driver that trusts the supplied
layout the draw faults inside pipeline creation.

The crash narrowed onto that once the renderer named each pipeline it created:

```
unified pipeline create: variant=6(GenericTev)  samplers=3   <- five of these, all fine
unified pipeline create: variant=3(DualTex)     samplers=1   <- the one that faulted
```

That also **falsified the first hypothesis** recorded below — that a three-sampler (`kGenericTev`)
layout was at fault. The three-sampler variant had been created successfully five times immediately
before. The faulting variant is the *two*-sampler one, which was the only variant declared one sampler
short. The draw that reached it is the title wordmark, `/actor/zelda_mag.zar | g_title`
(`Model/g_title.cmb`, 1 group, 2 textures, 6 verts): the first dual-texture model on that frame.

## Fix

`Fast::Unified::VariantSamplerCount` derives the count from `FeaturesFor` — the same feature table the
generated GLSL comes from — so the declared count cannot drift from the generated bindings. Locked by:

* `UnifiedShader.DeclaredSamplerCountMatchesGeneratedBindings` — counts `uniform sampler2D` in the
  generated fragment source for **every** variant and requires equality. This is the drift-proof
  check: it fails if the two ever disagree again, from either side.
* `UnifiedShader.DualTextureVariantsDeclareTwoSamplers` — pins the specific counts (dual-tex 2,
  generic TEV 3, single-tex 1, untextured 0).

Verified end to end: `tools/title_host_capture.py 1093` now advances the host through the wordmark with
zero fatal signals, and the full `ctest` gate passes 501/501 with the 12 declared asset-dependent skips.

## Second gate: the host and the oracle captured at different sizes (two tooling defects, not a product decision)

With the crash gone the comparison failed on shape instead:

```
title_host_capture: capture size mismatch: oracle=(480, 800, 3), host=(480, 640, 3)
```

The first reading of that is a product aspect decision — the oracle is 800x480 (PICA 400x240 at
`citra_resolution_factor=2`) and the host window defaulted to 640x480 (N64 4:3). **That reading was
wrong, and the two defects below are what actually produced it.**

1. **The harness's own raster override was a no-op.** `title_host_capture.py` sets
   `ZELDA3D_HARNESS_SOH_W/H` and the harness writes a `shipofharkinian.json` carrying that size, with
   the comment "Pin the host capture to the same raster instead of comparing a scaled 800x480
   default". But `Ship::Context::GetAppDirectoryPath` resolves configuration to
   `$XDG_CONFIG_HOME/soh` (else `~/.config/soh`), **not the cwd** — the port moved settings to the OS
   user-data location, and the harness was never updated. So the file the harness wrote was read by
   nobody, `conf->GetInt("Window.Width", 640)` fell through to the default, and the override silently
   did nothing. The log said so plainly: `RmlUi initialised (640x480)`.
2. **The harness was rewriting the player's real config.** Because the read and the write both
   resolved to `~/.config/soh/shipofharkinian.json`, every run rewrote the user's own settings file
   (observed: the real config was replaced at shutdown with a two-key stub, losing everything else).
   A harness that clobbers user state is a worse defect than the missing override.

Both have one cause and one fix: point SoH at the directory the harness already isolates.
`soh_runtime.cpp` now `setenv("SHIP_HOME", sohCwd, 1)` in `PrepareWorkingDirectory`, the product's own
override for that directory. The `chdir` and the `~/.config/soh` warning were both premised on the
config living in the cwd, so the warning is gone rather than reworded. The `oot.o2r` / `soh.o2r`
archives that function already symlinks into `sohCwd` satisfy the archive lookup there, so isolation
costs no re-extraction.

3. **The tool then pinned the wrong size.** With `SHIP_HOME` honoured, the host captured 400x240 while
   the cached oracle frame is 800x480. The tool hardcoded `RES_FACTOR=1` / `400x240` on the comment
   "OracleCache stores native 400x240 title frames" — a claim the cache had stopped satisfying once a
   frame was warmed at factor 2. The cached frame's own size is the authority for a pixel comparison,
   so `run()` now reads it and pins the host raster from it, and there is no constant left to go stale.

### Result: the first host-vs-oracle title image comparison

`tools/title_host_capture.py 1093` (unified renderer, `title_cs=1093` → cached oracle `az=2016`):

```
content=0.4784  gold_px=2470/3544  gold_mean_r=225.5/65.7  union_rgb_mae=47.80
```

with `scratch/title_host_capture/title_cs1093_sxs.png`. What it shows, and what it does **not** show:

* **Structure matches.** The whole wordmark composition is present and correctly laid out: the
  `THE LEGEND OF ZELDA` lockup, the `OCARINA OF TIME 3D` sub-lockup, the shield, the sword, the
  `© 1998 - 2011 Nintendo / Codeveloped by GREZZO` credit. Geometry, scale and placement of the
  3DS-authored content agree.
* **The glow hue does not.** The oracle's sword flame and wordmark glow are orange; the host's are
  white/cyan. The `gold_mean_r` gap (225.5 vs 65.7) is that difference, not a brightness bug.
* **Attribution, not yet proven.** The unified draw path still installs a placeholder combiner
  (`kCombA` = cycle-0 `TEXEL0 * vColor0`, with the in-source comment "no real per-material TEV data
  exists on the CMB side yet"), and the wordmark is the first `kDualTex` draw that has ever rendered
  here at all — its pipeline used to fault. An additive glow drawn through a `texel * vertex colour`
  mux reads white. So the hue is *expected* from the placeholder and is not new evidence about the
  glow mechanism; the closed native-path cases in `docs/parity-map.md` are untouched by this. It is
  evidence that `render.multi-stage-tev` is now the binding constraint on this frame.
* **The framing differs, and the obvious reading of it is wrong.** The oracle has mounted Link small
  and distant at frame left; the host has him large and close at frame right. It is tempting to call
  that a camera divergence. It is not established as one, and the project already has a documented,
  already-diagnosed cause that produces exactly this symptom: frontier `title.rider-trajectory` records
  the host at **cs 10/s against the oracle's 30/s** — a cutscene-clock RATE desync that is
  user-owned and contested (card #149, commit `7b3e53eb` reverted the oracle-matched rate because it
  ran "too fast"). At `cs=1407` a cs-frame-locked A/B showed horse and rider co-located, which closed
  that one as benign. The camera may well be identical; a rider at a different point on its path
  with the camera tracking it looks like this.
  A five-cursor host-only sweep (`cs=1069, 1081, 1093, 1105, 1117`, advancing naturally and capturing
  at each, `scratch/title_cam_sweep/`) shows the horse staying large and near the right edge across
  that span, so the mismatch is **larger than a frame-domain residue** — but a rate desync is exactly
  that size, so the sweep does not separate the two.
  The discriminating step is therefore a **cs-frame-locked A/B at cs=1093** using the methodology that
  closed `cs=1407` (`scratch/title_ab/verify_04_cs1407.{az,soh}.png`), not a camera investigation. If
  the rider is co-located there, the camera was never wrong and the earlier open-camera rows stand. If
  it is not, then the desync is segment-dependent and the camera becomes a live suspect. **Do not tune
  the camera before that A/B** — `title.epona-gallop-rate` and the rate decision are the owning rows.

## What was tried / dead ends

* `SDL_GPU_DEVICE="AMD Radeon RX 6700 XT (RADV NAVI22)"` — no effect; still lavapipe.
* `VK_DRIVER_FILES` pinned to radeon — no device at all: `MESA: vulkan: No DRI3 support detected` then
  `SDL_CreateGPUDevice failed: No supported SDL_GPU backend found!`. Useful, not a dead end: it proves
  the software-driver fallback is environmental, and it means the AMD GPU still cannot serve as an
  independent check of this class of fault on this host.
* Resolving the faulting addresses — `libvulkan_lvp.so` and `libSDL3.so.0` are both stripped to a
  single dynamic entry point (`vk_icdGetInstanceProcAddr`, `SDL_DYNAPI_entry`), so `addr2line` cannot
  name the offending Vulkan entry point. Naming the pipeline in the renderer's own log is what made
  the fault actionable instead.
* No alternative renderer backend exists to cross-check: `Fast3dWindow::InitWindowManager` forces
  `FAST3D_SDL_GPU` and the other backends no longer exist.
* Building the fix at all: the existing build tree was configured against
  `/usr/lib64/libSDL3.so.0.4.14` while the installed SDL3 is `0.4.16`, so **any** relink failed with
  "missing and no known rule to make it". A plain `cmake .` in the build directory re-resolved SDL3 and
  the full 2015-target build then succeeded. Worth knowing: the build tree caches absolute soname
  paths, so an SDL3 package upgrade bricks it until it is reconfigured.
