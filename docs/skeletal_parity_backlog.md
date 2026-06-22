# Skeletal-actor parity backlog (CSAB auto-draw path vs OoT3D)

READ-ONLY parity audit, 2026-06-22. Assesses what the new shared draw-override framework
(`src/soh3d/soh3d_anim_override.{h,cpp}` + the `SoH3D_SetBonePostRot` post-rotation channel +
`Csab::skinMatricesMorph` morph cross-fade) STILL DROPS, per skinned NPC actor, relative to OoT3D.

## How the new framework works (what it covers / what it cannot)

The CSAB auto-draw path is `SoH3D_DoRetarget` auto branch (`soh3d.c:2148-2249`). Per draw it:
1. picks a CSAB from the live N64 anim (`SoH3D_ResolveAutoCsab`) and samples a raw `(anim, frame)` pose,
2. replays procedural OverrideLimbDraw rotation deltas (cucco wing-flap only) via
   `SoH3D_ApplyProcOverride` → `SoH3D_SetBoneRotDelta` (`kSoH3dProcOverride`, `soh3d.c:260`),
3. NEW: applies head/torso TRACKING via `SoH3D_ApplyActorOverrides` → `SoH3D_SetBonePostRot`
   (`kTrackActors`, `soh3d_anim_override.cpp:48`), keyed by OoT3D model ZAR,
4. NEW: morph cross-fades transitions globally via `gSoH3dPendingMorphWeight` → `skinMatricesMorph`.

What the framework CANNOT yet express (the gap dimension):
- **There is NO material / texture-segment / per-limb-DL override channel.** Grep confirms the only
  per-bone channels are `SetBoneRotDelta` (rotation) and `SetBonePostRot` (rotation). The skinner
  (`soh3d_model.cpp:2129-2145`) stores only 3x3 post-rotations. Eye-blink / mouth material anim
  (`gSPSegment(0x08/0x09/0x0A)` in the actors' limb-draw) and held-item DL swaps (Saria's ocarina)
  are GL-drawn from the static CMB and never executed → **dropped for every actor**. This is the
  single biggest missing extension and blocks several rows below.
- The track table is keyed by raw byte offset into the **N64** actor struct (`interactOff + 0x08`
  headRot / `+0x0E` torsoRot, Vec3s). Adding an actor = one `TrackActor` row, but you need that
  actor's `NpcInteractInfo` offset + the OoT3D head/torso bone ids + the torso Matrix_Rotate style.

## Strategic context (from `oot3d-decomp/docs/divergence_map.md`)

OoT3D skeletal-actor *logic* is byte-faithful to N64 (En_Ko, En_Sa proven). So finishing this ONE
framework — rotation overrides (done) + **material/segment/DL overrides (NOT done)** + morph (done) —
is expected to fix the VAST MAJORITY of skeletal-actor visual bugs across ALL actors with no
per-actor logic porting. The remaining per-actor work is only for 3DS-divergent / 3DS-exclusive actors.

---

## Prioritized gaps

### P0 — Add the material/segment/DL override channel to the framework (blocks everything facial)
**Impact: very high, broad.** Affects EVERY skinned NPC drawn through the CSAB path. Right now every
Kokiri, Saria, every townsperson has a **frozen face** (no eye-blink, no mouth movement) and any
actor with a held-item or limb-DL swap is missing it. The current framework is rotation-only.

- Concrete next step: extend the framework with a per-limb override of two kinds, applied at GL emit
  time (where `SoH3D_EmitModelDraw` / the CSAB skinner builds the draw):
  1. **Texture-segment override** — map the OoT3D model's eye/mouth material(s) to a per-frame
     texture index driven by the actor's blink/talk state (the N64 actors animate this by writing
     `gSPSegment(0x08/0x09/0x0A, ...)` to point at the current eye/mouth texture). Needs: which
     OoT3D CMB material slot is the eye / the mouth, and the actor's blink-state byte offset.
  2. **Per-limb DL swap** — let a bone optionally GL-draw an extra CMB mesh group (held item /
     alt-limb), e.g. Saria's ocarina. Needs: bone id + which CMB mesh group / external ZAR.
