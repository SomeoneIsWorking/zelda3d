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
>
> **Entrance resolved + first drive (2026-07-17):** `ENTRANCE(scene,spawn) = ((ENTR_SCENE_##scene &
> 0x7F) << 9) | ((spawn & 0x1F) << 4)` (z64scene.h:755); `ENTR_SCENE_TERMINA_FIELD = 0x2A`, so
> **Termina Field spawn 0 = `0x5400`**. `ZELDA3D_MM_SKINNED_TPOSE=1 tools/mm_game.py start 0x5400`
> boots there (scene 45), MM3D provider live (rigid scene objects obj_tokeidai/keikoku_obj/fall map +
> render). BUT the entrance-corridor spawn is **enemy-sparse — the run log shows ZERO skinned-MM3D
> archive loads** (no `[MM3D] skinned-tpose ...`), so nothing skinned rendered to grade. The 12
> nearest actors are mostly obj=0x001 (gameplay_keep) + a few scene props; none pulled a skinned
> `zelda2_*.gar.lzs`. NEXT: either navigate/`tp` Link to a confirmed skinned enemy within Termina
> Field (Leever/Chuchu/Guay), OR pick an enemy-dense entrance (Woodfall/swamp), OR check whether the
> nearby enemy OBJECTS even have MM3D archives (if not, that's a separate auto-map-coverage gap). Only
> once a skinned enemy actually loads (`skinned-tpose` log line) can the identity retarget be graded.
>
> **BLOCKER FALSIFIED + first real grade — POSITIVE (2026-07-17, later):** the "reach a scene with a
> complex animated skinned rig" blocker was FALSE. **Default South Clock Town (scene 111, no entrance
> arg) already loads multiple complex skinned MM3D rigs** under `ZELDA3D_MM_SKINNED_TPOSE=1` — run log
> (`scratch/logs/mm_n2/run_mm.log`): `dog` obj=0x132 **12 bones**, `an1` obj=0x0E2 **20 bones**, `mm`
> obj=0x107 **15 bones**, `sdn` obj=0x1B6 **16 bones** (plus box/pst/lodmoon). No Termina Field / enemy
> hunt needed. **Grade of the identity retarget = POSITIVE (gross):** screenshots
> `scratch/screenshots/{skinned_grade_sct,dog_grade,dog_closeup}.png` show the dog rendering as a
> coherent posed quadruped (alert stance, moves around the plaza live — its N64 actor updates) and a
> Clock Town humanoid NPC rendering in a **dynamic animated pose (arms spread wide, one leg bent — a
> gesture/dance idle)** — NOT a T-pose, NOT bind-pose, NOT mangled. So Stage-2 (N64-joint-driven) posing
> + the identity bone→limb map produce correct-looking poses on complex animated humanoid (15–20 bone)
> AND quadruped (12 bone) rigs — the divergent-bone-order fear did not materialize on these. (The
> `[MM3D] skinned-tpose ... (Stage 1 bind-pose draw)` log TEXT is stale — the actual draw is the wired
> Stage-2 path; the dynamic NPC pose proves it.)
>
> **Stage-2 CODE-VERIFIED (2026-07-17, follow-up):** confirmed by reading the path, not just the
> screenshot — the intercept is wired at THREE SkelAnime choke points (`2ship/src/code/z_skelanime.c`
> :331/:454/:601 → `Zelda3D_MM_InterceptSkelAnime`), and `mmUpdateAnimN64` (mm3d_model.cpp:350) poses
> each CMB bone from the LIVE N64 `jointRots[limb*3..]` (binang→rad, `Rz·Ry·Rx`, parent-walk, skin =
> `world·inverse(bind)`) with the identity `limb = bone.id` retarget. So the visible rigs are genuinely
> live-animated MM3D skinned CMBs — the grade is now code-verified, not just visual. Fixed the stale
> `mm3d_model.cpp:258-275` comment + log (said "Stage 1 T-pose / not yet wired / T-pose ugly" — all
> stale; now "Stage-2 live-posed draw", with the legacy env-var name noted).
>
> NEXT (rigorous grade — oracle A/B is INFEASIBLE right now): there is **no MM3D oracle tooling** (all
> `tools/oracle_*` are OoT3D-only) and frame-matching *animated, moving* actors between two runs is
> unreliable — so "oracle A/B" needs MM oracle infra built first (a separate effort). The feasible,
> deterministic rigorous grade instead is a **static skeleton-topology check**: for each skinned
> archive, compare the CMB bone hierarchy (parent-index array / bone order) against the N64 actor's
> FlexSkeleton limb hierarchy — where they match, the identity map is provably correct; where they
> diverge, that archive needs a per-archive `BoneMap` (replacing mmUpdateAnimN64's identity
> `limb = bone.id`). Plus a broader rig/scene survey (enemy-dense scenes) to catch any grossly-divergent
> rig. Once identity is validated (or bone-maps added) across the common rigs, **enable the gate by
> default** — the old "T-pose looks worse than N64" reason is gone (it live-poses correctly now).
>
> **TOPOLOGY GRADE — tool built, identity map is DIVERGENT; earlier "positive grade" OVERTURNED
> (2026-07-17).** Built the deterministic grader (env `ZELDA3D_MM_SKINNED_TOPO=1`): at the SkelAnime
> intercept it reconstructs the live N64 limb-parent array from the child/sibling tree
> (`Zelda3D_MM_SkelParents`, mm3d_draw.c) and compares it index-for-index against the CMB bone
> hierarchy (`Zelda3D_MM_GradeTopology`, mm3d_model.cpp), logging `[MM3D-TOPO]` once per archive.
> Result (South Clock Town, `scratch/logs/mm3d_topology_grade.txt`) — **the identity `limb = bone.id`
> retarget is DIVERGENT on EVERY complex rig**:
> - `pst` 2 bones: OK (trivial).
> - `sdn` 16/16: DIVERGENT, 4 mismatched parents (limbs 10,12,13,15).
> - `dog` 12/12: DIVERGENT, **8 of 12** mismatched (limbs 3–10).
> - `an1` 15 N64 limbs vs **20** CMB bones: DIVERGENT, 7 mismatches + 5 extra CMB bones.
> - `mm` 16 N64 limbs vs **15** CMB bones: count mismatch (N64 limb 15 has no CMB bone).
>
> So **the CMB bone order ≠ the N64 limb order** — `jointRots[bone.id]` is applied to the WRONG bone,
> and bone counts don't even match on `an1`/`mm`. **This DIRECTLY REFUTES the earlier "grade POSITIVE
> / divergent-bone-order fear did not materialize" note above** (commits 4483db65, 7f9b8ab7): that was
> a low-confidence VISUAL grade of small/distant rigs — the poses looked plausible but are actually
> mis-posed; the eyeball missed it. The tool is ground truth. **Per-archive `BoneMap`s ARE required**
> (the divergent-topology risk the code always flagged at mm3d_model.cpp is real). Do NOT enable the
> gate by default yet. NEXT: build a per-archive N64-limb→CMB-bone map — match body parts across the
> two skeletons (by hierarchy shape + rest-position, since CMB bones carry no names), replace
> mmUpdateAnimN64's identity `limb = id` with the map, and re-run `[MM3D-TOPO]` (should report OK) +
> a visual re-check. The `an1`/`mm` bone-count deltas need handling too (extra/absent bones).

