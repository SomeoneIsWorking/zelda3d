# MM Player left-hand, sword, and bottle visibility selector

## Root cause

The five-form base mask deliberately hides all mutually exclusive left-hand,
sword, bottle, bottle-content, and Deku-stick groups. The native MM Player path
had already restored the retail sheath and right-hand groups, but no owner
implemented `FUN_00211aa4`, so those left-side groups remained absent in every
state.

## Retail evidence

The complete ARM function is `0x00211aa4..0x00211f8c`. Its literal pools point
to the open/closed/default hand tables, bottle hand/content tables, animation
override table, draw state, and exact action callbacks. The recovered
duplicate-LOD mesh rows in FD/Goron/Zora/Deku/Human order are:

```text
open             2, 2, 1, 1, 21
closed           1, 1, 2, 1, 20
one-hand sword   8, 2, 2, 1, 12
two-hand sword   8, 2, 2, 1, 18
bottle hand      6, 8, 8, 6, 0
bottle contents  7, 9, 9, 7, 24
```

`FUN_00219aa0` at `0x00219aa0` supplies the Human Kokiri/Razor/Gilded sword
override from `0x0068ee50` (`12/14/16`) and the otherwise easy-to-miss Fierce
Deity rule that mesh 8 also enables mesh 1. Exact branch precedence covers the
bottle item-change frame-13 split, Zora boomerang/open state, movement closure,
linkb closed/open override, Zora guitar mesh 10, Giant's Mask, the Human sword
override, bottle-content visibility, and additive Deku-stick mesh 27.

The shared retail GAR confirms every numeric animation ID used by the
selector: bottle clips `0x5a..0x62`, item change `0x107`, Deku drink
`0x26b..0x26d`, and Zora guitar `0x2c0/0x2c1/0x2c2/0x2d3`. Exact CMB
inventory checks found every selected mesh group in the corresponding form
body. Full addresses, table contents, and typed field alignment are recorded
in `mm3d-decomp/docs/player_draw.md`.

## Port ownership

`mm3d_player_left_hand_policy.{cpp,h}` owns the pure retail visibility policy.
`mm3d_player_left_hand.{cpp,h}` is the narrow adapter from typed 2S2H Player,
save, animation, action-callback, and live display-list table state. It rejects
unknown table pointers and out-of-range linkb hand indices. The Player draw
composer ORs the selected left-hand groups into the same one-shot mask as the
base, sheath, and right-hand owners.

The sword equipment enum conversion was moved into
`PlayerSwordFromRetailIndex` in the sheath policy so both equipment selectors
use one checked mapping rather than duplicate switch statements.

## Focused evidence

The isolated Clang gate, with the retail ROM environment loaded, passed all
policy, typed-adapter, archive, and CMB checks:

```text
CXX=clang++ uv run --frozen python tools/test_mm3d_player_left_hand.py
Ran 3 tests -- OK

CXX=clang++ uv run --frozen python tools/test_mm3d_player_sheath_policy.py
Ran 1 test -- OK

CXX=clang++ uv run --frozen python tools/test_mm3d_player_right_hand.py
Ran 3 tests -- OK

CXX=clang++ uv run --frozen python tools/test_mm3d_player_contracts.py
Ran 8 tests -- OK
```

The adapter tests include negative cases for an unknown default table, an
invalid linkb index, and a disabled bottle button. They also prove the retail
HUD-visibility exception restores that disabled button's item. No shared build
or game instance ran for this batch.

## Live Zora-boomerang producer proof

The generic `linkinfo` diagnostic now reports the shipping left-hand adapter's
mask and the typed `zoraBoomerangActor` identity. A serialized headless run in
Great Bay Coast transformed through the normal Zora-mask path, held B to charge,
then released the real scripted pad. It produced this exact transition:

```text
charge:    leftHandType=1 leftHandMask=0x0000000000000004 zoraBoomerangActorId=-1
in flight: leftHandType=1 leftHandMask=0x0000000000000002 zoraBoomerangActorId=32
caught:    leftHandType=1 leftHandMask=0x0000000000000004 zoraBoomerangActorId=-1
```

Actor ID 32 is `ACTOR_EN_BOOM`; the in-flight state also carried
`PLAYER_STATE1_ZORA_BOOMERANG_THROWN`. Because `leftHandType` stayed 1, the
existing retail branch requiring type 4 plus that state flag could not select
the open hand. The actor-lifetime producer changed the mask from closed mesh 2
to open mesh 1, then the native destructor restored the closed hand. The full
probe transcript is cached at
`scratch/logs/mm_n2/zora_boomerang_probe.txt`; no oracle rerun was needed.
The diagnostic owner was migrated from C `snprintf` construction to typed
`fmt::format` in `mm3d_link_repl.cpp`, preserving the established numeric
`linkinfo` contract while making the touched owner clang-tidy clean.

