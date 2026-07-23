# 2026-07-23 — Link movement: walk vibration, door-exit slide, climb warp-up (#201 a/b/c)

User-reported, long-standing, "very disorienting": (a) jittery/vibrating while walking,
(b) door-exit slide, (c) climb warp-up. Hypothesis going in was "one shared position-integration
bug". **Outcome: NOT one cause — (a)+(c) shared the draw-ANCHOR mechanism, (b) was anim
SELECTION. All three root-caused; all three fixed; (a)(b) live-verified end-to-end, (c) verified
at the mechanism level (forced climb clips) — a real ladder-grab repro was not achieved headless.**

## Measurements that framed it (all in scratch/logs/)

- Logic-side integration is CLEAN: steady walk dz = 2.241/frame constant (spd 1.494 × R_UPDATE_RATE
  1.5), zero variance (`ground_walk40.txt`). The old "position steps at logic rate" hypothesis is
  FALSE — SoH frame interpolation covers the zelda3d draw matrix (it goes through the recorded
  Matrix_* ops + mtx_replacements like every actor).
- Render-side subframe trace (new REPL `mptrace <modelId|link>` → `[MPTRACE]` lines): horizontal
  anchor perfectly smooth (dz/subframe constant to 5e-5), but the VERTICAL anchor wobbled in a
  ~0.6-unit band reversing direction every logic frame (`mptrace_walk.txt`) — that band is
  (a), the walking vibration.
- Source of the band: `Zelda3D_PosedGroundOffset` per-frame min-visible-vertex grounding. During
  walk: groundOff std 16.8 local / p2p 81 local = **0.9 world units of per-frame vertical noise**.
- Climb clips: groundOff idle −1361 vs climb_upL −2271 vs climb_upR −3793 → **switching upL→upR
  teleported the whole body 16.7 world units in one frame** — (c)'s ladder-phase warp. (The
  ledge-vault half of (c) is the missing `shape.yOffset` term, see below.)
- Door exit (warp 0x1D1, market mask-shop exit spawn): Link glides 16 frames × 3 units at
  lin=2.0 **with csab=nml_wait_free (idle pose)** — (b) captured exactly.

## Root causes

### (a) walk vibration + (c) climb warp — the draw anchor was a heuristic, not the RE'd mechanism
The real 3DS mechanism (RE'd this session):
- ALL Link CSABs — including the child/anim clips — author the hip (bone 1) translation in the
  **BOY rig's space** (boy hip rest 3538.08, child 2156.32; child walk clip hip ty 3441..3554
  read from the ROM; live oracle child jointTable carries the raw boy values at +stride 0x34).
- The engine scales the ANIM-provided root translation by the **age factor 0.64** at
  draw/consume time — N64 `Player_OverrideLimbDrawGameplayDefault` (z_player_lib.c:1304
  `pos->x/y/z *= 0.64f` for child), and Grezzo kept the literal on 3DS: `FUN_002bc768`
  `DAT_002bc8b8 = 0.64` (root-motion consumption twin; pool VA 0x2bc8b8 — one of five 0.64f
  pools in code.bin).
- The 3DS actor base transform is `T(pos.x, pos.y + shape.yOffset*scale.y, pos.z)·R_YXZ·S`
  (FUN_00408828, en_horse_rider_pos.md) — the **shape.yOffset term is load-bearing for the ledge
  vault**: z_player.c:5002-5005 hides the one-frame `pos.y += yDistToLedge` behind
  `shape.yOffset -= yDist*100`, decayed by `Math_StepToF(&yOffset,0,150)` (:10639). Our draw
  omitted it → teleport.
- Player actor scale on 3DS = **0.01 exactly** (live oracle read player+0x54). Our hand-tuned
  0.011 was compensating the grounding sink.

Fix: `Csab::sampleLocalTRS` grew `animTransScale` (applied to genuinely ANIMATED translation
tracks only; rest fallbacks and static-track rest keeps are rig-space and never scaled), threaded
through all skin/world/morph/two-source entry points; `Zelda3D_SetAnimTransScale(modelId, age)`
set from the Link draw (child 0.64 / adult 1.0). Draw translate now carries
`+ shape.yOffset*scale.y`; per-frame grounding + the climb groundOff freeze + `climbgroundfix`
knob DELETED; `gZelda3dLinkScale` default 0.011 → 0.01.

AFTER: steady-walk rendered anchor dz constant (std 5e-5), vertical band **0.000** (was 0.6);
climb-clip upL↔upR anchor delta **0** (was 16.7 units); idle posed min-vertex = +94.3 local
(prediction from 0.64 was +96) — Link stands ~0.94 world units above actor.y, which is what the
0.64-vs-bind-ratio (0.6095) residual authors on the real 3DS too.

