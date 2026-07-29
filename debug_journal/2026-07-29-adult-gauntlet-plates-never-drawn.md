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
