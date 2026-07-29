# Adult Link's gauntlet plates are never drawn (sweep finding, 2026-07-29)

Found while closing kanban #201 e. Not a user report and not a card — agent-found, so it is fixed
in-session and recorded here (project rule: sweeps do not produce kanban cards).

## The finding

`grep -i "gauntlet|strength|UPG_"` across `Shipwright/zelda3d_shared/player/` and
`Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp` returns **nothing**. The adult mask
(`linkAdultMidMask`) starts from `LINK_MID(45) | LINK_MID(46)` and adds hand/back meshes; mesh ids
4, 5, 6, 17, 18, 19 never appear. So an adult Link with silver or gold gauntlets equipped shows no
gauntlet plates at all.

Same class of defect as #201 e — a state OoT3D consults that our mask does not model — but the
opposite polarity: #201 e drew something that should be hidden, this hides something that should be
drawn.

## The rule, already RE'd

From `Player_DrawImpl` (`0x004c11f4`), documented in `oot3d-decomp/docs/player_draw_impl_located.md`
and corresponding line-for-line with N64 `z_player_lib.c:1114-1141`:

```c
if (LINK_IS_ADULT) {
    strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);        // gUpgradeMasks[2] / gUpgradeShifts[2]
    if (strengthUpgrade >= 2) {                            // silver or gold gauntlets
        show(4);   show(17);                               // left/right gauntlet plate 1
        show(sLeftHandType  == PLAYER_MODELTYPE_LH_OPEN ? 5  : 6);
        show(sRightHandType == PLAYER_MODELTYPE_RH_OPEN ? 18 : 19);
    }
}
```

Two independent confirmations that the mesh ids and selectors are right:

* The 3DS test `cfg[0x40] == 8` is `sRightHandType == PLAYER_MODELTYPE_RH_OPEN`, because
  `PLAYER_MODELTYPE_RH_OPEN` **is** `0x08` — matching N64's
  `(sRightHandType == PLAYER_MODELTYPE_RH_OPEN) ? Plate2 : Plate3` exactly.
* OoT3D's base reset table `0x004dc388` lists adult `{45, 45, 46, 47}` as the always-on core meshes,
  and our adult mask independently starts from `LINK_MID(45) | LINK_MID(46)`. The mesh numbering
  agrees because both sides index the same `link_v2.cmb` by mesh position (`SetMeshVisible` indexes
  by CMB mesh index — see the same doc), so OoT3D's ids are directly usable in our mask.

Note `47` is in OoT3D's always-on set and not in ours; check whether that is our far-LOD mesh (the
child map documents 25 as far-LOD, never drawn) before adding it.

## Why it is not being fixed in this tick

The code change is small and the rule is settled, but verifying it needs `UPG_STRENGTH` forced the
way `bitem` forces the B item, and there is no REPL primitive for the upgrade word yet. Shipping the
edit without that would repeat the mistake #201 e's first verification attempt made — a change that
builds and looks right, measured by something that cannot actually see it.

Next: add a `upg` REPL primitive (or extend `bitem` into a general `gsc` field poke), implement the
rule in `linkAdultMidMask`, and verify with the frozen-logic method that worked for #201 e — control
capture first, then diff with the HUD region excluded by address rather than by eye.

## FIXED 2026-07-29

`LinkGear` gained `strengthUpgrade` (0..3, game-agnostic — MM uses the same slot); the OoT adapter
fills it from `CUR_UPG_VALUE(UPG_STRENGTH)` so the shared policy file stays free of `gSaveContext`;
and `linkAdultMidMask` gained the ported rule:

```c
if (gear.strengthUpgrade >= 2) {
    m |= LINK_MID(4) | LINK_MID(17);                                  // plate 1, both arms
    m |= (gear.leftHand  == LinkHandLeft::Open)  ? LINK_MID(5)  : LINK_MID(6);
    m |= (gear.rightHand == LinkHandRight::Open) ? LINK_MID(18) : LINK_MID(19);
}
```

