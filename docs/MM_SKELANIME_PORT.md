# MM3D skinned-actor port — design brief

## Why

MM3D auto-probe reports (as of `0be8d766`, sweep across South CT + Termina Field +
Clock Town {E,W,N} + Woodfall + Astral Obs + Ikana Graveyard):

- Rigid archives (≤1 bone) auto-mapped: **21**
- Skinned archives (>1 bone) rejected: **21** in these scenes alone
- Total shipped archives: **418** (`scratch/mm3d_actor_archives.md`)

The rejected set is small in dev scenes because sweep coverage is thin — but nearly
every NPC, enemy, and treasure chest is bones>1. Landing this path is the single
biggest lever for MM3D visual parity.

## Ground truth — how OoT does it

See `Shipwright/soh/src/zelda3d/zelda3d.c` (6.5k lines). The skinned dispatch:

1. **Defer at draw time** (~L2160). `Zelda3D_TryDrawActor` detects
   `Zelda3D_AutoModelSkinned(modelId)`, doesn't emit — instead stashes:
   - `gZelda3dPendingActor`, `gZelda3dPendingModel`
   - `gZelda3dPendingScale`, `gZelda3dPendingGroundOff`
   - `gZelda3dPendingBoneMap` (precomputed N64↔OoT3D limb correspondence, may be NULL)
   Returns 0 → the actor's own `draw()` runs.
2. **Intercept at SkelAnime choke points** (`z_skelanime.c` L333, L353, L404, L525,
   L698, L828). Each choke point calls `Zelda3D_SkelAnimeDraw` (SkelAnime*) or
   `Zelda3D_SkelAnimeDrawRaw` (skel+jointTable), which:
   - Bails if no pending replacement (returns 0 → N64 limb draw runs)
   - Reads live pose from `jointTable`
   - Reads live anim state from SkelAnime (`animation`, `curFrame`, `animLength`,
     `morphWeight`) — CSAB phase-lock inputs
   - Calls `Zelda3D_DoRetarget(play, skel, jointTable, limbCount)` — emits the
     OoT3D skinned draw