- Where I lack OoT3D ids: the eye/mouth material slot ids and blink-state offsets per actor are not
  yet derived (separate decomp agent). Frame the channel generically (a small table keyed by ZAR,
  paralleling `kTrackActors`) so rows can be filled as ids land.

### P1 — Saria (En_Sa, `zelda_sa.zar`): confirm track bones + add facial + ocarina DL
**Impact: high, story-critical NPC, framed up close in cutscenes/Lost Woods.**
- Head/torso tracking row EXISTS (`kTrackActors` `{0x1E0, 10, 9, torsoStyle 1}`) but the comment
  flags the bone ids as **unconfirmed** ("sa bone ids assume the same layout as km1 — re-confirm
  against the sa skeleton when first exercised in the Meadow"). interactInfo @ 0x1E0 (z_en_sa.h).
  - Next step: live-verify head=10 / torso=9 against the `zelda_sa` skeleton (bonestats), confirm
    `torsoStyle 1` (RotateY(torsoRot.y)·RotateX(torsoRot.x)) matches `En_Sa_OverrideLimbDraw`.
- **Facial anim DROPPED** (blink/mouth) — needs the P0 material channel.
- **Ocarina held-item DL DROPPED** — `En_Sa_Draw` (z_en_sa.c:822-829) does a per-limb DL swap on
  limb 15 (the ocarina in hand during the song). Needs the P0 DL-swap channel + the OoT3D bone id
  for Saria's right hand and the ocarina mesh.
- Morph: handled globally now; no actor-specific concern noted.
- Decomp anchors available if per-actor work is needed: init 0x168504, draw 0x1b9358,
  update 0x1b9450, OverrideLimbDraw 0x23bca4, PostLimbDraw 0x21e968, ACTOR_EN_SA=0x146.

### P2 — Kokiri kids (En_Ko, `zelda_km1` boy / `zelda_kw1` girl): facial + track confirm
**Impact: high — Kokiri Forest is the first populated area; many kids, all framed at NPC distance.**
- Head/torso tracking rows EXIST and bones are CONFIRMED (km1: head=10, torso=9; interactInfo @ 0x1E8;
  torsoStyle 0 = RotateX(-torsoRot.y)·RotateZ(torsoRot.x)). kw1 assumes km1 layout — re-confirm.
  N64 limb-draw reference: `EnKo_OverrideLimbDraw` (z_en_ko.c:1336-1347), limb 8 torso / limb 15 head.
- **Facial anim DROPPED** (kids blink / talk) — needs P0 material channel + km1/kw1 eye/mouth slots.
- No held-item DL noted for the standard kids.
- Note: per-ENKO_TYPE head-variant mesh selection + sit/stand anim overrides are already handled
  (`SoH3D_EnKoMidMask`, `SoH3D_EnKoCsabOverride`); those are separate from this facial gap.

### P3 — Adult townsfolk / carpenters (En_Hy family: `zelda_boj`, `zelda_ahg`, `zelda_aob`, `zelda_daiku`, `zelda_bba`, `zelda_bji`, `zelda_bob`, `zelda_cob`, `zelda_cow`)
**Impact: high by COUNT (Market/Kakariko crowds) — they're the most numerous on-screen NPCs.**
- These share the N64 En_Hy bank and the `object_os_anime` generic anim entries; `zelda_boj` has a
  bonemap row (`soh3d_bonemap.inc:31`). They are drawn through the same auto CSAB path.
- **Head/torso tracking: NO track rows exist for any of them.** En_Hy NPCs head-track the player in
  OoT3D. Each needs a `kTrackActors` row.
  - Next step per actor: `{zar, interactOff (En_Hy NpcInteractInfo offset), headBone, torsoBone, torsoStyle}`.
  - Ids NOT yet available: the En_Hy interactInfo offset and the per-body-zar head/torso bone ids are
    undrived (separate decomp agent). En_Hy bodies are interchangeable rigs, so one offset likely
    covers the whole family, but the head/torso bone ids must be checked per zar (different skeletons).
- **Facial anim DROPPED** — needs P0 material channel.
- Carpenters (`zelda_daiku`) carry/swing tools in some anims; check for per-limb DL swaps (P0 DL channel).

### P4 — Other framed humanoid NPCs with likely head-track + facial (lower frequency, still visible)
**Impact: medium — each appears in specific scenes, often in dialogue (so facial matters).**
Wired up with ZARs + anim entries but NO track rows and no facial:
- `zelda_md` (Mido), `zelda_ma1`/`zelda_ma2` (Malon child/adult), `zelda_in` (Ingo), `zelda_mu`
  (market guards/runners), `zelda_zl1`/`zelda_zl2`/`zelda_zl4` (child Zelda — cutscene, close-up,
  facial important), `zelda_ru1`/`zelda_ru2` (Ruto), `zelda_im` (Impa), `zelda_du`/`zelda_gr`/`zelda_ka`
  (Gorons/Zora/etc.), `zelda_fu`, `zelda_gla`.
- Per actor: add a `kTrackActors` row if the N64 actor head-tracks (most NPCs do via `Npc_UpdateTalking`/
  interactInfo), plus the P0 facial channel. Bone ids + interactInfo offsets per actor are undrived.
- child Zelda (`zelda_zl4`) already has a scale calibration (`soh3d.c:2167`); a cutscene close-up makes
  her frozen face especially visible — promote her toward P2/P3 once cutscene framing is exercised.

### P5 — Morph: global handling — note any actor-specific concern
**Impact: low residual.** Morph cross-fade is now global (`gSoH3dPendingMorphWeight` →
`skinMatricesMorph`), driven from `skelAnime->morphWeight` at the choke point (`soh3d.c:2281`). No
per-actor override needed in the common case. Concerns to watch (not yet bugs):
- Actors whose OoT3D CSAB transition set differs from N64 (anim-id divergence, e.g. En_Ko #87 where
  OoT3D plays distinct anims) may morph between a different anim pair than N64 — morph is correct
  mechanically but the endpoints can differ. This is an anim-MAP issue, not a morph-channel issue.
- The raw (SkelAnime-less) `SoH3D_SkelAnimeDrawRaw` choke point has no playhead/morphWeight → those
  actors free-run with morphWeight 0 (hard cut). If a numerous NPC draws only through the raw path,
  its transitions won't morph. Worth auditing which NPCs hit raw vs SkelAnime* path.

---

## Summary table

| Actor / family | Track row? | Facial (P0)? | Held-item DL? | Top action |
|---|---|---|---|---|
| (framework) | — | **MISSING** | **MISSING** | P0: add material/segment/DL channel |
| En_Sa Saria | yes (bones unconfirmed) | dropped | ocarina dropped | confirm bones; facial + ocarina DL |
| En_Ko km1/kw1 | yes (km1 confirmed) | dropped | — | facial; confirm kw1 bones |
| En_Hy boj/ahg/aob/daiku/… | **none** | dropped | carpenter tools? | add track rows + facial |
| md/ma/in/zl/ru/im/… | **none** | dropped | varies | add track rows where they head-track + facial |
| morph | global (done) | — | — | audit anim-id divergence + raw-path actors |

## What I could NOT determine (needs the decomp agent's bone-id output)
- OoT3D eye/mouth material slot ids and per-actor blink/talk state offsets (blocks ALL facial rows).
- En_Hy `NpcInteractInfo` offset + per-body-zar head/torso bone ids (blocks P3 track rows).
- Saria right-hand bone id + ocarina mesh group (blocks the ocarina DL).
- Confirmation of `zelda_sa` / `zelda_kw1` head=10/torso=9 against their actual skeletons.
- The `scratch/align/saria_en_sa_compare.md` referenced in the audit brief does not exist in the
  tree yet (decomp agent still producing it); facts above are sourced from `divergence_map.md`.