New REPL primitive `upg <type> [value]` drives the state, for the same reason `bitem` exists.

### Evidence (adult Link, frozen logic, control = 58 px)

| test | changed px in the body band x300-470 |
|---|---|
| strength 0 vs 1 (Goron bracelet) | **14** — noise; the `>= 2` gate holds |
| strength 0 vs 2 (silver) | **1415** — plates appear |
| strength 2 vs 3 (silver vs gold) | **13** — identical geometry |
| strength 0 vs 3 (gold) | **1414** |

Silver and gold being geometrically identical is CORRECT and matches N64, which draws the same
display lists for both and distinguishes them only by env colour (`sGauntletColors[upgrade - 2]`).
Visually the silver plates and their red gem appear on the forearm where there was bare bracer.

### Residual, deliberately not fixed here

**Gold gauntlets render silver.** N64 sets an env colour per upgrade
(`z_player_lib.c:1120-1130`, `sGauntletColors`); we apply none, which the 13-px silver-vs-gold delta
proves — if colour were applied the plates would change hue and the delta would be large. That is a
COLOUR path (material/env override), not mesh visibility, so it does not belong in the midmask and
is left as a separate follow-up rather than bolted on here.

## Sibling findings: boots and the Goron bracelet are unmodelled too

The same grep that found the gauntlets finds nothing for boots or the bracelet either. Both rules are
already RE'd in `oot3d-decomp/docs/player_draw_impl_located.md`:

* **Adult boots** — `if (boots != 0)` then two meshes from `sBootDListGroups[boots - 1]`; the 3DS
  table at `0x0053c74c` gives iron -> meshes **35, 36** and hover -> **15, 22**. Not attempted yet:
  it needs a REPL primitive to set `currentBoots`, which does not exist.
* **Child Goron bracelet** — mesh **15** (`0xf`), gated on strength `>= 1` (NOT the `>= 2` the adult
  plates use — the bracelet *is* upgrade 1).

### Bracelet: written, builds, NOT verified — left uncommitted

The rule is in `LinkMidMask::compute`'s child path and the build is clean, but the measurement does
not support it, so it is not committed.

What the measurement actually said, after two false starts:

1. First attempt: control 0 px, test 0 px. Both frames were **black** — `freeze 1` landed during the
   post-restart fade-in. That reads identically to "no change" and was nearly recorded as one.
2. Second attempt on a usable frame (mean RGB 75.6/83.7/27.3): control **153** px, test **139** px.
   The test is BELOW the control, i.e. no signal.

Confirmed along the way that the code path is live — the on-screen Link is unmistakably the child
(child proportions, Deku shield), so `compute`'s child branch is the one running.

So either mesh 15 is not the bracelet in `childlink_v2.cmb`, or the bracelet sits on the left forearm
which this side-profile camera occludes behind the body and shield. The child body meshes agreeing
with OoT3D's always-on child set ({24,25,26} vs our {24,26} with 25 documented as far-LOD) argues the
numbering IS shared, which favours the occlusion explanation.

