# #201(d) — Link's face never animates: the missing `.faceb` facial track

Date: 2026-07-23. Status: **ported + verified live**; oracle capture separate (below).

## Symptom

User screenshot: Link mid stretch/yawn idle fidget (arms above his head) with a **completely
neutral face** — eyes wide open, mouth shut. On 3DS his eyes squeeze shut and his mouth opens.

We were not porting the facial channel for the PLAYER at all. Every NPC already had it
(`behaviors/actor/kokiri_kid.cpp`, `saria.cpp`, `malon.cpp`, `townsfolk.cpp` → `applyFacialFrame`).

## Ground truth (static RE from the retail romfs — no guessing, no probing)

**The face is part of the ANIMATION, not of the actor state.** Both Link zars
(`/actor/zelda_link_child_new.zar`, `/actor/zelda_link_boy_new.zar`) contain, next to each of their
**582 `boy/anim/<clip>.csab`, a `boy/anim/<clip>.faceb`** — 582 of each, same basename.

`.faceb` layout (little-endian, `Shipwright/cmb3d/asset/faceb.h`):

```
0x00  "fkb" + u8 version (1)
0x04  u16 keyCount, u16 pad
0x08  keyCount × { u16 frame; u8 eyeIndex; u8 mouthIndex }    frames ascending
0xFF in a channel = HOLD (this clip does not drive it) — a clip with no face data is one {0,FF,FF} key
```

The indices select a frame of a **TexturePalette CMAB** bound to one eye and one mouth material
(`tools/cmab.py`, material index read from the cmab's own `mmad`):

| rig | eye cmab | eye mat | frames | mouth cmab | mouth mat | frames |
|---|---|---|---|---|---|---|
| child (`childlink_v2`) | `childlink_eye.cmab` | 14 | 8 | `childlink_mouth.cmab` | 15 | 4 |
| adult (`link_v2`) | `link_eye.cmab` | 16 | 8 | `link_mouth.cmab` | 17 | 4 |

**8 eye / 4 mouth is exactly N64's `sEyeTextures[8]` / `sMouthTextures[4]`.** Grezzo re-encoded the
same data N64 hides inside the animation's fake limb 22 (`z_player_lib.c` `Player_DrawImpl`:
`eyeIndex = (jointTable[22].x & 0xF) - 1`, `mouthIndex = (jointTable[22].x >> 4) - 1`).

The reported yawn is **`wait_typeD_20f`** (N64 `sFidgetAnimations` FIDGET_STRETCH_1..3). Its faceb:

```
f0 e0 m0 · f1 e0 m1 · f6 e1 · f8 e2 · f10 e1 · f12 e0 · f17 e1 · f19 e7 · f36 e7 m3 · f39 e3 m3
· f50 e1 · f52 e2 · f56 e0 · f61 e1 · f63 e2 · f67 e4 · f75 e1 · f77 e2 · f79 e0 · f84 m0 · … · f128 e0 m0
```

i.e. **eye 7 (squeezed shut) frames 19–38, mouth 3 (wide open) frames 36–78**, with 0→1→2→1→0 blinks
either side. The decoded eye/mouth sprite sets are at `scratch/screenshots/link_child_face_atlas.png`
(`scratch/face_atlas_dump.py`).

## Falsified on the way — do not re-chase

1. **"Link's eye is a UV-scrolled atlas"** (suggested by `link_e00` being 128×64 for an 8-state eye).
   No. `tools/cmab.py /actor/zelda_link_boy_new.zar boy/misc/link_eye.cmab` → `TexturePalette mat=16`,
   8 integer keyframes. It is a texture SWAP, identical to every NPC.
2. **"our vendored N64 `z_player.c` already computes eye/mouth — just forward the two fields"**.
   The `Player` struct has **no eye/mouth field at all**; N64 reads them out of the animation, and our
   Link plays OoT3D CSABs whose joint data has no limb 22. The faceb IS the 3DS source.
