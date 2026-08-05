---
id: 8
title: Second game core SIGSEGVs in SDL_AcquireGPUCommandBuffer -- but the GPU is fine, the C heap is corrupt
status: resolved
symptom: Running two game cores back to back in one process (zelda3d --run-sequence mm,oot), the second core dies at its first GUI texture upload: UploadTexture -> SDL_AcquireGPUCommandBuffer -> VULKAN_AcquireCommandBuffer -> vk_common_ResetCommandBuffer (lavapipe).
tags: n3,heap,imgui,launcher
created: 2026-08-06
updated: 2026-08-06
---

## Root cause

`bad027cd` ("remove Dear ImGui library; replace with no-op header shim") deleted `guiWindow->Init()` from `Gui::AddGuiWindow`. `GuiElement::Init()` is the ONLY caller of `InitElement()`, so both MessageViewer classes (`2ship/2s2h/DeveloperTools/MessageViewer.h`, `Shipwright/soh/soh/Enhancements/debugger/MessageViewer.h`) stopped `calloc`-ing the `char*` members they still `free()` in their destructors -- and those members have no in-class initializer.

So the acquiring half stopped and the releasing half did not. `~MessageViewerWindow`, reached from `BenGui::Destroy` -> `DeinitOTR`, hands glibc two live FOREIGN pointers. glibc accepts them; from then on the free lists describe memory other subsystems still own. SDL's `VulkanCommandBuffer` structs (malloc'd during MM's run, still live because the device is never destroyed) get overwritten by the second core's allocations, and `VULKAN_AcquireCommandBuffer` resets a garbage `VkCommandBuffer` -- faulting arg `0xc1c10ad16cb81ead`, overwritten data rather than a stale pointer.

Fixed with in-class `= nullptr` on both classes. Restoring `Init()` was deliberately NOT the fix: it would revive 62 `InitElement` bodies that `bad027cd` intentionally killed, to solve a two-line memory bug. A destructor releasing what no constructor acquired is wrong C++ regardless -- `ConsoleWindow` already gets this right.

## DEAD END -- do not repeat

**The stack says GPU teardown and that reading is WRONG.** I recorded it in docs/MM_NATIVE.md as "a departing core shuts down ENGINE state" and was flatly wrong. Twelve renderer/SDL teardown entry points were watched under gdb across the whole two-core run -- `SDL_Quit`, `SDL_QuitSubSystem`, `SDL_DestroyGPUDevice`, `SDL_ReleaseWindowFromGPUDevice`, `SDL_DestroyWindow`, `SDL_DestroyRenderer`, `SDL_GL_DestroyContext`, `GfxWindowBackendSDL3::Destroy`, `Fast::Interpreter::Destroy`, `~Fast3dWindow`, `~GfxRenderingAPISdl3Gpu`, `~Window` -- all resolved to real addresses (proven by dumping `info breakpoints` AFTER the run, so "0 hits" cannot mean "never armed") and **all fired zero times**. The GPU device pointer is identical for both games on the same thread across 821 `SDL_AcquireGPUCommandBuffer` calls.

**Reach for `MALLOC_PERTURB_=165` first on this signature.** It moves the abort to the actual guilty `free()` -- `free(): invalid pointer` inside `~MessageViewerWindow`, before the second core even starts.

## Still open, and bigger than this bug

A survey of all 62 `InitElement()` definitions: 2 were this memory hazard, 1 already safe, 27 genuine no-ops, and **32 are FUNCTIONAL GAPS** where non-ImGui work silently stopped. Severe ones: SaveManager sections for gameplay stats and both trackers never registered; cosmetics never hydrated at boot; `BenMenu` has no other call site so the entire 2ship menu tree is never built; `disabledMap` stays empty in front of ~75 `.at()` calls that would throw. See claim C061.

### Note (2026-08-06)
FOLLOW-UP (2026-08-06): the 32 functional gaps are now PARTLY closed, and the split is not where I first guessed.

RESTORED `guiWindow->Init()` in `Gui::AddGuiWindow` — bad027cd's premise ('InitElement/DrawElement are ImGui scaffolding') is false for InitElement. Verified positively with a control in the same run: a save file carrying both a `sohStats` section and a deliberately-unregistered `zelda3dBogusControlSection`, loaded via the REPL `savecycle`. The control warns 'contains unloadable section zelda3dBogusControlSection'; `sohStats` does NOT — its load handler is registered again. Without that control the negative would have been unfalsifiable.

DID NOT restore Init() for `SetMenu`/`SetMenuBar`, and I tried to. That is the Dear ImGui MENU TREE, genuinely replaced by RmlUi. Enabling it threw immediately: gdb `catch throw` gives
  BenGui::SetupMenu -> GuiElement::Init -> BenMenu::InitElement -> Ship::Menu::UpdateWindowBackendObjects -> std::out_of_range: unordered_map::at
MM's boot died before reaching gameplay (live-engine lines 20 -> 5). This is the `disabledMap`/.at() hazard the original survey warned about — the survey was right and my broader hypothesis was wrong.

So the real line is WINDOW vs MENU TREE: a registered window's InitElement carries non-UI work that must run; the menu tree is dead UI that must not be built. That is the distinction bad027cd should have drawn instead of dropping all three call sites.
