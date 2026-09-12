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
