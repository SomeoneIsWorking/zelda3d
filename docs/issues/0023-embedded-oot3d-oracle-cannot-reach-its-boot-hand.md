---
id: 23
title: Embedded OoT3D oracle cannot reach its boot handshake on this host
status: investigating
symptom: The harness now completes its Vulkan handshake and captured frame runs, but the title-driving recipe still does not reach a gameplay PlayState or produce a fresh gameplay checkpoint. The software renderer remains too slow for the same scenario on this host.
state_items: S006
tags: oracle,harness,renderer,vulkan,software
created: 2026-09-12
updated: 2026-09-12
---

## Root cause

The libretro Vulkan fork called `set_image` and the synchronous `video_refresh` callback before its
worker had submitted the render command. Its `MasterSemaphoreLibRetro` then discarded the render
semaphore, so the frontend read back an image that was still in the core's pending submission. That
queue-order violation manifested as a fence stall and `UNDEFINED`/`SHADER_READ_ONLY_OPTIMAL`
validation errors.

The remaining title-driving failure is a separate setup/state boundary. A cold run accepts the
same A/START input and reaches the recovered title actor's transition request (`play+0x5C2D =
0x14`), but the title-to-file-select handoff cannot become gameplay because this harness save
directory has no save slots and Azahar reports the required system title
`title/0004000e/00033500/content/00000000.app` missing. The old title-state accessor also treated
the 3DS live-play global (`play + 0x14`) as the PlayState base, causing `scene` to report `0x05c0`
instead of the actual title scene `0x006b`; that diagnostic error is now corrected in the harness.

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
`0de1e9d7ab2aff3d980ec9056966e85f4c0ba345`). The fork carries the six harness RPC/oracle commits
and the applied memory, PICA, software-rasterizer, logging, and custom-texture probes that were
previously described as a re-applied patch stack. The separate GPU blit logger remains only in the
existing dirty checkout and is not part of this pinned branch.
The harness readback path now tracks the staging image layout and restores the core image layout
before returning. The core now signals that frame handoff and waits for its worker submission before
the callback; a Vulkan validation run over 30 captured frames completes with no image-layout or
semaphore-submit errors.

### Note (2026-09-12)
The no-capture title probe now has a falsifying trace for the remaining gap: cold boot → `run 300`
→ A hold/release → `run 50` writes the recovered transition byte `0x14`, but `gameplay` remains
`ok no`; the emulator logs the missing system-title app and missing `save00.bin`/`save01.bin`/
`save02.bin`. After normalizing the title global by subtracting its documented `+0x14` bias,
the same probe reports `playstate 0x0871e840 mode=title` and `scene 0x006b`, so the title
diagnostics no longer hide the handoff state behind the wrong base address.

### Note (2026-09-12)
The maintained fork's libretro core already supports `RETRO_DEVICE_POINTER`, but the harness
frontend previously exposed only joypad and analog state. The harness now owns a narrow
`pointer <x> <y> <pressed>` REPL command, forwards signed-16 coordinates and press state through
the real libretro callback, and rejects out-of-range coordinates or invalid press values. A live
command discriminator passed for both press/release and negative-coordinate inputs. This removes
the harness-control blind spot; it does not yet prove an empty-slot name-entry sequence or create a
gameplay checkpoint.

The harness keeps single-screen output by default for existing parity captures. The diagnostic
override `ZELDA3D_HARNESS_TOUCH_LAYOUT=1` selects the fork's `default` two-screen layout so pointer
coordinates can reach the touchscreen; no game or emulator memory is bypassed.
