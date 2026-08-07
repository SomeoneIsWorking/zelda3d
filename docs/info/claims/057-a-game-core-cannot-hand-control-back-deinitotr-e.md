---
id: C057
kind: claim
status: falsified
created: 2026-08-05
tags: n3,teardown,launcher
depends: 2ship/2s2h/BenPort.cpp
reconfirmed: 2026-08-05
verified_at: 2026-08-05
falsified_on: 2026-08-07
---

## Claim

A game core cannot hand control back: DeinitOTR ends in a deliberate _exit(0), so libultraship has NEVER been torn down and in-process game switching needs the exact teardown that was removed for crashing

## Evidence

Built Context::RequestExit (honoured in WindowIsRunning, the one seam both games' graph loops share) plus a REPL 'quit', then ran 'zelda3d oot' under the launcher and sent quit. The launcher prints a line placed where ONLY a returning run() could reach it ('core returned N -- control is back in the launcher'); that line is absent from the log while the process exits 0. The exit code alone does not discriminate -- a core calling exit() internally yields 0 too -- which is why the line exists and why an earlier 'LAUNCHER_EXIT_CODE=0' would have been misread as success. Cause located in soh/OTRGlobals.cpp DeinitOTR: it stops threads, saves window layout + config, then calls _exit(0), and its comment states it deliberately skips the GUI/renderer/window destructors because they crash in code we do not own (RADV/Wayland wsi_wl_swapchain_destroy double-free, lavapipe/X11 xcb_present buffer overflow, RmlUi static StyleSheetFactory double-free). Its closing rationale -- 'object-graph teardown only matters for swapchain RECREATE (resize), never for shutdown' -- is true only while the process always dies, and in-process game switching is the case that falsifies it. RequestExit is still an improvement over the prior bare exit(0) in Zelda3D_LauncherExit, which skipped the config save; it buys an ORDERLY shutdown, not a graceful one.

## What would falsify it

If someone runs the GUI/renderer/window destructors on current drivers and they DO NOT crash, the premise for _exit(0) is gone and a normal unwind becomes possible. The cited crashes are from the Vulkan era and this project has since moved to SDL3 GPU as its only backend -- so they should be RE-TESTED rather than assumed to still hold. Equally, a launcher-owned window/renderer with per-game archives+heaps would sidestep teardown entirely, making the whole question moot.

## Re-confirmed 2026-08-05

Falsifier RUN, and it did NOT fire -- the teardown still crashes on the CURRENT SDL3 GPU backend, so the _exit(0) rationale is a live measurement rather than an inherited Vulkan-era belief. Built Context::RequestExitWithFullTeardown + REPL 'quitteardown', which makes DeinitOTR call Ship::Context::DestroyInstance() instead of _exit(0). gdb backtrace: main -> Zelda3D_CoreRun -> DeinitOTR -> Context::DestroyInstance -> ~Context -> ~Fast3dWindow -> ~GfxRenderingAPISdl3Gpu -> SDL3 VULKAN_DestroyDevice -> SIGSEGV, with frame #0 at ?? (a freed or corrupted pointer). SDL3 GPU's Vulkan backend reproduces the same class of fault the old Vulkan path did. ALONG THE WAY, a genuine ownership inversion was found and fixed, and it is what made the first run misleading: soh/OTRGlobals.cpp held the window in a process-lifetime global std::shared_ptr<Fast::Fast3dWindow> sohFast3dWindow, so ~Context dropped only the ENGINE's reference and the window survived it; the first run therefore printed 'Context destroyed WITHOUT crashing' AND 'core returned 0' AND still died 139, because the core's static destructor destroyed the GPU device at exit() (backtrace frame #13 std::shared_ptr<Fast3dWindow>::~shared_ptr from libsoh_core.so, under __run_exit_handlers). Made non-owning (raw observer; Context is sole owner), after which the crash relocates INTO DestroyInstance where it honestly belongs. That fix is correct on its own terms -- a game core must never own an engine object, since under the launcher the engine outlives every core -- but it does NOT avoid the crash.

## Re-confirmed 2026-08-05

MM measured too, and it splits the question in a way OoT alone could not. MM's core DOES hand control back -- 'ZELDA3D LAUNCHER: mm core returned 0 -- control is back in the launcher' -- because MM has no _exit(0) in its shutdown, unlike OoT. So the HANDOFF MECHANISM IS SOUND and is not what blocks in-process switching; only the engine teardown is. The process still dies: MM_EXIT=134 (SIGABRT), after the launcher regained control. gdb: std::unique_ptr<Ship::Context>::~unique_ptr (the STATIC mContext, at process exit) -> ~Context -> ~Fast3dWindow -> Ship::Gui::ShutDownImGui -> Fast3dGui::ImGuiBackendShutdown -> ~SohRmlUi -> Rml::Shutdown -> StyleSheetFactory::Shutdown -> ~StyleSheetFactory -> heap double-free abort. That is the RmlUi StyleSheetFactory double-free named as the THIRD crash in DeinitOTR's own comment, so TWO of its three cited crashes are now confirmed live on current code: the SDL3 GPU VULKAN_DestroyDevice SIGSEGV (OoT) and this RmlUi abort (MM). Both occur inside ~Fast3dWindow, at different sub-steps -- MM aborts in the ImGui/RmlUi shutdown BEFORE reaching the GPU device destroy that kills OoT. Note this also means MM's ordinary quit has ALWAYS aborted at process exit; it went unseen because every prior MM run was killed with -9 rather than allowed to exit. Reinforces the design conclusion: since both faults live exclusively in ~Context, an engine that is NEVER torn down avoids both.

## FALSIFIED 2026-08-07

The first half is now false BY CONSTRUCTION: DeinitOTR's _exit(0) and graph.c's DeinitOTR()+exit(0) tail were both DELETED on 2026-08-07, and OoT's core now returns to the launcher (evidence: 'oot core returned 0 -- control is back in the launcher', from tools/zelda3d_sequence.sh oot,mm, a direction that could not run at all before). The second half -- 'in-process switching needs the exact teardown that was removed for crashing' -- was the actual error, and it is what the work disproves: switching needs NO teardown. The engine is never destroyed between games; BeginGameSession replaces only the per-game half while window/renderer/crash-handler stay up, which is the design C057's own re-confirmations kept pointing at. What REMAINS TRUE and is NOT falsified: ~Context still crashes on current drivers (SDL3 GPU VULKAN_DestroyDevice SIGSEGV for OoT, RmlUi StyleSheetFactory abort for MM), so Context::DestroyInstance stays the launcher's to run once at process exit and must not be run between games. Superseded by C078.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
