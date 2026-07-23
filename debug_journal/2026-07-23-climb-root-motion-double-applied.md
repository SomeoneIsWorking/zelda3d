# 2026-07-23 — Link floats off the ladder: the clip's root motion was applied TWICE

User report (with screenshot, after `f6d73b98`): *"link goes up in the air when mounting the ladder
and his animation cycles while climbing cause him to appear warping down while climbing up on every
cycle."* Correction from the user during the session: **he is NOT rotated** — the climb pose is
right, only its PLACEMENT is wrong. That is translation-only evidence, and it held up.

**Root cause: the CSAB's root-translation track was drawn while the ENGINE was simultaneously
consuming it into `actor.world.pos` — the ladder rise applied twice, resetting on every clip loop.**
Fixed by porting N64's root-motion split (`SkelAnime_UpdateTranslation`) into the CSAB draw.

## The mechanism (ground truth)

N64 `z_skelanime.c:2001-2041` `SkelAnime_UpdateTranslation`: while anim-movement is running it takes
the frame delta of `jointTable[0]` into the actor position and **immediately overwrites the root
joint with `baseTransl`** — x/z unconditionally, y as well when `ANIM_FLAG_UPDATEY` is set. So the
consumed components are NEVER part of the drawn pose. `z_player.c:12600` queues that consumption
every frame while `skelAnime.movementFlags & 8` (`ANIM_FLAG_ENABLE_MOVEMENT`); the ladder climb
enters with `Player_StartAnimMovement(play, this, 0x9F)` (`func_8083EC18`, z_player.c:7637).
The 3DS twin is `FUN_003603f8` (`Player_StartAnimMovement`) — byte-identical structure:
`+0x2ac/0x2b0/0x2b4 prevTransl`, `+0x2b8/0x2bc/0x2c0 baseTransl`, `+0x2a6 movementFlags`,
`+0x2cc` jointTable ptr with the RE'd 0x34 stride (`oot3d-decomp/docs/player_port.md`).

Our port runs the vendored N64 player logic (so the actor motion was already authentic) but drew the
pose from the OoT3D CSAB **without mirroring the reset** — the clip's own root track went straight
into the draw.

## Measurements

Offline, from the ROM (child zar, `tools/csab.py`), bone 1 = the Link rig's root-motion bone:

| clip | bone1 tY | per-clip |
|---|---|---|
| `nml_Fclimb_upL` | 4772 → 6272 | +1500 local = **+15 world** |
| `nml_Fclimb_upR` | 6272 → 7772 | +1500 local = **+15 world** |
| `cl_nml_climb_upL` (vine/wall family) | 3561 → 5236 → 5061 | same shape |
| `nml_wait_free` (idle, no anim-movement) | 3477…3499 | pelvis bob only |

`upR` ends at 7772 and the next `upL` restarts its track at 4772 → a **−3000 local (−30 world)** snap
at every pair boundary. The engine adds +15 world per rung to `world.pos.y` over the same frames
(measured live, `[link] GROUND` log: −66.87 → −51.84 → −36.66 …, monotonic, `yOffset` 0 throughout).
15 + 15 up, then −30 back = "climbs off the ladder, warps down every cycle", exactly as reported.
**x and z are affected too**, not only y (`Fclimb_upL` tX 0…302, tZ 471.8…696.3) — the recorded
residual's "hip-x/z" scope was incomplete, it is all three components.

Live, drawn lowest posed vertex (`linkground`) over one forced ladder climb, Kokiri:

```
BEFORE  groundOff  -1000 … -2850 local (10-28 world units below actor.y)
        drawn foot jumps +21.7 / +24.2 / +22.8 world units at clip boundaries
AFTER   groundOff    -44 … +407 local  (within ~4 world units of actor.y)
        drawn foot monotonic, +2.1 … +10.5 per sample, NO boundary jump
```

Visual before/after strip: `scratch/screenshots/climb_before_after.png` (top = before: Link ascends
at roughly double rate and detaches from the rail; bottom = after: he tracks the rail).
Clip: `scratch/screenshots/climb_after_fix.mp4`.

## The fix

