---
id: 23
title: Gameplay oracle is one missing savestate away; the cold title-to-gameplay route cannot reseed it
status: investigating
symptom: The cold title-to-gameplay route does not reach a gameplay PlayState (missing system title app, empty save slots), so no fresh gameplay checkpoint can be written, and `GAMEPLAY_STATE` (scratch/gameplay_settled.<render-contract>.state) is absent. The gameplay oracle route itself is sound -- `boot_to_gameplay` loads a cached state and skips the title, and it produced every cached gameplay capture under the current render-contract marker. The only gameplay state on disk is an unmarked predecessor that the current Azahar build cannot deserialize (`boost::archive::archive_exception`), so it is stale, not misnamed.
state_items: S006
tags: oracle,harness,savestate,render-contract,title-route
created: 2026-09-12
updated: 2026-09-28
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

## The cold route, measured frame by frame (2026-09-27): the blocker is external state, and it is exact

The record above said the cold title route "fails". It did not say *where*, so it was re-driven with
a frame captured at every step (`tools/title_flow_watch.py`, `tools/title_load_watch.py`). The flow is
**order-sensitive in a way that made earlier recipes look broken**:

* **A alone does nothing at all.** `scene` stays `0x006b`, `playstate` stays `mode=title`.
* **Start is the button that transitions**, and only once the title card is up. The correct user path
  is: run to the logo (~1093 frames), press **Start once**.
* The transition is not instant. Roughly 200 frames of pure black (`min=max=0`), and then the screen
  resolves to a **sky-only frame** — clouds, no geometry, no UI — at `scene 0x0000`.

### Refuted on the way, so nobody repeats them

* **"The menu only accepts input after the logo appears."** Refuted: the flow is byte-identical when
  the first tap is at frame 300 and at frame 1093 (`0x006b` -> `0x0000` on `start,start,a` either way).
* **"The black screen is still loading."** Refuted: 2400 frames at `scene 0x0000` leaves
  `playstate=ok 0x0871e840 mode=title` and `gameplay=ok no` at every 200-frame sample. It is stuck, not
  slow. (This one nearly reproduced a false negative: an intermediate probe read a frame only ~40
  frames into the transition as "black and stuck", when the screen in fact resolves at ~200.)

### What the emulator actually reports

```
core/file_sys/savedata_archive.cpp:OpenFile: Non-existing file .../title/00040000/00033500/
  data/00000001/save00.bin can't be open without mode create        (and save01.bin, save02.bin)
```

The game's save index is `.../data/00000001/system.dat`, and **it is not a 3DS file at all** — 34
bytes, no `SAVE` magic (`00 3D 55 24 43 75 54 65 ...`). Deleting it changes the reported error but
**Azahar does not regenerate it**, so the file-select has no slots to enumerate either way. A valid
save-data index is what would let the game create slot 0, and hand-authoring one would be exactly the
kind of guessed binary layout this project refuses to ship.

### The missing system title is genuinely absent, not mis-pathed

The NAND image carries system *data* (`sysdata/00010017` config, `00010026` eventlog, `00010035`
news.db, `extdata/00048000` gamecoin) but **has no `title/` directory at all**, and `0004000e` appears
nowhere in the whole save tree. The SD card holds only the game itself (`title/00040000`).

So the residual blocker is external state that must be **provided, not derived**: either an Azahar
image carrying `title/0004000e/00033500`, or a valid 3DS save-data `system.dat` so the game creates
its own slot. Neither is something to fabricate — the first is Nintendo system-title content, the
second is a hand-built binary filesystem.

### A second concrete dependent, measured 2026-09-27

The lit-material ground truth for `render.cmb-fragment-lighting` needs a gameplay scene, and this
issue is why it cannot be had. Measured over **207 draws at two points in the title demo: `picaLit=1`
on zero of them, 159 vertex-lit** (`tools/lit_pica_capture.py`, selecting on the authoritative
`regs.lighting.disable`, not the independent CmbVShader boolean). The title demo is simply not a
fragment-lit scene, which is why every fragment-lighting capture in this project is a negative control.