**Next:** re-measure from a front-facing camera where both forearms are unoccluded, with a settled
control taken in the same session (the noise floor moved between 0, 21, 58 and 153 px across this
evening's runs — a control from an earlier run is worthless). Recorded as instrument I009.

## Bracelet: VERIFIED and committed (dd3205ea)

The rule was right; three of my four measurements were not. Final evidence: forearm box
x380-560 y60-200 at `acam 35`, **control 0 px, strength 0 vs 1 = 1610 px**, changed region bbox
x[463..525] y[89..148] — a 62x59 forearm-cuff-sized area, with the cuff plainly visible in frame.

What made the earlier attempts fail, in order:

1. **Black frames.** `freeze 1` landed during the post-restart fade; control 0 px and test 0 px.
   Identical in shape to a genuine null result.
2. **Signal under the noise floor.** At `acam 100` the bracelet is a few dozen pixels and the scene
   noise floor was 153 px. Test 139 px. Correctly read as "no signal", but the conclusion drawn from
   it — that the mesh id might be wrong — was not warranted.
3. **Wrong band attribution.** At `acam 35` I split the frame into "HUD band y0-160" and the rest,
   found all 2468 changed pixels in y0-160 and none below, and nearly concluded the change was
   entirely HUD. But at that distance Link's forearms ARE in y0-160 — his head is out of frame. The
   band labels were carried over from the `acam 100` layout where they were true.

The fix for all three is the same discipline: never split a frame by remembered coordinates, place
the measurement box on the thing being measured in THIS camera, and take the control in the same
session. Recorded as instrument I009.

### Residual

The bracelet renders very dark, close to black (see the frame). Geometry and gating are right; its
material/texture is a separate question — mesh 15 was never drawn before, so its material has never
been exercised and may not be wired the way the other child meshes are.

### Still outstanding from this sweep

* **Adult boots** — iron -> meshes 35, 36; hover -> 15, 22 (from the `0x0053c74c` table). Needs a
  REPL primitive to set `currentBoots`; not attempted.
* **Gold gauntlets render silver** — no per-upgrade env colour; a colour path, not visibility.
* **Mesh 47** — in OoT3D's always-on adult set, absent from ours; plausibly far-LOD.

## Boots: VERIFIED and committed (5bac44f0) — sweep complete

Iron -> meshes 35, 36; hover -> 15, 22. Evidence at `acam 60`, adult, control **0 px**:
kokiri vs iron **7681 px**, kokiri vs hover **8329 px**, iron vs hover **9198 px**, all confined to
the feet (y 241-376). Iron renders as metallic caps over the leather, hover as the cream/yellow pair.

### The `boots` primitive took three iterations, and the first two failed SILENTLY

1. It printed the raw equip value, which is **1-based** (1 kokiri, 2 iron, 3 hover) while
   `player->currentBoots` is 0-based — `Player_SetBootData` does
   `currentBoots = CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) - 1`. So the default save read "boots=1" and
   looked like iron was already equipped.
2. It wrote only the SAVE. `currentBoots` is refreshed by `Player_SetBootData`, which runs on real
   equip events — a raw poke left the live player untouched while the command echoed the new value.
   **A tool reporting success for a state change that never happened.** It now writes both and prints
   both.

That is the same shape as every other instrument failure this session: the tool is confident, the
output is well-formed, and it is describing something other than what you asked about.

## Sweep summary

Three equipment meshes were never enabled at all. All three are now drawn and verified:

| item | meshes | gate | commit |
|---|---|---|---|
| adult gauntlet plates | 4, 17 + 5\|6 + 18\|19 | strength >= 2 | `bd6fec71` |
| child Goron bracelet | 15 | strength >= 1 | `dd3205ea` |
| iron / hover boots | 35,36 / 15,22 | boots != 0 | `5bac44f0` |

### Left open, all recorded rather than quietly dropped

* **Gold gauntlets render silver** — no per-upgrade env colour (N64 `sGauntletColors`). A colour
  path, not visibility.
* **The Goron bracelet renders very dark**, near black. Mesh 15 had never been drawn, so its material
  has never been exercised.
* **Mesh 47** — in OoT3D's always-on adult set, absent from ours; plausibly far-LOD.
* **The full reset-then-enable architecture** (claim C010) is still unported; our hand-curated map
  remains the mechanism, now with the three gaps above filled.

## Bracelet darkness: narrowed to the TEXCOORD MAPPING, not the material (2026-07-29)

My guess in the section above — "mesh 15 was never drawn, so its material has never been exercised"
— is **wrong**. Data-level inspection via `sgdump 2000` and `ZELDA3D_SG_DUMPTEX=2000`:

| | bracelet `g27` (meshId 15) | body `g44` (meshId 24, known-good) |
|---|---|---|
| texture | index 30, present | index 2, present |
| `combScale` | 2.000 | 2.000 |
| `vColor0` | (1,1,1,1) | (1,1,1,1) |
| `matAmb` / `matDif` | (0.40,0.40,0.40) / (0.50,0.50,0.50) | identical |
| blend / alpha test | 0 / 0 | 0 / 0 |
| **`coordMap`** | **(3, 0)** | (0, 0) |
| **TEV stages** | **4** | 2 |

And texture 30 is not dark at all: 32x32, **mean RGB (144.1, 94.5, 4.2), max 246** — a bright
orange/gold, exactly what a Goron bracelet should be.

So the material, the lighting inputs and the texture are all fine, and the only structural difference
from a group that renders correctly is the **texture-coordinate mapping (`coordMap` 3 rather than 0)
combined with a 4-stage TEV**. If we do not supply the coordinate set that `coordMap=3` selects, the
UVs are whatever that slot happens to hold and the sample lands somewhere unintended — which is
consistent with a bright texture rendering near-black.

**Next:** find what `coordMap` 3 selects in the CMB material (`Shipwright/cmb3d/asset/cmb.cpp`, the
`textureCoordinator` fields) and whether the renderer binds that set. Note the existing
`uv1Xf`/`uv2Xf` plumbing in the SG_DUMP line suggests up to three sets are modelled, so a
4-stage/coordMap-3 material may simply be outside what the shader currently handles.

This is a RENDERER question, not a visibility one — the #201 e/gauntlet/boots work is unaffected.

### Narrowed further: it is a SPHERE-ENV-MAPPED dual-texture material

Full group state, bracelet vs a known-good body group:

| | `g27` (mesh 15, bracelet) | `g44` (mesh 24, body) |
|---|---|---|
| tex0 | 30 — 32x32, mean (144.1, 94.5, 4.2), max 246 (bright orange) | 2 |
| **tex1** | **31 — 16x16, mean (26.9, 19.1, 8.4), max 191 (dark)** | **-1 (none)** |
| `coordMap` | **(3, 0)** | (0, 0) |
| TEV stages | **4** | 2 |
| wrap | **(0x8370 MIRRORED_REPEAT, 0x2901 REPEAT)** | (REPEAT, REPEAT) |
| uv0 sample | (1.22, 0.03) — u > 1, needs the wrap | (0.32, 0.81) |

`coordMap` prints `coord1Mapping` / `coord2Mapping` — the mappings for texture units 1 and 2, not
unit 0. Value **3 is `CameraSphereEnvMap`** (`zelda3d_sdl3gpu.cpp:272`), i.e. a generated normal-derived
UV, not a vertex attribute — which is consistent with it being out of range for the three texcoord
sets the CMB vertex layout actually carries (`texCoord0/1/2`, `cmb.cpp:42`).

So the bracelet is **tex0 (bright orange base) + tex1 (dark 16x16 highlight) sphere-mapped over it,
through a 4-stage chain**. A dark second texture combined wrongly is exactly how a bright base ends up
near-black.

Sphere mapping is NOT missing — it is implemented and the shader recognises `uTevCtl.z == 3`. But the
comments at `zelda3d_sdl3gpu.cpp:272-302` show it was developed for the TITLE WORDMARK's decorations,
whose normals are flat `(0,0,1)`; a skinned character mesh has real varying normals and a per-bone
transform, so the view-space normal the sphere UV derives from may not be right here.

**Next:** decode `g27`'s 4-stage TEV pack
(`00e30e30/00000000/00000111, 00e1ff43/00000002/00000101, 00e1fedf/00000000/00000002,
00e1feef/05000020/00000008`) against the packing documented at `Zelda3DGlGroup::tevStagePack`, and
check the sphere UV a skinned mesh produces — the title path is the only one it has been validated on.

## Mesh 47: leaving it out is CORRECT (2026-07-29)

OoT3D's always-on adult set is `{45, 46, 47}` and ours is `{45, 46}`; I had guessed 47 was the
far-LOD body and deliberately not added it. Rendered alone via `linkmid only <n>` on adult Link at
`acam 110`, frozen, against an all-off frame:

| mesh | changed px | reading |
|---|---|---|
| 45 | **16515** | the body |
| 46 | **3676** | head / face |
| 47 | **70** | — |
| *control (same state twice)* | *51* | — |

70 against a 51 px control is not a signal. Mesh 47 draws **nothing** at our LOD — and note it was
rendered ALONE, so this is not occlusion by mesh 45.

So omitting it is right, and now on evidence rather than on the guess. It is presumably the far-LOD
variant, which we never select because we always render near LOD. Recorded so nobody "fixes" the
discrepancy with OoT3D's table by adding a mesh that would at best do nothing and at worst
double-draw the body.

## Gold vs silver: the tint slot is material CONSTANT 5 (2026-07-29)

My earlier note said "N64 sets an env colour per upgrade and we apply none", which quietly implied
N64's mechanism is the one to port. That was an assumption. Checked properly:

* **`Player_DrawImpl` does not set any gauntlet colour.** Its tail is eye/mouth texture selection and
  fade handling — read through to the end, nothing there.
* **OoT3D does NOT swap textures per upgrade.** All four gauntlet meshes (4, 17, 5, 18) share
  texture index **12** in `zelda_link_boy_new.zar`.
* **That texture is designed to be tinted**: 128x256, mean RGB (73.8, 62.0, 57.6), mean channel
  spread **17.3** — effectively greyscale — with max 255.
* **The material's TEV consumes a constant, and names which one**:
  `g14 combUsesConst=1 constIdx=5`, with `const5 = (0.000, 0.000, 0.000, ...)` in our render.

So the tint IS a per-upgrade colour as on N64. **FOUND (see below): it is set in `Player_DrawImpl`
after all**, from a two-row table at `0x0053ca1c` — silver `(1,1,1,1)`, gold `(0.996,0.812,0.059)`.

## This WEAKENS the bracelet sphere-map hypothesis

The gauntlet group `g14` has **`coordMap=(3,0)` and `stages=4`** — the same sphere-env-map + 4-stage
structure I flagged as the suspect for the bracelet's darkness. The gauntlets render correctly
(bright metallic plates, see the verified screenshots).

So "sphere mapping was developed for the title wordmark's flat normals and may not hold for a skinned
mesh" does NOT by itself explain the bracelet: a skinned mesh on the same code path looks right. The
remaining difference between them is the second texture — the bracelet has `tex1=31` (a dark 16x16),
the gauntlets do not use that unit the same way. Whoever picks this up should start from that
difference, not from the sphere-map path.


## The tint was in `Player_DrawImpl` all along — and silver is right by accident

I wrote above that `Player_DrawImpl` sets no gauntlet colour. Wrong: the call is in the gauntlet
block immediately before the visibility calls and I skipped it as unrelated.

```c
iVar4 = 0x0053ca1c + strengthUpgrade * 0x10;
func_0x0033dd8c(iVar4[-0x20], iVar4[-0x1c], iVar4[-0x18], iVar4[-0x14], player + 0x254, 0xe, 4, 0);
```

| upgrade | value | |
|---|---|---|
| 2 silver | `(1.0, 1.0, 1.0, 1.0)` | **white — identity, no tint** |
| 3 gold | `(0.996, 0.812, 0.059, 1.0)` | RGB (254, 207, 15) |

Exactly two rows, matching N64's `sGauntletColors` length; beyond them the bytes decode as garbage.

**Why the verification screenshots gave no hint of this.** Silver's tint is the identity, so applying
no tint is accidentally correct — the silver plates looked right and nothing suggested a whole colour
path was missing. Only gold is wrong. A missing multiply is invisible wherever the factor happens to
be 1, which is a good argument for testing the non-default case of anything that looks like a tint.

Full detail and the port target in `oot3d-decomp/docs/player_draw_impl_located.md`.

## Bracelet: the material-constant hypothesis is FALSIFIED too (2026-07-29)

The gold-gauntlet fix made a tempting hypothesis: `townsfolk.cpp` documents that a CMB's default
`matConstant` of `(0,0,0,1)` feeding a `MODULATE(PREV, CONST)` stage renders the fragment BLACK, and
the bracelet renders black. But the constant state is identical between the bracelet and a group that
renders correctly:

```
g27 (bracelet, black)      combUsesConst=1 constIdx=5  const0..5 all (0.000,0.000,0.000,1.000)
g44 (child body, correct)  combUsesConst=1 constIdx=5  const0..5 all (0.000,0.000,0.000,1.000)
```

So a zero constant is not sufficient to blacken a fragment here, and that explanation is dead.

Running list of what the bracelet's darkness is NOT:
* not the material/lighting inputs (matAmb, matDif, vColor0, combScale all match a good group)
* not the texture (tex0 index 30 is a bright orange 32x32, mean (144, 94, 4), max 246)
* not the sphere-map path (the gauntlets share `coordMap=(3,0)` + 4 stages and render correctly)
* not the material constants (identical to a good group, above)

What is left is the TEV CHAIN ITSELF. The packed stages differ between the two 4-stage materials:

```
bracelet g27:  00e30e30/00000000/00000111, 00e1ff43/00000002/00000101,
               00e1fedf/00000000/00000002, 00e1feef/05000020/00000008
gauntlet g14:  00e30e30/00000000/00000111, 00e1ff43/00000002/00000008,
               00e1feef/04000000/00000001, 00e1feef/05000020/00000008
```

Stages 2 and 3 differ in both the operand word and the source word. **Next session** (not this one —
decoding a TEV packing at the end of a long session is how confidently-wrong conclusions get made):
decode both against the packing documented at `Zelda3DGlGroup::tevStagePack` and find which stage our
shader mishandles. The gauntlet chain is a known-good control to diff against, which is the useful
part of this finding.

## Regression check on the gold tint: no state leak (2026-07-29)

`Zelda3D_GL_SetMatConstOverride` writes into a slot that PERSISTS until it is written again, and the
port only *calls* it when `strengthUpgrade >= 2`. That is two separate leak risks, both checked:

**1. Gold -> silver.** Not a leak, because the silver branch writes the identity `(1,1,1,1)` rather
than skipping. Measured on adult Link, frozen, `acam 60 z`, changed-pixel count in the forearm band
`y80:200`:

```
silver -> gold        1118 px
gold   -> silver      1118 px      (symmetric)
silver(1) vs silver(2)   0 px      <- byte-identical; nonzero here would have meant a leak
```

**2. Strength dropped below 2.** The call is skipped entirely, so whatever was last written stays in
material 14's constant slot. Harmless ONLY if material 14 is exclusive to the gauntlet plates —
otherwise a stale gold value would discolour whatever else shares it. `sgdump 2014` settles it:

```
g14 meshId=4  tex=12 mat=14      g53 meshId=17 tex=12 mat=14
g15 meshId=5  tex=12 mat=14      g54 meshId=18 tex=12 mat=14
g17 meshId=6  tex=12 mat=14      g56 meshId=19 tex=12 mat=14
```

Exactly the six meshes the `>= 2` gate enables, and no others. A stale constant therefore lands only
on meshes that are hidden in that same state. No collateral, no reset needed.

Worth keeping in mind if the override mechanism is reused: this is safe because of a property of THIS
material (single-owner), not because overrides clean up after themselves. They do not.

## RESOLVED: the bracelet was black because we never implemented the PICA combiner buffer

Decoding the two chains (new tool `tools/tev_decode.py`) against the known-good gauntlet control:

```
bracelet g27 (childlink_v2 mat26)        gauntlet g14 (control, renders correctly)
 0  rgb = 2 * (Primary * Tex0)            0  rgb = 2 * (Primary * Tex0)
 1  rgb = 2 * (Tex0.a * Tex1)             1  rgb = Tex0.a * Tex1 + Previous
 2  rgb = Previous + PrevBuffer           2  rgb = Previous * Constant
 3  rgb = Previous * Constant.a + Constant   3  (same)
```

This is the standard PICA diffuse-in-buffer idiom: stage 0 computes the diffuse and latches it into
the combiner buffer, stage 1 REPLACES Previous with the env/spec term, and stage 2 adds the diffuse
back from the buffer. Our shader evaluated PREVBUF as `vec4(0)`, so the diffuse was discarded and only
the dark env term survived. The gauntlet escapes it because its stage 1 is MulThenAdd `+ Previous`,
carrying the diffuse forward without ever touching the buffer — which is why it was a good control and
a misleading one at the same time.

The shader documented the assumption that made this invisible: *"the CMB buffer-input selector is
0x8579 corpus-wide (buffer never latches PREVIOUS)"*. That is FALSE. Corpus-wide (survey now reports
it): **1108 stages latch**, and **14 materials read PREVBUF** — childlink_v2 mat10/22/26, all seven
Volvagia meshes (`zelda_fd/valbasia*`), and four Ganon's Tower scene materials.