3. **Clear pending** in `Zelda3D_AfterActorDraw`.
4. **Collider re-walk** — `gZelda3dColliderPass=1` suppresses replacement so the
   N64 limb walk runs purely for `Collider_UpdateSpheres` side-effects (#107).

Support systems used by (2):
- **CSAB** (Grezzo animation file) parser
- **BoneMap** = `Zelda3DBoneMap` per-archive precomputed limb correspondence
- **AutoModel*** helpers (`AutoModelSkinned`, `AutoModelMinY`, `AutoModelHeight`)
  that expose CMB metadata to the deferral path

## MVP for MM (T-pose first, animate later)

**Order matters — each stage lands independently.**

### Stage 1 — T-pose skinned draw (no anim, no retarget)
Prove skinned CMB rendering end-to-end using MM3D bind-pose bones only.

- Loosen `mm3d_model.cpp::resolveModelForObject` to also accept `bones() > 1`,
  BUT push into a separate `g_skinnedModels` bucket so the static draw path
  can't accidentally render them.
- Add `Zelda3D::MakeGlSkinnedGroup` (or extend `MakeGlGroup`) in cmb3d to emit
  bind-pose skinning (per-vertex boneIdx + weight already parsed by `Cmb`).
- Add a `Zelda3D_TryDrawSkinnedActor` mirror in `mm3d_draw.c` that emits with
  rest-pose bones. **This lets us SEE the model in-scene, at least in T-pose.**
- Sanity: pick a low-bone actor first — `obj_box` (0x00C, 3 bones — treasure
  chest) is the trivial target.

### Stage 2 — SkelAnime intercept (drive OoT3D bones from N64 joints)

> **STATUS (2026-07-17 code read): Stage 2 is WIRED and LIVE (gated).** `Zelda3D_TryDrawActor`
> (mm3d_draw.c:158) defers skinned actors → `Zelda3D_MM_SetPending`; the SkelAnime hook
> (`Zelda3D_MM_SkelAnimeDrawRaw`, called at mm3d_draw.c:131) runs `mmUpdateAnimN64` (mm3d_model.cpp:350
> — retarget from the live N64 jointTable, Rz·Ry·Rx, identity bone→limb map) then
> `Zelda3D_MM_EmitModelDraw`. Verified live: with `ZELDA3D_MM_SKINNED_TPOSE=1`, skinned MM3D archives
> load + go through this path (run log `[MM3D] skinned-tpose obj=0x00C (box) -> modelId (skinned, 3
> bones)`). It is NOT stuck at Stage-1 bind-pose. It stays **gated off by default** because the
> **identity bone→limb map** isn't ship-quality for rigs whose CMB bone order diverges from the N64
> limb order (mm3d_model.cpp:377 flags the per-archive bone-map as the fix). NEXT: frame a complex
> skinned NPC/enemy with the gate on and grade the identity retarget — where it mis-poses, add a
> per-archive `BoneMap` (the [[soh3d-n64anim-retarget]] per-bone-correction pattern from OoT).
>
> **Partial grade (2026-07-17):** the only skinned MM3D actor in default Clock Town (scene 111) is
> obj 0x00C `box` (3 bones) — it renders CORRECTLY through the Stage-2 path (a properly-posed 3DS
> treasure chest, scratch/screenshots/mm3d_box_skinned.png), confirming skinned CMB load+draw+retarget
> works end-to-end for a SIMPLE rig. But a closed chest is near-static, so it does NOT exercise the
> identity-map on a divergent ANIMATED rig. Grading that still needs an MM scene with a complex
> animated skinned enemy — reaching one requires an MM entrance number (derive from the MM entrance
> table; Termina Field / a swamp enemy scene), the concrete blocker for the full grade.

Now pose the skinned model from the LIVE N64 animation.

- Add `MM_Zelda3D_SkelAnimeDraw{,Raw}` + `MM_Zelda3D_SetLimbOverride` +
  `gMmZelda3dPending{Actor,Model,Scale,GroundOff,BoneMap}` (or share via a
  libultraship shim if OoT-side gets refactored first).
- Wire the six SkelAnime choke points in `Shipwright/mm/src/code/z_skelanime.c`.
  Structure exactly mirrors OoT — copy the `Zelda3D_SetLimbOverride` + `if(...)
  return;` bookends.
- Identity retarget first (no bone-map): assume N64 limb order == CMB bone
  order. Where it visibly breaks, we'll add per-archive `BoneMap`s.
- CSAB still not touched — anim frames come from N64 jointTable only. This is
  sufficient because SkelAnime already computed the interpolated pose.

### Stage 3 — Colliders, ground offset, scale calibration
Once actors pose correctly:

- Port `gZelda3dColliderPass` re-walk (#107 fix — replaced enemies fly off
  without it).
- Auto-scale via `n64_measured_height / MM3D_height` (mirror
  `Zelda3D_AutoModelHeight` for MM).
- Per-archive scale entries land in `g_models` so `mscale`/`mlist` calibrate
  skinned actors too.

### Stage 4 (later, larger) — CSAB anim
- CSAB parser (small — Grezzo format is documented in cmb3d already for OoT)
- Per-actor N64→CSAB anim map (hand-tuned; OoT started tiny and grew)
- Phase-lock to N64 `curFrame` / `animLength`

## Files to touch (MVP)

| File | Change |
|---|---|
| `Shipwright/cmb3d/asset/cmb_glgroups.{h,cpp}` | `MakeGlSkinnedGroup` (bind-pose) |
| `Shipwright/mm/2s2h/zelda3d/mm3d_model.{h,cpp}` | Accept skinned; expose bone table |
| `Shipwright/mm/2s2h/zelda3d/mm3d_draw.c` | `Zelda3D_TryDrawSkinnedActor` |
| `Shipwright/mm/2s2h/zelda3d/mm3d_skel.{h,cpp}` NEW | SkelAnime intercept + retarget |
| `Shipwright/mm/src/code/z_skelanime.c` | 6 intercept sites (copy OoT pattern) |

## Non-goals for this port
- Do NOT reimplement OoT's full 6.5k-line `zelda3d.c`. That's not the substrate
  we need; MM should get a MINIMAL parallel that grows the same way OoT's did.
- Do NOT push shared code into libultraship yet. Cross-copy first; unify once
  both sides work. Premature abstraction bites.

## Verification per stage
- Stage 1: visual — `obj_box` renders where the chest actor is. Screenshot A/B.
- Stage 2: play a scene with an NPC; pose animates. Screenshot per anim state.
- Stage 3: run the collider regression that #107 introduced.
- Stage 4: motion-parity harness (existing OoT one) — port to MM oracle.
