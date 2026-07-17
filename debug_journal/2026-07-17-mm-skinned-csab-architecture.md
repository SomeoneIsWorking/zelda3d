# MM3D skinned actors — replace the N64-joint retarget with the real CSAB architecture

**Date:** 2026-07-17
**Outcome:** MM3D skinned actors now play their OWN 3DS CSAB animations on their OWN 3DS
skeletons, mirroring OoT's architecture. The prior "retarget N64 joint rotations onto the
CMB bones" path (last several commits: identity map → auto-derived bone-map → topology
grader) is REMOVED. Verified live: the `dog` (obj 0x132) renders as the MM3D terrier posed
by its own `dog_wait` 3DS idle, clean (scratch/screenshots/mm_dog_csab_mapped.png).

## Why the retarget was the wrong architecture (user correction)

The retarget drove the 3DS CMB bones from the live N64 `jointTable` rotations. It required a
per-archive N64-limb ↔ CMB-bone correspondence. A broader survey (21 rigs across 8 scenes)
showed the auto-derived parent-preserving map only fully covered rigs whose N64 and 3DS
skeletons are hierarchy-preserving bijections (dog/sdn/mm/dnk/... ), and cascaded to partial
coverage on rigs Grezzo restructured (dnt 8/27, pp 8/29, ssh 3/14) — and even a "full" map
can't correctly transfer local rotations across differing parent chains.

**The whole exercise was misguided.** The MM3D actor GARs ship the actor's OWN 3DS animations
(`.csab`) right next to the `.cmb` — e.g. `zelda2_dog.gar.lzs` has `dog_wait/walk/run/bark/
sit/jump/...`. There is nothing to retarget: play the 3DS clip on the 3DS rig. This is exactly
what OoT's soh3d layer already does (`Zelda3D_UpdateAnim` + the auto CSAB path + `kZelda3dAnimMaps`).
User directive: "use 3ds assets, bones and animations… use the same architecture, one already existed."

## The architecture (mirrors OoT)

1. **Capture** the live N64 anim state in `SkelAnime_Update` / `PlayerAnimation_Update` (the only
   MM entry points that carry the `SkelAnime*`): `Zelda3D_MM_CaptureAnimState(jointTable, animation,
   curFrame, animLength, morphWeight)`, keyed by the stable `jointTable` pointer.
   - `skelAnime->animation` is the ogAnim OTR path string (2s2h keeps it there deliberately,
     `PlayerAnimation_Change`/`SkelAnime_Change` line ~1517/1957, "to handle comparisons to other
     animation resources"), so `(const char*)animation` is the animmap key — same trick as OoT.
   - MM lacks OoT's `SkelAnime_DrawSkeletonOpa(SkelAnime*)` wrapper; its actors call the low-level
     `SkelAnime_DrawOpa(skeleton, jointTable)` directly, so the draw choke has no `SkelAnime*`. Hence
     the capture-in-Update + lookup-by-jointTable split.
2. **Resolve + drive** at the draw choke (`Zelda3D_MM_SkelAnimeDrawRaw`): look up the captured state
   by jointTable, map the N64 anim OTR → a 3DS CSAB base name (`kMMAnimMaps`, `__OTR__` prefix stripped;
   unmapped → the model's data-driven default idle = a `*wait*` clip), phase-lock the playhead
   (`csab_frame = (n64Cur/n64Len)·csab_duration`, free-run at 1 fps/draw when length is unusable),
   sample via the shared `Zelda3D::Csab::skinMatrices`, upload bind + skin matrices.
3. Removed: `mmUpdateAnimN64`, `Zelda3D_MM_BuildRetargetMap`, `Zelda3D_MM_GradeTopology`,
   `Zelda3D_MM_SkelParents`, `Loaded::boneToN64`, and the `mat4.h` include (retarget-only).

## The one real blocker: MM3D CSAB is subversion 5, not 3

The shared `Csab` parser (cmb3d/asset/csab.cpp) hard-gated `subver == 3` ("Ocarina"). MM3D CSABs are
**subversion 5** ("Majora") with a shifted header. Per noclip OcarinaOfTime3D/csab.ts, the ONLY
differences are header field offsets + the anod-table base; the `anod` node + track encodings are
identical. Added the branch:

| field            | OoT3D (sub 3) | MM3D (sub 5) |
|------------------|---------------|--------------|
| anod-table base  | 0x18          | 0x24         |
| duration (−1)    | 0x28          | 0x34         |
| anodCount        | 0x30          | 0x3C         |
| boneCount        | 0x34          | 0x40         |
| boneToAnim table | 0x38          | 0x44         |

Validated standalone (`$CLAUDE_JOB_DIR/tmp/csabval`): dog 12/12 clips OK @ 12 bones (matches its CMB),
an1 37/37 @ 20 bones, dnt 19/19 @ 27 bones — every bone count matches the rig, durations sane
(dog_walk=11, run=12, sit=50).

## Why this is correct-by-construction (vs the retarget)

The CSAB samples the rig's OWN bones with its OWN motion data — bone *i*'s track drives bone *i* of the
same skeleton. There is no cross-rig correspondence to get wrong, so it cannot mangle/explode the way
the retarget did on divergent rigs. Worst case for an unmapped state is "plays idle while sliding," not
a broken pose.

## Status / next

- **Landed + verified:** capture → resolve → phase-lock → CSAB sample → upload; subver-5 parse; dog
  renders clean. No crashes; all skinned MM3D actors route through the CSAB path with zero parse errors.
- **`kMMAnimMaps` is seeded only for `dog`** (run/walk/bark/sit; idle via default). It grows per actor
  exactly as OoT's did. The `[MM3D-ANIM] unmapped n64='…' -> default '…'` log harvests the real OTR
  keys — this session captured e.g. `object_daiku/object_daiku_Anim_002FA0` (mm/daiku),
  `ovl_En_Sth/gEnSthLookUpAnim` (an1). Fill those next.
- Still behind the `ZELDA3D_MM_SKINNED=1` env gate (renamed from the stale `_TPOSE`) pending map growth.
- Morph cross-fade + the walk-stop synthetic morph (OoT's `Zelda3D_UpdateAnimAuto` polish) not yet
  ported — add when a mapped transition visibly pops.