### The one-stage offset

Azahar assigns `combiner_buffer <- next_combiner_buffer` AFTER computing the stage output and only
then applies the latch (`sw_rasterizer.cpp` ~991 and `glsl_fs_shader_gen.cpp` ~476 agree), so a
latched value first becomes visible two stages later. Separately, the CMB stores the latch flag one
stage AHEAD of Azahar's 0-based mask bit: 3dbrew names the GPUREG_TEXENV_UPDATE_BUFFER bits "TEV
stage 1..4" and Grezzo puts the flag on the stage the register names.

Confirmed on data, not inferred: all 14 PREVBUF-reading materials latch at exactly `read - 1`,
including the Ganon's Tower ones at 4/3 rather than 2/1 — so it is a relationship, not a fixed
position. Under the other mapping every one of those latches would be inert and every buffer read
would return a value nothing ever wrote, which is not a pattern anyone authors.

The material's INITIAL buffer color (`tev_combiner_buffer_color`) is still not parsed and is taken as
0. That is exact for this ROM: every PREVBUF read is preceded by a latch, so the initial value is
never read. FALSIFIER: a material that reads PREVBUF with no preceding latch — the survey prints both
columns so it would show up.

### Measured A/B (the only leg that changed is the one that should have)

Same build tree, one line differing (`tevSrc` code 13 returning `vec4(0)` vs the buffer). Child Link,
`freeze 1`, identical camera/time, per-draw isolation differenced against a background baseline,
restricted to the stable band y120:360 (HUD and bottom edge drift between captures):

