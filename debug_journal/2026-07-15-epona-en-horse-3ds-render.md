# En_Horse/Epona now renders as the OoT3D CMB model — Skin_DrawImpl choke point wired

Follow-up to the 2026-07-15 hoof-dust session's honestly-flagged remainder ("wire a Zelda3D
choke point into `Skin_DrawImpl` so Epona can actually draw as its OoT3D CMB model — the
`e->skinned` AUTO classification already exists and is *waiting* for this; right now it's
inert"). This session does exactly that.

## The gap (confirmed, matches the prior session's finding)

`z_skin.c`'s `Skin_DrawImpl` (used exclusively by the `Skin` actor system: En_Horse and its
variants, En_fHG, En_Viewer) never called into `zelda3d.c` — no include, no choke point. Every
other N64-animated actor's draw call chain (`z_skelanime.c`'s `SkelAnime_Draw*` family) already
calls `Zelda3D_SkelAnimeDraw`/`Zelda3D_SkelAnimeDrawRaw` before emitting any N64 limb `Gfx`, so
`Zelda3D_TryAuto`'s "skinned" classification (`e->skinned = 1`, staged via
`gZelda3dPendingActor`/`gZelda3dPendingModel`) only ever got CONSUMED for `SkelAnime` actors.
Full RE writeup: `oot3d-decomp/docs/en_horse_epona_render_gap.md`.

## The fix

`Shipwright/soh/src/code/z_skin.c`:

- `#include "zelda3d/zelda3d.h"`.
- In `Skin_DrawImpl`, right after computing `skeleton` (and after
  `Skin_ApplyAnimTransformations` has already run, so `postDraw` callbacks that read posed limb
  matrices — e.g. `EnHorse_PostDraw`'s hoof-dust `Skin_GetLimbPos` — keep working unchanged):

  ```c
  if (Zelda3D_SkelAnimeDraw(play, &skin->skelAnime)) {
      if (postDraw != NULL) {
          postDraw(actor, play, skin);
      }
      goto close_disps;
  }
  ```

`Skin` embeds a real `SkelAnime skelAnime` member (`z64skin.h`), driven every frame by the
actor's own `SkelAnime_Update`/`Animation_Change` calls exactly like any other SkelAnime-based
actor (`z_en_horse.c` switches `skin.skelAnime.animation` per live gait — idle/walk/trot/gallop/
rear/whinny). So the existing `Zelda3D_SkelAnimeDraw(PlayState*, SkelAnime*)` choke point — the
SAME one `SkelAnime_DrawSkeletonOpa`/`DrawSkeleton2` already call — works completely unmodified:
it reads `skelAnime->animation` (captures the live N64 anim OTR path for the AUTO CSAB gait
map), `curFrame`/`animLength` (phase-lock), `morphWeight`, and retargets `skelAnime->skeleton`/
`jointTable` onto the OoT3D CMB skeleton via `Zelda3D_DoRetarget`. `SkinLimb`'s
`jointPos`/`child`/`sibling` fields share `StandardLimb`'s exact byte offsets (0x00/0x06/0x07),
so the generic N64-limb-tree walk (`Zelda3D_WalkN64Skeleton`) needs no Skin-specific variant.

No changes needed to `Zelda3D_TryAuto`, the AUTO object→ZAR table, or the CSAB gait map — those
were already correct and already staging the replacement; only the missing consumer was added.

**First attempt used `Zelda3D_SkelAnimeDrawRaw(play, (void**)skeleton, skin->skelAnime.jointTable)`**
(the SkelAnime*-less wrapper) — that also renders the OoT3D mesh, but with no `SkelAnime*` it
can't capture `->animation`, so the AUTO branch's CSAB gait map never resolves the live N64
anim name and always falls back to `hl_anim_wait2` (default idle) regardless of actual gait.
Switched to the full `Zelda3D_SkelAnimeDraw(play, &skin->skelAnime)` wrapper before shipping —
confirmed via `animdbg 1` this correctly gait-matches AND phase-locks (see verification below).

## Build

`cmake --build Shipwright/build-cmake --target soh -j4` — two clean builds this session (the
raw-wrapper draft, then the SkelAnime-wrapper final), both succeeded with no warnings from the
touched file. Only one build running at a time, RAM-constrained machine per project rule.

## Verification (live game, headless, `ZELDA3D_HEADLESS=1`)

### Title-cs rider

`ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`, drove the title cs via REPL
`titlecs <frame>` + `freeze`/`step` (the "titlecs pins the cursor but it keeps advancing in real
time" behavior meant naive `titlecs N; shot` calls landed on drifted frames — using `freeze 1`
+ `step 1` between `titlecs` and `shot` made the capture deterministic). A coverage sweep across
the full 0-2400 cs loop (`scratch/screenshots/epona_coverage_sheet.png`, gitignored) located the
rider's visible window around cs frame ~1500-2100 (small distant figure while the wordmark is
still fading, then closer once the logo clears past frame ~2000).

Enabled `animdbg 1` and captured stderr from `scratch/logs/run.log`:

