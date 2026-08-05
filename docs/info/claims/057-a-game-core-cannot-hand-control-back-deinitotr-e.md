---
id: C057
kind: claim
status: holds
created: 2026-08-05
tags: n3,teardown,launcher
depends: Shipwright/soh/soh/OTRGlobals.cpp, Shipwright/libultraship/src/libultraship/bridge/windowbridge.cpp
---

## Claim

A game core cannot hand control back: DeinitOTR ends in a deliberate _exit(0), so libultraship has NEVER been torn down and in-process game switching needs the exact teardown that was removed for crashing

## Evidence

Built Context::RequestExit (honoured in WindowIsRunning, the one seam both games' graph loops share) plus a REPL 'quit', then ran 'zelda3d oot' under the launcher and sent quit. The launcher prints a line placed where ONLY a returning run() could reach it ('core returned N -- control is back in the launcher'); that line is absent from the log while the process exits 0. The exit code alone does not discriminate -- a core calling exit() internally yields 0 too -- which is why the line exists and why an earlier 'LAUNCHER_EXIT_CODE=0' would have been misread as success. Cause located in soh/OTRGlobals.cpp DeinitOTR: it stops threads, saves window layout + config, then calls _exit(0), and its comment states it deliberately skips the GUI/renderer/window destructors because they crash in code we do not own (RADV/Wayland wsi_wl_swapchain_destroy double-free, lavapipe/X11 xcb_present buffer overflow, RmlUi static StyleSheetFactory double-free). Its closing rationale -- 'object-graph teardown only matters for swapchain RECREATE (resize), never for shutdown' -- is true only while the process always dies, and in-process game switching is the case that falsifies it. RequestExit is still an improvement over the prior bare exit(0) in Zelda3D_LauncherExit, which skipped the config save; it buys an ORDERLY shutdown, not a graceful one.

## What would falsify it

If someone runs the GUI/renderer/window destructors on current drivers and they DO NOT crash, the premise for _exit(0) is gone and a normal unwind becomes possible. The cited crashes are from the Vulkan era and this project has since moved to SDL3 GPU as its only backend -- so they should be RE-TESTED rather than assumed to still hold. Equally, a launcher-owned window/renderer with per-game archives+heaps would sidestep teardown entirely, making the whole question moot.