```
                            PRE-FIX                    POST-FIX
bracelet  (reads PREVBUF)    822 px  RGB(25,17,7)      840 px  RGB(136,101,11)
control   (no PREVBUF)      7184 px  RGB(20,68,18)    7172 px  RGB(20,68,18)
control repeat              7183 px                   7166 px
```

The control is unchanged to the integer in every channel. The bracelet keeps its footprint (±2%) and
goes 5.4x brighter and orange — and `(136,101,11)` matches its own texture's mean `(144,94,4)`,
measured independently earlier in this file, which is what restoring a discarded diffuse should do.

### Tooling notes (both were needed to get here honestly)

* `tools/tev_decode.py` — decodes/diffs packed TEV chains. Reading the hex by eye is how a wrong
  stage gets blamed; validated on six hand-built cases before use.
* The first three attempts at the A/B were invalid and said so only because a control was carried:
  `sgdrawonly` isolates Zelda3D groups but NOT the N64 world, so the first measurement was the
  background in both legs (381k px, identical). Then `afreeze` + `animlive 0` still left Link
  animating — two consecutive captures of the SAME draw differed by 19969 px. Only `freeze 1`
  (holds Play_Update) gave a reproducible frame, and even then the HUD and bottom edge drift.
  A control re-captured in-run is what turns "the numbers moved" into evidence.
