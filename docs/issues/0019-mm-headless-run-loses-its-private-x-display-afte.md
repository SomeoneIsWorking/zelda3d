---
id: 19
title: MM headless run loses its private X display after launcher returns
status: resolved
symptom: MM headless gameplay reaches a scene then X connection breaks and phase report has zero samples
tags: mm,headless,xvfb,graphics,tooling
created: 2026-08-13
updated: 2026-08-13
---

## Root cause

`tools/mm_game.sh` launched Xvfb as an ordinary background child but launched `zelda3d mm` through
`setsid nohup`. When a wrapper such as `gpuguard run` returned after `mm_game.sh start`, its shell
sent SIGHUP to the still-attached Xvfb process. The detached game remained alive but lost `:94`; its
first live phase report consequently contained zero pairs and was not evidence about skinned graphics.

## What was tried / dead ends

The first Termina Field run was deliberately retained as a negative result: `posinfo` confirmed scene
45 reached gameplay, but the run log contained `X connection to :94 broken` and the phase report said
`0 (model,clip) pair(s) sampled`. Treating that as a clean skinned-render run would have been false.

## Resolution

Launch the private Xvfb through `setsid nohup` as well. The next guarded Termina Field run remained
alive after the manager returned: `posinfo` reported scene 45, the game process and Xvfb were both
present after an additional 12 seconds, and the log had no X-disconnect line. That particular scene
only loaded rigid archives, so it proves transport lifetime rather than skinned-animation coverage.

The repaired path was then exercised through one orderly, in-process three-scene tour: South Clock
Town → Termina Field → Great Bay Coast, followed by REPL `quitteardown` so the report flushed. It
sampled 9 `(model, clip)` pairs; all 7 pairs with at least two samples moved, and morph fired on
1218 samples (including `kamome_fly` at max weight 0.93). This validates the control transport and
the existing limited live coverage. It is not a claim of whole-game skinned-render parity.
