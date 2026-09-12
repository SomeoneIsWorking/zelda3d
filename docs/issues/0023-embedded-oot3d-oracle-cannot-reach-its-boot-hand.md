---
id: 23
title: Embedded OoT3D oracle cannot reach its boot handshake on this host
status: investigating
symptom: After a successful incremental build, tools/harness_cli.py boot-to-play does not emit 'boot succeeded' or create scratch/gameplay_settled.p45-00401070.state. Default Vulkan execution spins in HarnessVk::Readback from HarnessFrontend::SubmitOracleFrame; supported SOH3D_HARNESS_SW=1 execution instead stalls in the software renderer work queue before the handshake, including at ZELDA3D_HARNESS_RES_FACTOR=1 with SOH3D_HARNESS_HEADLESS=1.
state_items: S006
tags: oracle,harness,renderer,vulkan,software
created: 2026-09-12
updated: 2026-09-12
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-09-12)
Reviewed the 970-line dirty Azahar instrumentation diff. The modified GPU and software-rasterizer logging paths are guarded by unset SOH3D_HARNESS_LOG_* variables and by soh3d_draw_log_active=0, so they are inactive during the failed fresh boot. The startup stall cannot currently be attributed to that in-flight instrumentation. The default software rasterizer still constructs its worker pool from std::thread::hardware_concurrency(); LP_NUM_THREADS does not control it.

### Note (2026-09-12)
The first-party presentation gate is verified: after it moved capture enablement until after retro_load_game and Vulkan initialization, the default harness reached the REPL worker (post-handshake Readback stack), whereas the earlier stack was inside retro_load_game. A gated SOH3D_HARNESS_SW=1 session also reaches the REPL, but its first runtime frame saturates the SwRenderer workers and never reaches gameplay. The remaining runtime failure is distinct from the repaired pre-handshake readback deadlock.

### Note (2026-09-12)
Added explicit SOH3D_HARNESS_NO_CAPTURE=1 diagnostic mode in the first-party harness. It preserves the libretro fork and REPL/state driving while bypassing synchronous Vulkan readback; with capture disabled, cold boot reaches the REPL and completes run 300, but the existing title-input recipe still never reaches gameplay. A title-checkpoint load reports ok no. Default capture remains enabled, so screenshots still require resolving the HarnessVk::Readback fence stall. Fresh no-capture stderr also records repeated missing custom-texture replacement warnings; these are independent of the protocol reachability result.

### Note (2026-09-12)
The libretro Azahar dependency now has a maintained fork and immutable source declaration at
`tools/soh3d_harness/AZAHAR_SOURCE.toml` (`SomeoneIsWorking/azahar`, revision
`d488783b3708339a739dca5e10077cd21cb97226`). The fork carries the six harness RPC/oracle commits
and the applied diagnostic probes that were previously described as a re-applied patch stack.
The harness readback path now tracks the staging image layout and restores the core image layout
before returning. A Vulkan validation run over 30 frames still reports
`VUID-vkCmdDraw-None-09600` for an image whose current layout is `UNDEFINED` while a descriptor
expects `SHADER_READ_ONLY_OPTIMAL`; this is evidence of a remaining core-side layout contract bug,
not a reason to disable capture or add a timeout.
