---
id: 30
title: REPL `step` after a warp crashes in KaleidoScope_UpdateCursorSize
status: investigating
symptom: `zelda3d_repl.py cmd "step N"` aborts the headless OoT instance with a SIGABRT inside `KaleidoScope_UpdateCursorSize` -> `KaleidoScope_Update` -> `KaleidoScopeCall_Update` -> `Play_Update`, called from `Zelda3D_SimulationReplCommand`. Reproduced while driving the Temple of Time dais staircase for the camera.calc-at-default-ybias discriminator: `freeze 0`, `warp 0x60`, settle, `freeze 1`, `tpf 120 1180 180`, `step 20`.
state_items: S006
tags: repl,crash,kaleidoscope,freeze,step,warp,oracle
created: 2026-10-04
updated: 2026-10-04
---

## Measured, 2026-10-04

Found while building the `camera.calc-at-default-ybias` live discriminator. The
sequence that dies is a REPL `step` taken after a warp has been queued and
completed while the game was frozen-then-resumed. `StepFrames` in
`repl/commands/simulation.cpp` loops `Zelda3D_StepLogicFrame` (`WalkInject` +
`Play_Update`), and the abort is inside `Play_Update`, not inside the stepping
harness, so the frozen-step path itself is not the fault.

The backtrace the instance printed (from `scratch/logs/run.log`):

```
KaleidoScope_UpdateCursorSize (+0x25)
KaleidoScope_Update (+0x8D)
KaleidoScopeCall_Update (+0x123)
Play_Update (+0xBEF)
Zelda3D_SimulationReplCommand(PlayState*, char const*, char const*, char const*) (+0x1C8)
```

Scene at the crash was `SCENE_TEMPLE_OF_TIME`, room 1, Link in category PLAYER.

## Scope of what is known, and what is not

* **Known:** `step` under `freeze` is not crash-safe after a scene transition
  in this state. A `step` in the same scene without a preceding
  `freeze 0` / `warp` / settle cycle runs fine — the discriminator's own
  `atdefault trace <n>` takes the same `Zelda3D_StepLogicFrame` path 60 times
  per run without aborting.
* **Not established:** whether the trigger is the warp, the KaleidoScope
  (pause-menu) state surviving a transition while frozen, or the `tpf` landing.
  Not bisected, because the discriminator did not need it: the same
  staircase run succeeded by warping in one `drive_host` sequence and by
  choosing a different `tpf` destination.
* **Not a regression from the `Zelda3D_StepLogicFrame` extraction.** That
  change only moved `Zelda3D_WalkInject(play); Play_Update(play);` out of
  `simulation.cpp` into `control/frame_step_control.c` with identical
  ordering, and both `step`/`settle` and `atdefault trace` call it.

## Why it is filed rather than worked

It is not on the `camera.calc-at-default-ybias` critical path (the live
measurement completed), it needs a bisect across warp/pause-state/teleport to
characterize honestly, and it is a shipping-path crash rather than a parity
question. Recorded so the next session that steps frames across a transition
knows the hazard exists instead of discovering it as a lost run.

## Related trap found in the same session

`ZELDA3D_CORE_OOT` must name a core **in the same directory** as the shipping
one. `Shipwright/zelda3d_app/zelda3d_main.cpp` `chdir`s into
`dirname(ResolveCorePath(spec))` before `dlopen`, so pointing the override at
a copy under `scratch/` makes the run fail asset resolution and abort in
`AutoExtract` with `Error: Unable to read config file`. This matters because
that override is how a deliberately perturbed core is loaded for a live
control measurement.