The final combined Clang build passed `mm_core` and `zelda3d_app`; the focused
adapter, REPL-structure, and Player-contract suite passed 19/19; and the locked
retail-asset `lus_tests` run passed 461/461. Direct clang-format and clang-tidy
checks are clean for every touched C/C++ owner. The normal repository verifier
still stops on the same eleven unrelated oversized SoH enhancement files
tracked by issue 0022; none is in this change.

## Remaining gaps

- MM3D `Player+0x129bc` bit 16 has multiple producers. The exact `En_Boom`
  producer is now ported and live-proven. Player helper `0x002250f0` separately
  drives the bit around a mount transition; its exact typed timing predicate
  remains unresolved and inactive.
- Retail-only Zora `pz_gakkiwait` and `pz_gakki_demo` have no independent
  typed N64 animation symbols. The shared N64 play/start cases are aligned.
- Authentic live sword and instrument submission captures remain open. The
  bottle path is covered by the later live control below.

## Bottle material follow-up

The previously named "joint-transform" stage is a material-constant write.
Retail `0x00211c90..0x00211cc8` calls `FUN_0020ce94`, which forwards to the
generic `FUN_001ff274`/`FUN_00223fc8` RGBA writer. The exact call targets
constant zero on form materials `6/3/4/3/5`; all five shipping targets bind
`p_bin_00` and source `CONST[0]` in the same TEV chain. The 23 four-float
records at `0x006269c4` are byte-normalized item colours, not position,
rotation, or scale values.

`mm3d_player_bottle_material_policy.{cpp,h}` now owns the recovered table and
form-material mapping. It reuses the left-hand selector's bottle-route/content
index and returns the override through the same typed production result as the
mesh mask. `mm3d_player.c` writes it to the existing emit-ordered material
override seam before the pending Player draw is captured. Exact addresses,
RGBA rows, and the writer chain are recorded in
`mm3d-decomp/docs/player_draw.md`.

## Deku spin material follow-up

The next cohesive `Player_Draw` stage is the Deku-only material-alpha write at
`0x001f9c9c..0x001f9d18`. Retail initializes alpha to zero, raises it across
the opening portion of typed `Player_Action_95`, holds it at one, then lowers
it as `Player::unk_B10[1]` approaches zero. The writer chain is
`0x001f9038 -> 0x0020ce94 -> 0x001ff274 -> FUN_00223fc8`; mode 2 replaces
only alpha in material 6, constant 4.

The typed alignment is not address inference: retail setup
`0x001f02bc..0x001f0318` installs action `0x00200974` and initializes the two
phase values to the same `20000.0f`/`196608.0f` used by 2S2H. Decompilation of
that action matches `Player_Action_95`'s flags, attack cylinder, `-800.0f`
step, yaw rotation, and termination behavior. The shipping Deku CMB confirms
the write's consumers: material 6/constant 4 is used only by mesh IDs
`11,12,13` and binds `link_nuts_f00`.

`mm3d_player_deku_spin_material_policy.{cpp,h}` now owns the exact four-word
fade formula. `mm3d_player_deku_spin_material.{cpp,h}` adapts typed form,
action callback, and `unk_B10[1]`; the Player composer submits the result
through the existing emit-ordered constant channel. The focused policy,
adapter, production-seam, and exact-CMB gate passes 4/4 with the locked asset
environment, and the combined Clang build passes `mm_core`, `soh_core`,
`lus_tests`, and `zelda3d_app`.

## Authentic bottle control and live material proof

The earlier `linkstate itemuse` control only installed an animation. It did not
equip an item or call MM's item-use owner, so it could not prove the bottle
selector in the shipping path. The generic replacement keeps state ownership
in the game: `linkequip <c-left|c-down|c-right> <ItemId>` writes the selected
save button item and reloads its icon, while `linkitem <ItemId>` delegates to
`Player_UseItem`. Both commands reject malformed slots and values outside the
retail byte-sized ItemId range.

A headless native-MM run equipped and requested empty bottle ItemId `0x12`.
After the asynchronous transition settled, typed state reported
`itemAction=21`, `heldItemAction=21`, and `heldItemId=18`. The renderer trace
changed from put-away mask `0x0000000370a00028` to held mask
`0x0000000370800029`: the only changed bits were 0 and 21, exactly replacing
the default closed Human hand with the empty-bottle mesh while every other
Player group stayed fixed. The live material diagnostic reported model 0,
material 5, constant 0 as `(0,0,0,0)`, the exact empty-bottle row.

The diagnostic also demonstrated the required other answer. Equipping and
requesting fish ItemId `0x1a` selected `itemAction=22` and repeatedly emitted
model 0, material 5, constant 0 as `(0,0.498,1,1)`, matching the exact fish
row before the ordinary action put the item away. This rules out an inert or
all-zero material probe. The controls establish live selector/material
delivery; the newly ported Deku-spin stage still needs its own authentic live
spin capture.