```
SOH3D ANIM: model 2010 n64=__OTR__objects/object_horse/gEponaGallopingAnim -> csab=hl_anim_fastrun2_30 scale=0.01084 n64frame=16.8/24.0 [PHASE-LOCK]
SOH3D ANIM: model 2010 n64=__OTR__objects/object_horse/gEponaRearingAnim -> csab=hl_anim_stand2 scale=0.01084 n64frame=1.0/33.0 [PHASE-LOCK]
```

confirming the OoT3D `epona.cmb` (model 2010, 25 bones) draws live with gait-matched,
phase-locked CSAB — not a frozen bind pose, not a default-idle fallback. `[Zelda3D] auto-loaded
model 2010 (/actor/zelda_horse.zar): cmb 'Model/epona.cmb' of 1, height=9239.5, bones=25` from
the same log confirms the asset load.

Screenshot evidence (`scratch/screenshots/`, gitignored):

- `epona_final_1600_zoom.png` — 6x zoom crop during the title cs (logo still overlaid): the
  rider is visibly the rounded OoT3D model in a galloping pose (legs extended), with a flowing
  mane and tail, not the blocky N64 mesh.
- `epona_close_2000.png` — REPL `asel 0x14` + `acam 250` framing after the wordmark clears:
  unobstructed close-up, clearly the OoT3D CMB Epona (rounded body/head, textured saddle, mane
  and tail both visibly posed off bind-rest) with Link (also OoT3D-model, per the
  2026-07-15 mounted-Link fix) seated correctly.

**Honest judgement**: yes, Epona now visibly renders as the 3DS model with an animated (not
static bind-pose) mane and tail, matching the task's stated success criterion.

### Gameplay regression check

Warped (REPL `warp 0x157` = `ENTR_LON_LON_RANCH_ENTRANCE`) to Lon Lon Ranch, which spawns
`En_Horse_Normal` (`ACTOR_EN_HORSE_NORMAL`, id 0x3C) — a DIFFERENT `Skin`-actor overlay from
En_Horse, sharing only `z_skin.c`'s `Skin_DrawImpl`. `actorsnear 3000` showed
`AUTO:/actor/zelda_horse_normal.zar (skin)`; `animdbg` log showed the same gait-matched
phase-lock behavior across idle/gallop/rear/whinny:

```
SOH3D ANIM: model 2016 n64=.../gHorseNormalGallopingAnim -> csab=hn_anim_fastrun ... [PHASE-LOCK]
SOH3D ANIM: model 2016 n64=.../gHorseNormalRearingAnim -> csab=hn_anim_stand ... [PHASE-LOCK]
SOH3D ANIM: model 2016 n64=.../gHorseNormalIdleAnim -> csab=hn_anim_wait ... [PHASE-LOCK]
SOH3D ANIM: model 2016 n64=.../gHorseNormalWhinnyAnim -> csab=hn_anim_wait02 ... [PHASE-LOCK]
```

Screenshot `scratch/screenshots/ranch_horse_regression3.png` (REPL `asel`/`afreeze`/`acam 400`)
shows the ranch horse behind its corral fence rendering as the smooth OoT3D model. The game
instance stayed up (`ps` showed the pid alive, 1:44 elapsed at the time of the check) with no
`SIGSEGV`/assert/crash lines in `scratch/logs/run.log`. Shut down cleanly via
`tools/zelda3d_game.sh stop` — no leftover `soh.elf` processes.

## Scope / honest remainder

- This session's live verification covers **En_Horse (Epona, title cs)** and
  **En_Horse_Normal (ranch horse, gameplay)** only. The fix is in the shared `Skin_DrawImpl`
  choke point, so it should equally apply to En_Horse_Ganon, En_Horse_Link_Child,
  En_Horse_Zelda, En_fHG (ghost Epona), and En_Viewer — these were **not individually
  re-verified live** this session (no crash was observed anywhere the fix runs, and the
  mechanism is identical, but "should work" is not the same as "verified" — flagging honestly
  rather than claiming blanket coverage).
- Hoof-dust terrain-warp reconciliation (the *other* half of the prior session's fix) is
  unaffected/still correct — `EnHorse_PostDraw`'s `postDraw` callback still runs after the new
  early-return, reading the same `gSkinLimbMatrices` `Skin_ApplyAnimTransformations` already
  populated.
- Bone-level mane/tail CSAB coverage was already fully RE'd/documented by the earlier
  2026-07-15 mane/tail session (bones 14/23/24) — no new bone-mapping work was needed; this
  session's contribution is purely the missing draw-choke-point wiring that makes that
  coverage actually reach the screen.
- Did not attempt frame-perfect A/B against the Azahar 3DS oracle for the title-cs gallop pose
  (per-frame pose matching) — the verification here is "renders as the correct OoT3D model with
  correct live gait-matched animation," not byte-exact pose parity. A future session wanting
  frame-exact parity should A/B against the oracle at a pinned cs frame.

## Files changed

- `Shipwright/soh/src/code/z_skin.c` — the fix (both the `#include` and the `Skin_DrawImpl`
  choke-point hunk).
- `oot3d-decomp/docs/en_horse_epona_render_gap.md` — new doc: the gap, why the raw choke point
  works byte-layout-wise, why the `SkelAnime*` wrapper was chosen over the raw one, and scope.