> **FIXED — auto-derived retarget map, all rigs now hierarchy-consistent (2026-07-17).** The
> divergence is NOT a re-rig (my earlier CSAB worry was wrong): dumping the full both-side structure
> (`ZELDA3D_MM_SKINNED_TOPO=1`, positions added to `[MM3D-TOPO]`) showed the N64 `jointPos` and CMB
> `trans` are in the SAME scale and the two skeletons are a **clean bijection that preserves the
> hierarchy** — a pure reordering (dog: n64 limb2↔cmb bone4, limb3↔bone7, … verified by both parent
> AND identical local rest-position; idx0/1/11 already identity). So `Zelda3D_MM_BuildRetargetMap`
> (mm3d_model.cpp) auto-derives the correspondence per archive: walk the CMB tree parent-first and,
> among N64 limbs whose already-mapped parent matches, pick the one whose local rest-position is
> nearest. mmUpdateAnimN64 now uses `boneToN64[bone.id]` instead of identity `limb = id`. Result
> (`scratch/logs/mm3d_retarget_result.txt`) — **every rig maps hierarchy-consistently**: dog 12/12,
> sdn 16/16, mm 15/15 FULL/CONSISTENT (all previously DIVERGENT); an1 13/20 mapped + consistent (the
> 7 extra Grezzo detail bones have no N64 limb → correctly stay at bind rot). The self-check
> (`n64Parent[map[B]] == map[cmbParent[B]]` for every mapped bone) passes 100%. Visual: rigs render
> clean (`scratch/screenshots/retarget_dog.png`), no mangling/T-pose. NEXT: broader enemy-scene survey
> to confirm the auto-map holds game-wide (structurally-identical siblings with equal rest-positions —
> e.g. symmetric limbs — are matched arbitrarily among themselves, harmless when interchangeable but
> worth a spot-check on a rig with distinct-but-mirrored limbs), then **enable the gate by default**.

Now pose the skinned model from the LIVE N64 animation.

- Add `MM_Zelda3D_SkelAnimeDraw{,Raw}` + `MM_Zelda3D_SetLimbOverride` +
  `gMmZelda3dPending{Actor,Model,Scale,GroundOff,BoneMap}` (or share via a
  libultraship shim if OoT-side gets refactored first).
- Wire the six SkelAnime choke points in `2ship/src/code/z_skelanime.c`.
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

### Stage 4 — CSAB anim — **THIS IS THE ARCHITECTURE (landed 2026-07-17)**