`Csab` gained a `RootMotion` descriptor (replacing the bare `float animTransScale` parameter on all
six entry points): `{ transScale, pinBone, pinMask }`. A pinned component of the root-motion bone
keeps the **rig's rest translation** instead of sampling the clip track — the analogue of N64
writing `jointTable[0].c = baseTransl.c`. `Zelda3D_SetAnimRootPin(modelId, bone, mask)` sets it
per model; the Link draw computes the mask each frame straight from the engine's own state:

```c
if (player->skelAnime.movementFlags & ANIM_FLAG_ENABLE_MOVEMENT) {   // z_player.c:12600
    pinMask = X | Z;                                                  // reset unconditionally
    if (player->skelAnime.movementFlags & ANIM_FLAG_UPDATEY) pinMask |= Y;
}
```

Nothing is subtracted or tuned; the pin is on exactly while the engine's consumption is queued.
This also covers the other anim-movement states by construction (ledge vault 0x9D/0x9F, door
pass-through 0x9B, time travel) — that was the open `player.draw-anchor` residual.

## Gates

- `tools/parity_pose_sweep.py`: idle **1.2** / walk **1.3** / run **1.8** deg — all PASS (unchanged;
  those states have `movementFlags == 0`, so the pin never engages there).
- `tools/link_sweep.py sweep`: **MATCH=24, DIVERGENT=0, UNREACHABLE=1** (baseline 23/0/2 — walk and
  run reached the live oracle this run and pose-matched at 1.3 / 1.6 deg).
- Facial channel drives (`linkface` → `waitF_itemA_20f`, faceb=1), native HUD renders, top-out and
  the following on-foot walk are clean (`scratch/screenshots/after_hud_face.png`).

## Falsified / corrected on the way here — do NOT re-derive

- **"OoT3D player CSABs have no translation tracks at all."** WRONG, and it was my own tooling bug:
  `tools/csab.py` keys `AnimNode.tracks` by NAME (`"tX"`/`"tY"`/`"tZ"`), not by integer slot, so
  `nd.tracks.get(0)` silently returned `None` for all 582 clips. A scan that reports "zero clips have
  translation tracks" is the signature of this mistake. Use the string keys.
- **`f6d73b98` is not at fault and must NOT be reverted.** It fixed clip SELECTION; the pose it now
  plays is correct (the user confirmed the pose looks right). It only made a pre-existing draw bug
  observable, because the idle clip it used to fall back to has a nearly-static root track.
- **The reported symptom does not come from `shape.yOffset` or the actor path.** `yOffset` is 0 for
  the whole climb and `world.pos.y` is monotonic — both measured per frame.
- **Root ROTATION is not involved** (matches the user's "he is not rotated").

## Tooling notes

- `tools/ladder_repro.py --target ladder`'s hop route is **broken**: `tpf` is collision-swept, the
  leg to `(-470,-560)` lands Link on a roof at y≈196 and he then falls through Mido's door (scene
  0x28). Do not trust it. The reliable way to reach the **real-ladder** clip family (`Fclimb_*`,
  `actionVar1=2`) is: `tpf -29 990 0` in Kokiri (stationary wall contact, `wallPoly` set, no
  auto-grab because `linearVelocity==0`) then `forceclimb`, which ORs the ladder bit into
  `func_8083EC18`'s wall flags. The natural walk-in grab on that same wall gives the vine/wall
  family (`cl_nml_climb_*`). Both families were exercised for this fix.
- Session scripts: `scratch/climb_measure.py` (per-frame pos + clip + `linkground`),
  `scratch/fclimb_seq.py` / `scratch/nat_seq.py` (fixed-camera strips).

## Residual (honest)

The pin target is the **rig's own rest translation** (child bone1 y = 2156.32, boy = 3538.08), not
Grezzo's `baseTransl` constant, which is NOT yet RE'd. Evidence that rest is the right order of
magnitude: N64's `sSkeletonBaseTransl` is `{-57, 3377, 0}` and the child draw scales it by 0.64 →
2161.3, within **0.05 world units** of the child rig's 2156.32. Measured consequence of the
approximation: entering anim-movement shifts the drawn body by **+0.6 world units** (idle foot sits
1.0 units below `actor.y`, climb foot 0.4 below). Pinning the 3DS constant is tracked as a
`player.draw-anchor` gap.