### (b) door-exit slide — the leg driver runs even when the named anim is the idle
During scripted forward moves (door-exit walk-out and entrance walk-ins:
`Player_Action_80845CA4` → `func_80845BA0` → `func_80845964`), N64 keeps
`skelAnime.animation` on the IDLE header and drives the legs through **func_80841EE4** (the
`unk_868` leg-phase accumulator — z_player.c:10703), the same mechanism as normal ground
locomotion. Our CSAB selection keyed locomotion off the resolved anim NAME → idle frozen at
frame 0 while the body glided = the slide. unk_868 advancing (~1.95/frame) in the captured trace
confirmed the driver was running.

Fix (zelda3d_link.cpp, selection block): when unk_868 advanced this logic frame AND
speedXZ > 0.5 AND the resolved clip is not already a loco clip (and no heldActor), substitute
walk/run by the same speed threshold as the walk/run gate; the existing unk_868 phase-locked loco
path then plays it — which IS what N64 renders. AFTER: warp 0x1D1 walk-out plays nml_walk_free
for the full glide, then walk_endR → idle. Clip: `scratch/screenshots/door_exit_after.mp4`
(before: `door_exit_before.mp4` context / the 0x1D1 trace).

## Evidence files
- clips: scratch/screenshots/{walk_jitter_before,walk_jitter_after,door_exit_after,climb_ladder_after}.mp4,
  idle_after_fix.png (stand/feet), idle_final_stand.png
- traces: scratch/logs/{ground_walk40.txt,mptrace_walk.txt,mptrace_walk_after.txt,ground_walk_after.txt}
- oracle reads: scratch/logs/oracle_root2b8.csv (+ inline session reads: jointTable hip raw
  boy-space on the CHILD save; player scale 0.01)

## Regression gates
- `tools/parity_pose_sweep.py`: idle 1.2° / walk 1.3° / run 1.9° — all MATCH/PASS (baseline
  1.2/1.2/1.7; sweep measures LOCAL ROTATIONS, untouched by this change; drift is capture noise).
- `tools/link_sweep.py sweep --skip-oracle`: selection curve correct (mag 0→wait, 30→walk,
  60+→run); backwalk MATCH. idle/walk/run print UNREACHABLE **only because --skip-oracle**
  ("oracle unavailable: not booted") — not a regression.
- HUD renders (hearts/rupees/buttons visible in every captured screenshot).

## Open residuals (recorded in re-frontier player.draw-anchor)
1. The 3DS DRAW-side 0.64 application site is not yet pinned to a specific function (0.64f pools
   at VA 0x254ac4 / 0x279748 / 0x325a20 undecompiled; 0x2bc8b8 = root-motion consume is pinned).
   The value itself is double-sourced (N64 literal + 3DS root-motion literal).
2. Anim-movement (movementFlags 0x9B) root-motion consumption is NOT yet mirrored in the CSAB
   draw: during door-open/time-travel style anims the CSAB hip x/z track could double-apply on
   top of the actor position. Not observed live yet (the door-exit walk-out turned out to be the
   unk_868 path, not anim movement) — needs a live repro (real EnDoor pass-through) before any fix.
3. Real ladder-grab climb never engaged headless (Kokiri treehouse ladder: approaches slid
   past/ran up the root; wall at spawn not climb-flagged). End-to-end climb pose selection
   (Fclimb anims) + climb-entry pos jumps remain UNVERIFIED live — repro tooling gap (a
   `wallFlags`/`yDistToLedge` readout + a deterministic ladder-approach primitive would close it).
4. Stand-height: we now draw feet +0.94 units above actor.y at idle mid-breath (authentic per the
   0.64 literal). Worth one oracle A/B screenshot at matched camera to confirm the 3DS shows the
   same residual.
5. REPL `tp` right after `warp` writes through a stale PlayState (pos reverts) — tooling bug
   observed repeatedly this session; worth a look in zelda3d_repl.cpp state refresh.

## Falsified along the way
- "All three bugs share one position-integration cause" — NO: (a)+(c) draw anchor, (b) selection.
- "Position steps at logic rate without interpolation" — NO: mtx interpolation covers the player
  draw; horizontal anchor is subframe-smooth.
- "+0x2B8 is the live animated root" — NO: it is the rig REST hip (3538.08 constant even while
  walking); the live animated root is jointTable bone 1 (stride 0x34 rows = local 3x4 matrices).
