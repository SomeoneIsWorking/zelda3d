# MM Player sheath, back-shield, and right-hand selectors

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
base mask before its one material-state snapshot.

The following right-hand stage is now separately owned by
`mm3d_player_right_hand_policy.{cpp,h}` and the narrow typed adapter
`mm3d_player_right_hand.{cpp,h}`. Keeping the two selectors separate preserves
their retail responsibilities and branch order. The independent left-hand
stage in `FUN_00211aa4` remains absent.

## Follow-on right-hand recovery

`FUN_00201074` tail-shares the complete right-hand selector at
`0x00211fd4..0x00212124`. Its literal pool at `0x00212128..0x00212140` names:

- state mask `0x402` (`PLAYER_STATE1_2 | PLAYER_STATE1_400`);
- live draw state `0x006919bc`;
- closed-hand table `0x006913ac`;
- held-shield table `0x00691514`;
- animation override table `0x00691a64`;
- carry-action callback `FUN_001ef758`;
- animation-open table `0x00691384`.

The recovered branch order selects Zora mesh 4 for its state/land-boots cases;
Human mesh 10/11 for Hero/Mirror shields held in hand; per-form closed hands
while a nominally open hand moves above `2.0f`; linkb closed/open overrides;
Fierce Deity closed/carry-open hands under Giant's Mask; Deku mesh 4 for
animation IDs `0x26b/0x26c`; otherwise the retained live default table. The
adapter recognizes that table pointer independently of `rightHandType`, which
preserves cutscene `RH_FF` states that intentionally keep their previous hand.

`FUN_001ef758` is grounded as typed 2S2H
`Player_UpperAction_CarryActor`: both implementations test
`PLAYER_STATE1_CARRYING_ACTOR`, handle `heldActor`, contain Cucco ID `0x11`, and
write gravity `-0.5f` plus terminal velocity `-2.0f`. The linkb override is
aligned through typed `PlayerAnimationFrame.appearanceInfo`; zero means none
and `0x0100/0x0200` decode to the same closed/open table indices.

The retail shared GAR contains exactly 1,694 entries: 847 CSABs followed by
847 paired linkb entries. IDs `0x26b/0x26c` are the exact
`nuts/anim/pn_drink.csab` and `pn_drinkend.csab` pairs, not texture-name
inferences. Five exact CMB inventory checks found every selected mesh: FD 4/5,
Goron 4/5, Zora 4/5, Deku 3/4, and Human 2/9/10/11/22/23/25.

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
parity: that capture predates the right-hand port and therefore proves only the
sheath transition, not the shield-in-hand mesh.

The completed right-hand policy/adapter/retail-asset checks passed 3/3 and the
MM contracts passed 8/8 (11/11 combined). The serialized Clang `mm_core` build
and `mm3d_player_animation_policy_test` CTest passed 1/1; direct clang-tidy
passed all three touched production translation units, and format/diff checks
were clean. No right-hand game instance was launched for this batch. The
honest remaining gaps are an authentic live held-shield/weapon/instrument
capture and the independent left-hand selector at `FUN_00211aa4`.