3. **"There IS no yawn animation"** — a previous session's note in `zelda3d_link.cpp` (#88). Wrong,
   and it cost this session time. No clip is *named* yawn/akubi, but `wait_typeD_20f` is the yawn.
   Note corrected in-code.
4. **"`az_camera` can frame the oracle's camera on Link's head"** (from the task brief). It cannot —
   `az_camera` is a READ of the title-demo camera basis @0x005BE6D4 and is inert in gameplay. The
   oracle has no gameplay camera setter; higher-resolution evidence comes from
   `ZELDA3D_HARNESS_RES_FACTOR` (1..8) plus a crop.

## The reason the fidget was hard to observe on the oracle

`Player_ChooseNextIdleAnim` uses `fidgetType = play->roomCtx.curRoom.behaviorType2` on the 1-in-5 roll
that rejects a common fidget. STRETCH needs `behaviorType2 >= 4`. A scan of **all 724 OoT3D room
`.zsi`** (header cmd `0x08`, `cmd2 & 0xFF`) gives:

```
0 (LOOK_AROUND) 457 rooms   1 (COLD) 43   2 (WARM) 51   3 (HOT) 14
4 (STRETCH_1)    36 rooms   5 (STRETCH_2) 7   6 (STRETCH_3) 2
```

**Kokiri Forest is 0 — the stretch can never fire there.** The first oracle attempt (entrance 0xEE)
therefore only ever produced look-around / adjust-tunic / tap-feet, which reads like "the oracle
suppresses the stretch". Stretch rooms include Market Entrance day (4, entrance `0x33`), Link's House
(5), Market Alley (6).

## The port

- `Shipwright/cmb3d/asset/faceb.{h,cpp}` — new parser + step sampler (0xFF = hold).
- `Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp` — two Link rows in `kFacialAssets` (so the
  existing cmab frame decoder loads the 8+4 sprites), plus `getFaceb`/`Zelda3D_FacebSample`
  (per-model faceb cache, sibling-of-CSAB resolution) and `Zelda3D_FacialMaterialIndex`.
- `Shipwright/soh/src/zelda3d/player/zelda3d_link_face.cpp` — the per-frame driver (new module, per
  the "one module per behavior" rule). Samples the faceb at the **resolved CSAB playhead**
  (`Zelda3D_LastAutoAnim`, not the N64 `curFrame` — the clips differ in length), latches the last
  driven index across clips (the 0xFF hold), and binds through the shared `applyFacialFrame`.
- `zelda3d_link.cpp` — call site after the anim update, plus REPL `linkface` (report live
  clip/frame/eye/mouth/materials) and `linkframe <f>` (pin the forced clip's playhead).

## Verification

Data path, live game, pinned `wait_typeD_20f` — reproduces the ROM track exactly:

```
linkface … frame=0.00  eye=0 mouth=0      linkface … frame=37.00 eye=7 mouth=3
linkface … frame=8.00  eye=2 mouth=1      linkface … frame=40.00 eye=3 mouth=3
linkface … frame=25.00 eye=7 mouth=1      linkface … frame=60.00 eye=0 mouth=3
                                          linkface … frame=90.00 eye=0 mouth=0
eyeMat=14 mouthMat=15 (child rig), faceb=1 (track found)
```

Render: `scratch/screenshots/ours_face_before_after.png` — left `faceframe 0` (what shipped before:
wide-open eyes), right live (eye 7 squeezed shut). Neutral idle `nml_wait_free` reports
`eye=-1 mouth=-1` (single hold key) — correct: on 3DS the neutral idle carries no face data and the
last bound face persists, so blinks come from the fidget clips.

## Left open (not approximated)

`actor.shape.face` scripted faces (N64 `sEyeMouthIndexes[face]`, used for damage/cutscene faces).
OoT3D's twin has not been located; deliberately not faked. Tracked in `docs/re-frontier.md`
`player.facial-anim`.
