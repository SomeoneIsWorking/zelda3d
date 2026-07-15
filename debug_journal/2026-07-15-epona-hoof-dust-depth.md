# Epona hoof-dust depth/occlusion — fixed (terrain-warp Y reconciliation), plus a bigger dead-path finding

Task: fix the title-intro hoof-dust particles rendering in front of / punching through the terrain
hill instead of being occluded by it. Prior attribution (2026-07-15 tree/terrain journal): "the
dust-spawn positions come from a stale native skin skeleton because the 3DS Epona model bypasses
native EnHorse_Draw."

## RE (confirmed, no new Ghidra needed)

`z_en_horse.c`'s `EnHorse_PostDraw` (called from `EnHorse_Draw` via `Skin_DrawImpl`'s postDraw
callback) computes hoof positions via `Skin_GetLimbPos(skin, n64LimbIndex, ...)` at specific
animation-frame windows, sets `this->dustFlags`; `EnHorse_Update` the next logic frame spawns
`EffectSsDust` (`func_800287AC`) at the stashed position. N64 hoof limb indices: frontLeft=20,
frontRight=28, backLeft=37, backRight=45. Full writeup in
`oot3d-decomp/docs/en_horse_hoof_dust.md`.

## The prior attribution's MECHANISM was wrong — corrected this session

I initially built a fix assuming Epona's body draws as the OoT3D `epona.cmb` model (confirmed the
25-bone CMB loads, `actorsnear` reports `AUTO:/actor/zelda_horse.zar (skin)`) and that
`EnHorse_PostDraw`'s `Skin_GetLimbPos` reads a STALE native skeleton relative to that visible mesh.
I dumped the CMB's hoof-bone world offsets (bones 6/13 front, 18/22 hind — see the decomp doc) and
wired a `Zelda3D_HoofDustWorldPos` that resolved hoof positions from `Zelda3D_PosedBoneWorldPos`
(the existing #6 held-actor-attach API).

**Live verification falsified this.** Added `ZELDA3D_DUST_DEBUG=1` instrumentation logging every
hoof-dust resolve; ran the live game through the title-cs gallop — 100% `FALLBACK (not
OoT3D-replaced this frame)`, on every single hoof-dust event, across multiple fresh runs, with
`ZELDA3D_N64ANIM` both at its default (1) and forced to 0 (byte-identical fallback-only result either
way — ruling out an env-flag issue). Traced why: `z_skin.c`'s `Skin_DrawImpl` (which `EnHorse_Draw`
calls) has **zero Zelda3D references** anywhere in the file — the Zelda3D draw-replacement hooks
(`Zelda3D_SkelAnimeDraw`/`Zelda3D_SkelAnimeDrawRaw`) are wired into `z_skelanime.c`'s
`SkelAnime_Draw*` family only, a completely different animation/draw system than the `Skin` system
En_Horse exclusively uses. So `Zelda3D_TryAuto`'s `e->skinned` branch (which defers to the actor's
own `Draw`, expecting a hook mid-native-draw to consume `gZelda3dPendingActor`/`Model`) sets those
globals every frame for Epona and **nothing ever consumes them** — Epona currently ALWAYS renders as
the native N64 mesh (with the HD texture pack — the "4K TEXTURES BY HENRIKO" credit visible in every
title-cs screenshot was the tell I initially missed). `actorsnear`'s `(skin)` tag is a static
eligibility classification, not a live draw confirmation.

**This is a separate, bigger finding than the dust bug**: Epona's OoT3D body replacement is
effectively dead code right now. Porting it for real needs a Zelda3D choke point wired into
`Skin_DrawImpl` (mirroring the existing `SkelAnime_Draw*` hooks) — out of scope this session, noted
honestly in the decomp doc as the next real RE/port step, NOT silently left as a passing
classification.

## The real, fixable bug: terrain-warp Y never reconciled for the dust effect

Independent of which model draws Epona's body, the dust's Y comes from the native N64 collision-floor
height, while `zelda3d.c`'s `Zelda3D_TerrainWarpEnabled()` already documents (and already fixes, for
actor MODELS and unconditionally during the title cs) exactly this class of divergence — title-cs
props/actors positioned from raw N64 coordinates never get reconciled against the OoT3D-warped render
terrain, so they can poke through or float above hill relief the N64 mesh never had. `EffectSsDust`
spawns through its own path and had never gotten that reconciliation.

