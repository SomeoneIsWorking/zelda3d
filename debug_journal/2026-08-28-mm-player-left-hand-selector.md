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

## Remaining gaps

- MM3D `Player+0x129bc` bit 16 is a private transient open-hand input set by
  the mount transition and two actor-contact functions. No exact typed 2S2H
  homolog is proved, so the adapter does not guess it.
- Retail-only Zora `pz_gakkiwait` and `pz_gakki_demo` have no independent
  typed N64 animation symbols. The shared N64 play/start cases are aligned.
- The 23-record bottle-content joint-transform stage in `FUN_00211aa4` is
  independent of group visibility and remains unported.
- The new selector has no authentic live sword/bottle submission capture yet.
