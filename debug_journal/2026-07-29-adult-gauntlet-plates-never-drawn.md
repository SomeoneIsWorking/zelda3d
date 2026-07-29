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