So the two open questions on that frontier step -- the `+0x18A` byte's contribution to `config0` bit
`0x11`, and the per-slot enable bytes for a lit material -- are behind this issue, not behind more RE.
Unblocking it needs an Azahar image carrying `title/0004000e/00033500`, or a valid 3DS save-data
`system.dat` so the game creates its own slot; neither is derivable here.

### Consequence for the renderer campaign

The title screen remains available as an oracle, and the corpus surveys do not need one at all, so
this blocks *gameplay-scoped* parity specifically. It also explains why the title host-vs-oracle image
is a weak discriminator for renderer semantics: the host runs the **N64** title demo and the oracle
runs the **3DS** title demo, so the authored camera scripts differ and most of the measured delta
(`union_rgb_mae=47.80`) is that difference rather than raster semantics. A forced pose or a
per-material isolation is the honest way to compare raster semantics on the title content, and it
cannot stand in for a gameplay interaction.

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

### Scope widened 2026-09-28: this is now a TWO-GAME issue, with different missing pieces

The title-to-gameplay route problem is per-game, and the two titles are missing *different* things, so
they must not be filed as one. Recording that here because the "no MM3D oracle at all" reason that used
to sit under MM3D was refuted (see `docs/project-state.md` S003/S005) and this file is what the
remaining truth belongs in.

**OoT3D** is blocked exactly as above: a cold run needs a `0004000e` system-title app or a valid
`system.dat` to reach a gameplay `PlayState`, and without one no fresh gameplay checkpoint can be
written. Its gameplay oracle route (`boot_to_gameplay` over a cached state) is sound.

**MM3D** used to be filed as worse -- no oracle capture of any kind. That was wrong, twice over:

* MM3D **boots and renders in the Azahar oracle.** The harness core is game-agnostic (it takes a ROM
  path and hands it to `retro_load_game` with no identity check, and all 18 patches in
  `AZAHAR_PATCH.md` are observation hooks -- no OoT3D addresses, no HLE overrides). MM3D's image
  needed its NCCH `no_crypto` header bit set in a copy before the emulator would load it
  (`tools/ctr_oracle_rom.py`, 13 tests; provisioned automatically by
  `provision_mm3d_oracle_rom`, exported as `$ZELDA3D_MM3D_ORACLE_ROM`). Its own ROM filename
  corroborates the diagnosis -- the dump is literally named "Decrypted".
* Frames, `run`, `mem`, `az_fog`, `lighting_capture` and the PICA draw logs all work on MM3D today
  with no further work. Measured: PICA fog is scene-driven (`(0,0,0)` to frame 1600, `(187,110,110)`
  from 1800, `(90,110,0)` from 3600) and fragment lighting is on for 116-143 of 131-161 draws per
  frame.

So MM3D's *remaining* gap is the same one OoT3D has, plus one thing OoT3D does not:

1. **No gameplay state**, same reason -- `docs/issues/0023` applies to it verbatim. A gameplay
   `PlayState` is what a fresh MM3D checkpoint would be written from.
2. **Its opening is fog mode 0** throughout frames 600-4000, so the *fogged-frame* counterfactual is
   not reachable from the title even though the fog colour register is being written. The colours
   observed there are the opening's own, not a scene table's.
3. **MM3D needs its own recovered addresses for the high-level commands.** `playstate`, `scene`,
   `actors`, `warp` and `az_daytime` all resolve through OoT3D's `oracle_layout.h`, so on MM3D they
   either do not resolve or resolve wrongly. This is extra work MM3D has and OoT3D does not, and it
   is a RE task, not a state task.
4. **An OoT3D savestate cannot be dropped into MM3D.** A savestate is a whole-`System` boost archive
   carrying a `program_id` that `LoadStateBuffer` checks (`Azahar/src/core/savestate.cpp:266`). The
   measured program IDs are OoT3D `0x0004000000033500` and MM3D `0x0004000000125500`, so each title
   needs its own.

The practical consequence: reaching gameplay for EITHER title is the single highest-value unblock in
this campaign, and it is one task, not two.
