# MM Player sheath and back-shield selector

## Root cause

The form-specific base reset deliberately hides every equipment group, but the
native MM Player route stopped there. Retail MM3D immediately follows the reset
with a complete sheath-limb selector. Without that stage, Deku's sheath and
Human's equipped sword, sheath, and shield-on-back cannot appear even though
the correct groups exist in the selected form CMB.

This is not an OoT mesh-map reuse. ARM disassembly of retail
`FUN_0020cfa4(Player*)` grounds both the input identities and every selected
mesh. The durable address/field/table record is in
`mm3d-decomp/docs/player_draw.md`.

## Recovered behavior

- MM3D `Player` offsets `+0x1f8`, `+0x1ff`, `+0x20a`, `+0x20b`, and `+0x218`
  align to typed 2S2H `currentShield`, `transformation`, `sheathType`,
  `currentMask`, and `sheathDLists`.
- The save halfword read uses mask `0x000f` and shift zero, exactly
  `GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD)`.
- Giant's Mask branches around both sheath and shield-on-back selection.
- Deku sheath types 12/13 add mesh 8; types 14/15 add none.
- Human sword equipment None/Kokiri/Razor/Gilded selects `-1/5/6/7` when the
  sword is sheathed and `-1/13/15/17` for the empty sheath.
- Human sheath types 14/15 additionally select Hero mesh 3 or Mirror mesh 4
  when a shield is equipped.

Exact-member parsing of `child/model/link_child.cmb` reported all 34 Human mesh
IDs. Mesh 3 uses `p_shield_h_00`, mesh 4 uses `p_shield_m_00`; meshes 5/6/7
contain the three `p_sword_*` plus `p_saya_*` texture families, while 13/15/17
are their sheath groups. The binary tables remain the mapping authority; the
asset inventory is an independent structural check.

## Port shape

`mm3d_player_sheath_policy.{cpp,h}` owns a pure typed selector, and
`mm3d_player_sheath.{cpp,h}` is the narrow adapter from native 2S2H enums. The
Player draw composer ORs the returned equipment additions with the existing
base mask before its one material-state snapshot. The left/right-hand stages
are intentionally still absent: their default mesh corpus is recovered, but
their animation, speed, joint-table, and state overrides are not yet fully
aligned.

## Focused evidence

The isolated Clang policy gate passed all three Player policy binaries:

```text
CXX=clang++ uv run --frozen python -m unittest \
  tools.test_mm3d_player_model_policy \
  tools.test_mm3d_player_mesh_policy \
  tools.test_mm3d_player_sheath_policy
Ran 3 tests in 0.721s -- OK
```

The exact-member static gate found Deku mesh 8 and all eight Human selector
groups (`3,4,5,6,7,13,15,17`). A required nonexistent mesh 99 returned exit 1,
so the diagnostic has demonstrated both pass and fail outcomes.

The serialized shared tree reports Clang 22.1.8 for both C and C++ and passed:

```text
cmake --build Shipwright/build-cmake --target zelda3d_app -j1
[59/60] Linking CXX executable zelda3d/zelda3d
exit 0

ctest --test-dir Shipwright/build-cmake --output-on-failure \
  -R '^mm3d_player_animation_policy_test$'
1/1 passed
```

The serialized live gate used the real debug-save Human equipment (Kokiri
sword + Hero shield) and the normal scripted-pad input path. Renderer model 0
reported these exact masks:

```text
idle:       0x0000000370000028
R held:     0x0000000370000020
R released: 0x0000000370000028
```

The `0x8` Hero back-shield bit disappears only while the shipping R-shield
path changes sheath type 14 to 12; the `0x20` sheathed Kokiri sword remains.
This proves the selector is live and state-driven. It is not full visual Player
parity: the later right-hand selector still owns the shield-in-hand mesh and
remains open on the RE frontier.
