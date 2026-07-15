# Epona mane/tail secondary motion — investigation result: already correct, no fix needed

> **CORRECTION ADDENDUM (2026-07-15, later same day, hoof-dust session):** the "live verification"
> below (visible mane-tuft reorientation between two frames) is almost certainly the NATIVE N64
> Epona mesh's own mane polygon animating via its native N64 anim, not the OoT3D CMB's bone 14. A
> later session (`debug_journal/2026-07-15-epona-hoof-dust-depth.md`) traced the actual draw call
> chain and found `z_skin.c`'s `Skin_DrawImpl` (which `EnHorse_Draw` uses) has ZERO Zelda3D hooks —
> the AUTO-skinned classification `actorsnear` reports for Epona is a static eligibility tag, not
> live confirmation the OoT3D model draws; confirmed live (`ZELDA3D_DUST_DEBUG=1` instrumentation)
> that Epona currently ALWAYS renders as the native N64 mesh (+ HD texture pack) regardless of
> `ZELDA3D_N64ANIM`. The conclusion below ("no fix needed") may still hold once the OoT3D body draw
> is actually wired up (a separate, bigger port — see that journal's remaining-work list) — CSAB
> track coverage for the mane/tail bones was verified via static dump and is presumably still
> correct — but the LIVE-VERIFICATION half of this entry's evidence is retracted; re-verify against
> the actual OoT3D draw once that's wired up, don't trust the old screenshots.

Task ask: drive Epona's mane/tail bones (unmapped by the N64 jointTable retarget) from the
3DS CSAB so they flow instead of sitting in bind pose, either full-CSAB or blended with the
N64 retarget.

## Finding: En_Horse does NOT use the N64-jointTable retarget path at all

Grepped `Shipwright/soh/src/zelda3d/zelda3d.c` for the two retarget mechanisms:

- **N64-jointTable direct retarget** (`Zelda3D_UpdateAnimN64Mapped` / `Zelda3D_UpdateAnimN64Corr`):
  only reachable via a curated `sModelTable[]` entry with `resolveJoints` set (e.g. En_Ge1), or the
  `bm != NULL` "hand-calibrated" branch inside `Zelda3D_DoRetarget`. `object_horse`/`ACTOR_EN_HORSE`
  has **no entry in `sModelTable`** — grepped the whole table, no "horse" hit.
- **AUTO full-CSAB path** (`gZelda3dPendingAuto = 1`, `Zelda3D_UpdateAnimAuto` →
  `Csab::skinMatrices`): the generic object-id→ZAR auto-replace mechanism (kAssemblies /
  `zelda3d_object_zars.inc`) sets `gZelda3dPendingAuto=1` for any actor not in the curated table.
  Epona (`object_horse` → `/actor/zelda_horse.zar`) goes through THIS path — confirmed live via
  `actorsnear`: `id=0x14 ... AUTO:/actor/zelda_horse.zar (skin)`.

The AUTO path selects a CSAB clip via `zelda3d_animmap.inc`'s N64-anim→CSAB table (already has a
full `gEponaIdleAnim`/`WalkingAnim`/`TrottingAnim`/`GallopingAnim`/... row set mapping to
`hl_anim_wait2`/`hl_anim_walk2_30`/`hl_anim_slowrun2_30`/`hl_anim_fastrun2_30`/...) and plays it via
`Zelda3D_UpdateAnim` → `Csab::skinMatrices`, which samples **every bone's own track**, not a subset
gated by N64 limb correspondence. So there is no "N64-jointTable leaves 3DS-only bones in rest pose"
mechanism in play for Epona — that failure mode only applies to the (unused-by-horse) curated
retarget path.

## Confirmed epona.cmb (25 bones) carries real tracks for every bone, incl. mane+tail

Dumped `actor/zelda_horse.zar` (`Model/epona.cmb` + all `Anim/hl_anim_*.csab`) via
`tools/ctr_romfs.py` + `tools/zar.py` + `tools/cmb.py` + `tools/csab.py`. Bone tree (25 bones):

- 0 = root/pelvis
- 1 = withers/chest, 2 = shoulder (children of 1)
- 3-6 / 10-13 = front-left / front-right leg chains (children of 2, offset ±835 in Z)
- 7-9 = neck+head chain (child of 2, Z=0 centerline)
- 14 = single leaf bone off bone 1 (chest), offset (1268,-1763,0) — **the mane bone**: every
  `hl_anim_*.csab` clip checked (`fastrun2_30`, `wait2`, `stand2`, `walk2_30`) carries a `rZ`
  rotation track for bone 14 (only Z animated — a simplified single-bone mane-swing rig), all other
  bones' tracks present too.
- 15-18 / 19-22 = hind-left / hind-right leg chains (children of root, offset ±690 in X)
- 23-24 = 2-segment tail chain (children of root, centerline, Z=-1308 then further -Z) — **the
  tail**: full rX/rY/rZ tracks present on both bones in every clip checked.

No bone in the 25-bone rig is missing a rotation track in `hl_anim_fastrun2_30` (the galloping
clip actually played during the title-cs ride, per `zelda3d_animmap.inc`
`gEponaGallopingAnim → hl_anim_fastrun2_30`). `Csab::skinMatrices`/`sampleLocalTRS` (csab.cpp)
falls back to CMB rest rotation only for a bone with **no present track**, which doesn't happen
here — every bone samples its own authored curve every frame.

## Live verification (title cs, mount segment ~cs1400)

Booted `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh`, `titlecs 1400`+ to reach the rider, framed via
REPL `cam`. Comparing `scratch/screenshots/mane_f1416_zoom.png` vs `mane_f1418_zoom.png` (2 engine
frames apart, same fixed world-space camera): the dark forelock/mane tuft above Epona's head visibly
changes orientation between the two frames (swings from pointing down-left to pointing up) — direct
proof bone 14 is being driven live, not frozen at CMB rest.

(Screenshots are scratch/ — gitignored, not committed, per repo convention.)

## Conclusion

**No code change made.** The described bug (mane/tail stuck in bind pose because only N64-jointTable
bones get driven) does not reproduce: En_Horse was never on the jointTable-retarget path, and the
AUTO/CSAB path it does use already carries + applies authored per-bone tracks for the mane and tail
bones, verified both statically (CSAB track dump) and live (visible frame-to-frame mane motion during
the title-cs gallop). Introducing a second, redundant CSAB-driven secondary-motion system for these
bones would double-drive already-correct bones — a bandaid for a non-problem — so it was not built.

If a future playtest report says the mane/tail still read as visually stiff, the next real
investigation step is A/B AMPLITUDE against the Azahar oracle (same clip, same frame), not
re-deriving which bones are covered — coverage is confirmed complete here.