> **The Stage-2 "retarget N64 joints onto CMB bones" path was the WRONG architecture and has been
> REMOVED.** MM3D actor GARs ship the actor's OWN 3DS animations (`.csab`) next to the `.cmb`
> (`zelda2_dog.gar.lzs` → `dog_wait/walk/run/bark/sit/...`). There is nothing to retarget: play the
> 3DS clip on the 3DS rig, exactly as OoT's soh3d layer does. See
> `debug_journal/2026-07-17-mm-skinned-csab-architecture.md`.

**Landed + verified (dog renders posed by its own `dog_wait`, scratch/screenshots/mm_dog_csab_mapped.png):**
- **CSAB parser** — the shared `cmb3d/asset/csab.cpp` now parses MM3D **subversion 5** ("Majora")
  as well as OoT3D subversion 3. Only header offsets + anod base differ (0x24 vs 0x18); anod/track
  layout identical (noclip csab.ts). Validated: dog 12/12, an1 37/37, dnt 19/19 — bone counts match CMBs.
- **Capture** live N64 anim state in `SkelAnime_Update`/`PlayerAnimation_Update`
  (`Zelda3D_MM_CaptureAnimState`, keyed by jointTable) — MM has no `SkelAnime*` at the draw choke.
- **Resolve** N64 anim OTR → 3DS CSAB via `kMMAnimMaps` (`__OTR__` stripped) + data-driven default idle.
- **Phase-lock** the CSAB to N64 `curFrame`/`animLength`; sample via `Csab::skinMatrices`; upload.

**Next (incremental, same as OoT grew):** fill `kMMAnimMaps` per actor from the `[MM3D-ANIM]` harvest
log; port the morph cross-fade + walk-stop synthetic morph; then flip `ZELDA3D_MM_SKINNED` on by default.

## Files to touch (MVP)

| File | Change |
|---|---|
| `Shipwright/cmb3d/asset/cmb_glgroups.{h,cpp}` | `MakeGlSkinnedGroup` (bind-pose) |
| `2ship/2s2h/zelda3d/mm3d_model.{h,cpp}` | Accept skinned; expose bone table |
| `2ship/2s2h/zelda3d/mm3d_draw.c` | `Zelda3D_TryDrawSkinnedActor` |
| `2ship/2s2h/zelda3d/mm3d_skel.{h,cpp}` NEW | SkelAnime intercept + retarget |
| `2ship/src/code/z_skelanime.c` | 6 intercept sites (copy OoT pattern) |

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

> **Drive #2 (2026-07-17) — MM3D rigid actor rendering CONFIRMED live in gameplay; still no skinned rig
> to grade.** `tp` works (the earlier "tp broken" was a false read — (-2698,48,-1139) was a non-walkable
> enemy position that voided Link back; tp to walkable spots moves him fine, Y auto-snaps to floor).
> Drove Link across Termina Field via `tp`; the MM3D provider mapped + rendered these actors, **ALL
> rigid (1 bone)**: slime (obj 0x16A), bigicicle (0x1AD), bombiwa (0x12A), gs (0x143), plus scene props
> (obj_tokeidai/keikoku_obj/fall). So MM3D actor substitution is live in real gameplay — but Termina
> Field's common enemies are rigid, so NONE exercises the skinned retarget. (Also: the slime rendered
> visibly mis-scaled at the 0.1 default — a Stage-3 scale-calibration TODO, `scratch/screenshots/
> mm_termina_slime.png`.) NEXT for the skinned grade: identify which MM3D archives are >1 bone from
> `scratch/mm3d_actor_archives.md`, then reach an actor using one — a humanoid NPC (Clock Town
> townsfolk) or a humanoid enemy (Garo/Stalchild/ReDead/dungeon boss), NOT the rigid field creatures.

> **Drive #3 (2026-07-17) — skinned humanoid/animal rigs CONFIRMED loading + rendering in Clock Town.**
> Booted default South Clock Town (scene 111) with `ZELDA3D_MM_SKINNED_TPOSE=1`. The run log shows real
> multi-bone skinned archives loading through the MM3D retarget path: **obj 0x0E2 `an1` (20 bones,
> humanoid NPC)**, obj 0x1B6 `sdn` (16 bones), obj 0x132 `dog` (12 bones), obj 0x223 `lodmoon` (2), obj
> 0x00C `box` (3). So the earlier "only the box is skinned in Clock Town" was too pessimistic — full
> humanoid rigs are present here. Framing recipe that works: `tp` Link adjacent to the actor (positions
> from `actors`), then `cam <yaw> <dist> <h>` (cam orbits Link, so put the target beside him). Framed the
> `dog` (obj 0x132) at ~125u: it renders in-frame **without mangling/explosion** (scratch/screenshots/
> mm_ct_dog.png) — a preliminary pass for the identity retarget on that rig. NEXT for a definitive grade:
> A/B each rig gate-on vs gate-off (isolate MM3D-vs-N64), and get a close frame of the 20-bone `an1`
> humanoid (the divergent-rig stress test) — where the identity map mis-poses, add a per-archive BoneMap.
