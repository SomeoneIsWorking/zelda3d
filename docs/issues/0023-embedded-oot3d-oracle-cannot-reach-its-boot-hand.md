---
id: 23
title: Gameplay oracle is one missing savestate away; the cold title-to-gameplay route cannot reseed it
status: investigating
symptom: The cold title-to-gameplay route does not reach a gameplay PlayState (missing system title app, empty save slots), so no fresh gameplay checkpoint can be written, and `GAMEPLAY_STATE` (scratch/gameplay_settled.<render-contract>.state) is absent. The gameplay oracle route itself is sound -- `boot_to_gameplay` loads a cached state and skips the title, and it produced every cached gameplay capture under the current render-contract marker. The only gameplay state on disk is an unmarked predecessor that the current Azahar build cannot deserialize (`boost::archive::archive_exception`), so it is stale, not misnamed.
state_items: S006
tags: oracle,harness,savestate,render-contract,title-route
created: 2026-09-12
updated: 2026-09-27
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

## Scope correction (2026-09-27): the gameplay route works; one state file is what is missing

The issue had been carried as "the gameplay oracle is unreachable", which is wrong and was making
more capabilities look blocked than are. Measured:

* **The gameplay route is a real, working route.** `harness_gameplay.boot_to_gameplay` loads a cached
  gameplay savestate, runs 60 frames, and checks `in_gameplay` — its own docstring says "the cached
  state avoids title input entirely... future boots skip the title entirely". It is how every
  `c57f33c936bb6002_*` gameplay cache was produced, and those caches carry keys
  `p39`..`p45-00401070` — the **current** render-contract marker. So gameplay captures have been
  taken under the contract in force today.
* **What is missing is one file, and the reason is exact.** `GAMEPLAY_STATE` resolves to
  `scratch/gameplay_settled.p45-00401070.state` and does not exist. The only gameplay state on disk is
  `scratch/gameplay_settled.state`, a symlink to `raw/fd2-oracle-fixed.state` — an *unmarked
  predecessor*. Loading it does not fail a check, it **kills the oracle**:

  ```
  terminate called after throwing an instance of 'boost::archive::archive_exception'
  ```

  That is Azahar's boost serialization refusing a savestate written by an incompatible build, i.e. the
  render-contract/version invalidation the `azahar_render_contract_marker()` filename encodes. The
  marker is doing its job; the state is genuinely stale, not misnamed.
* **And it cannot simply be re-made.** Writing a fresh gameplay state requires reaching gameplay,
  which is the cold title route this issue already documents as failing (the missing system title
  `title/0004000e/00033500/content/00000000.app` and the empty save slots).

So the residual blocker is precisely: **produce one gameplay savestate for the current Azahar build.**
Everything downstream of that — the material-identified gameplay command lists that
`tools/cmb_shader_uniform_coverage.py` says `render.cmb-texcoord-mapping` items (1), (2) and (4)
need, and any gameplay-scoped host-vs-oracle image — is waiting on that one artifact, not on new RE.

The pre-handshake Vulkan work recorded below stays valid and stays fixed; it was never this blocker.

## What was tried / dead ends

* Loading `raw/fd2-oracle-fixed.state` directly as `boot_to_gameplay`'s state: the harness closes
  stdout and the oracle log ends in `boost::archive::archive_exception`. Confirms staleness at the
  mechanism level rather than by inference from the filename.
* `scratch/gameplay_settled.state` is a hand-made symlink to that raw state, not a state the current
  build wrote. Treating it as a cached gameplay state is what made the route look broken rather than
  merely unseeded.
* **The title demo is not a substitute route, measured.** `harness_gameplay.in_gameplay` deliberately
  refuses the title demo's PlayState (its docstring: "`playstate` deliberately falls back to the title
  demo's PlayState, so it cannot establish the loaded-save precondition required by `warp`"), so the
  demo could at best supply renderer frames rather than a warpable save. Whether it even supplies
  frames is now measured: a cold oracle run of **900 frames** with capture disabled stayed
  `gameplay=ok no`, `scene=ok 0x006b`, `playstate=ok 0x0871e840 mode=title` at every 150-frame
  sample. The demo does not self-start within that budget. That does not prove the demo is
  unreachable — it may need input or a longer idle — so this is a bounded negative, not a refutation,
  and nobody should spend time on it again without a named input sequence to try.


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

### Note (2026-09-12)
The maintained Azahar fork also carries a `PacketType::Touch` UDP-RPC handler, but that server is
owned by the optional standalone scripting frontend (`ENABLE_SCRIPTING`) and is not started by the
embedded libretro core. The embedded harness therefore drives the same fork input path through
`RETRO_DEVICE_POINTER`; this is its in-process frontend adapter, not a second HID implementation.
The harness now answers the fork's three touchscreen option lookups explicitly, so pointer support is
enabled by the harness configuration owner rather than by `FetchVariable` fallbacks.

The existing `diag` command now reports pointer poll counts and IDs alongside joypad polling. This
distinguishes a control that was merely accepted by the REPL from one consumed by the fork's input
path before any title transition conclusion is drawn.
