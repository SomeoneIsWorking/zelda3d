---
id: 25
title: Host title screen crashes in the software Vulkan driver creating the title wordmark's unified pipeline
status: investigating
symptom: Advancing the SoH3D host to the title-cutscene cursor that draws the wordmark faults inside the Vulkan driver, while `tools/title_host_capture.py` reports only that the harness closed its stdout. The oracle side of the same comparison is reachable and cached, so the host-vs-oracle title A/B cannot run at all.
state_items: S003
tags: renderer,vulkan,lavapipe,unified,title,wordmark,parity-tooling
created: 2026-09-27
updated: 2026-09-27
---

## Root cause

Not yet isolated to a cause, and the boundary that matters is measured: it is the **driver**, not
verifiably our descriptor. The evidence:

* **Deterministic.** Two runs of `tools/title_host_capture.py 1093` fault at the same frame with the
  same frames, and a third with `ZELDA3D_SDL3GPU_DEBUG=1` faults identically.
* **Located to one draw.** The log's last renderer line is
  `unified-uploaded model 2016: 6 verts` — model 2016 is `/actor/zelda_mag.zar | g_title`
  (`Model/g_title.cmb`, 1 group, 2 textures, 6 verts), the title wordmark. Every earlier model of the
  same frame uploads and draws, so earlier unified pipelines are created without incident.
* **Inside pipeline creation.** The backtrace is
  `RunFrame -> Graph_ProcessGfxCommands -> Zelda3D_RunGraphicsCommands -> DrawAndRunGraphicsCommands
  -> Interpreter::Run -> GfxStep -> gfx_zelda3d_draw_handler_custom -> Zelda3D_GL_Submit ->
  Zelda3D_Sg_DrawModel -> Fast::Zelda3DRenderer::DrawModel -> getUnifiedPipeline+0x79B -> SDL3 ->
  libvulkan_lvp.so`. The fault is in the driver, inside the call that creates the pipeline.
* **Validation is silent.** With `ZELDA3D_SDL3GPU_DEBUG=1` (SDL3 GPU debug/validation) nothing is
  reported about the pipeline before the fault, so the create info is not obviously invalid by
  validation rules.
* **The driver is lavapipe, and it is here for a good reason.** SDL3 picks the device
  (`SDL_CreateGPUDevice(..., nullptr)`). Forcing the real AMD device instead fails earlier and
  honestly: `VK_DRIVER_FILES=.../radeon_icd.x86_64.json` gives
  `MESA: vulkan: No DRI3 support detected - required for presentation` and then
  `SDL_CreateGPUDevice failed: No supported SDL_GPU backend found!`. Under this Xvfb the AMD GPU
  cannot present, so SDL falls back to lavapipe. **This is why the real GPU cannot be used as a
  discriminator on this host.**

## What is blocked

`tools/title_host_capture.py` cannot produce a host title frame, so there is no host-vs-oracle title
image comparison on this host. That is the evidence path for the renderer families whose remaining gap
is "port is close-tested on retail data but no oracle comparison was made"
(`render.cmb-unlit-primary`, `render.cmb-lit-primary-alpha`, `render.cmb-fragment-lighting`), so this
one crash gates their closure.

## What was tried / dead ends

* `SDL_GPU_DEVICE="AMD Radeon RX 6700 XT (RADV NAVI22)"` — no effect; still lavapipe.
* `VK_DRIVER_FILES` pinned to radeon — no device at all (no DRI3 under Xvfb). This is the
  informative failure: it proves the fallback is environmental, not a misconfiguration.
* Resolving the faulting addresses: `libvulkan_lvp.so` and `libSDL3.so.0` are both stripped to a
  single dynamic entry point (`vk_icdGetInstanceProcAddr`, `SDL_DYNAPI_entry`), so `addr2line` cannot
  name the offending Vulkan entry point. Only the frame inside `getUnifiedPipeline` is resolvable.
* No alternative renderer backend exists to cross-check against: `Fast3dWindow::InitWindowManager`
  forces `FAST3D_SDL_GPU` and states the other backends no longer exist.

## Resolution

### Tooling fixed (2026-09-27): the parity path no longer hides a product crash

`harness_transport` reported a crashed harness as `harness closed stdout unexpectedly` — a transport
message that makes a reproducible product crash look like a flaky tool. `_log_diagnosis()` now reads
the harness's own stderr log (`HARNESS_STDERR`, tail-only) and the raised error carries the fatal
signal and its top frames. The same run now reports:

```
title_host_capture: harness closed stdout unexpectedly; harness logged a fatal signal;
log .../scratch/logs/title_host_capture.log; top frames: .../libvulkan_lvp.so(+0xd2101) | ...
```

Pinned by `tools/test_harness_death_diagnosis.py`, including the negative control that a log with no
fatal-signal marker still points at the log rather than inventing a diagnosis.

### Tooling fixed (2026-09-27): oracle frame warming wrote to a key the comparison cannot read

`tools/oracle_cache.py warm` did not apply the vanilla (texture-pack-off) context that
`tools/title_host_capture.py` compares against, and the texture pack is part of the OracleCache key.
Warming frame 2016 wrote to `..._p45-00401070_tp2149-e714eb17be1b` while the A/B reads
`..._p45-00401070_tpoff`, so the title A/B was unreachable without hand-holding. `cmd_warm` now
applies `configure_vanilla_title_context`; the same command caches the frame where the comparison
looks for it, and the oracle half of the A/B now reports a cache hit.

### Next step, named

Settle driver-versus-descriptor, then either fix the descriptor or record a driver limitation:

1. Run the same host capture on a display path that gives the AMD GPU a real surface (a Wayland
   session, a KMS/DRM run, or an X server with DRI3). If the crash disappears, it is a lavapipe
   limitation and the product needs no change; the correct follow-up is to record the driver
   requirement for the headless parity tooling rather than to alter the renderer.
2. If it reproduces on the real GPU, bisect `getUnifiedPipeline`'s create info for the wordmark's
   dual-texture material — `num_samplers == 3` (the `kGenericTev` variant) against the
   `kUntextured`/`kPlainTev` variants is the obvious axis, since `g_title.cmb` is the first model on
   this frame that carries a second texture.