## Fix

- `Shipwright/soh/src/zelda3d/zelda3d.c`: factored the core of the existing
  `Zelda3D_ActorRenderYOffset` into `Zelda3D_RenderYOffsetAtXZ(play, actor, x, z)` (explicit XZ
  instead of always `actor->world.pos`, since a hoof sits laterally away from the horse's root and
  needs the terrain delta at ITS OWN xz). Added `Zelda3D_HoofDustWorldPos(play, horseActor, ioPos)`:
  given an already-computed native hoof position, adds the OoT3D-vs-N64 Y delta at that XZ. No-op
  when the terrain warp is inactive.
- `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c`: `EnHorse_ResolveHoofPos` wraps
  every hoof `Skin_GetLimbPos` call site in `EnHorse_PostDraw` (15 sites across idle/stopping/gallop/
  jump branches) — computes the position the native way unchanged, then applies the Zelda3D Y
  correction. Pure fixup: byte-identical to vanilla N64 behavior whenever Zelda3D is off.
- `ZELDA3D_DUST_DEBUG=1` diagnostic kept as durable tooling (same convention as the existing
  `gZelda3dAnimDebug` `[SKELSCALE]` print) — dust is small/short-lived/easily occluded, genuinely
  hard to verify by eye alone.

## Verification

Live, headless, through the title cs's gallop cue window (native `animationIdx=ENHORSE_ANIM_GALLOP`,
`cutsceneAction=1`, confirmed via REPL `ainfo`). `ZELDA3D_DUST_DEBUG=1` stderr shows `corrected=1` on
every hoof-dust resolve during the gallop, e.g.:
```
[Zelda3D dust] limb=45 pos=(-6550.5,-47.5,4721.6) corrected=1 actorY=-48.0
[Zelda3D dust] limb=20 pos=(-6514.4,-32.1,4726.1) corrected=1 actorY=-43.6
[Zelda3D dust] limb=28 pos=(-6507.8,-31.9,4747.4) corrected=1 actorY=-41.5
```
confirming the fix's code path is live and actively adjusting the dust's Y. Screenshot
`scratch/screenshots/horse_dust_final.png` (gitignored) shows visible dust puffs at Epona's hooves
grounded against the grass mid-gallop.

**Honest gap**: did not capture a side-by-side frame showing a puff nested BEHIND the hill silhouette
specifically (vs. simply landing on the correct local ground, which IS confirmed) — the live game's
title-cs driver stalled intermittently under heavy REPL polling during this session (multiple runs
saw the `titlecs` frame counter and the rider's `world.pos` freeze for 10+ seconds while the horse's
own `skin.skelAnime.curFrame` kept advancing; recovered by a fresh restart each time, never by
waiting). This is a pre-existing harness/engine fragility (see `soh3d-harness-longstep-fragility`
memory for the general class), NOT caused by this fix — reproduced identically before touching any
code and on completely clean runs. Root-causing that stall is a separate, worthwhile follow-up (it
will keep costing every future title-cs live-verification session time) but is out of scope here.

## Remaining/follow-up work (journaled, not filed as a kanban card per the sweep rule)

1. **Bigger**: wire a Zelda3D choke point into `Skin_DrawImpl` (z_skin.c) so Epona (and any other
   `Skin`-system actor) can actually draw as its OoT3D CMB model — the `e->skinned` AUTO
   classification already exists and is *waiting* for this; right now it's inert. This is the
   correct fix for "Epona should look like OoT3D's model," which the mane/tail journal on 2026-07-15
   incorrectly believed was already working (that journal's zoomed-screenshot "mane bone visibly
   changes orientation" evidence was almost certainly the native N64 mane polygon, not a 3DS bone —
   worth a follow-up correction note there).
2. **Smaller**: the `titlecs`/rider-driver intermittent stall under REPL load — root-cause and fix so
   future live-verification sessions don't need multiple fresh-restart retries.
3. Not reproduced/verified: the exact `(5,-4,5)` `hoofOffset` sub-bone displacement the N64 code
   composes — omitted as sub-visual at world scale; revisit only if a future close look shows dust
   originating perceptibly off the hoof tip.
