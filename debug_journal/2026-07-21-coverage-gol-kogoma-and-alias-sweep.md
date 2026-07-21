# 2026-07-21 — object→ZAR coverage: En_Goma→kogoma (verified live) + alias sweep results

## Win: gol → kogoma (committed `dc00ea93`, verified live)

`object_gol` (OBJECT_GOL 0x1E) is drawn by **En_Goma** — the baby-Gohma larva (`gObjectGolSkel`
+ egg-hatch behavior: `eggTimer`/`eggScale`/`eggYOffset`). OoT3D `/actor/zelda_kogoma.zar`'s sole
model is `childgoma.cmb` (子ゴーマ = child Gohma). Identity certain by CMB name → added ALIAS
`"gol": "kogoma"` in `tools/gen_object_zars.py`, regenerated the table.

**Verified end-to-end (live, headless, entrance 238 Kokiri Forest):** `spawn 0x2B` then
`actorsnear` reports `id=0x2B ... AUTO:/actor/zelda_kogoma.zar (skin)` (was `--N64--` before).
Screenshot `scratch/screenshots/en_goma_kogoma.png` shows the red-spotted Gohma egg rendering from
the 3DS asset. Build was a clean incremental rebuild (3 TUs including the table + link).

## Tooling fix (same commit): stale generator output path

`gen_object_zars.py` `OUT` still pointed at the pre-reorg
`Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc`, but the build now `#include`s
`../tables/zelda3d_object_zars.inc` (Phase 3 reorg). Running the generator silently created a
stray unreferenced `.inc` while the compiled table went unregenerated. `OUT` now points at
`tables/`.

## Alias sweep — remaining unknowns (mostly NOT clean gaps; do not force)

Systematic check of the "unknown" no-zar objects (actor → drawn skeleton → OoT3D CMB match). The
strict bar is "CMB name makes identity certain" (a wrong alias renders the wrong actor). Results:

- **fd2 → fd (Volvagia) — CANDIDATE, DEFERRED.** `object_fd2` is **Boss_Fd2** ("Volvagia, hole
  form", `gHoleVolvagiaSkel`). OoT3D `fd` zar's CMBs are all `valbasia*` (バルバジア = Volvagia's
  JP name) and INCLUDE `valbasiagnd.cmb` (ground/hole form). So fd2's model lives in the `fd`
  archive (already mapped by `object_fd`). NOT applied because: (1) `fd` has 11 CMBs and the
  auto-replace path may pick the flying body over `valbasiagnd` → wrong sub-model; (2) bosses need
  behavioral porting, not just a model swap (memory `soh3d-boss-faithful-port`); (3) needs a live
  boss-room render check with correct sub-model selection. Revisit as a boss-render task, not an
  alias one-liner.
- **ossan / masterkokiri / masterkokirihead** — all drawn by **En_Ossan**, a multi-shopkeeper actor
  that loads different NPC objects per shop type. No single-CMB certain match; needs per-variant
  mapping. Deferred.
- **bird** (En_Bird, `gBirdSkel`), **vase** (En_Vase), **nwc** (En_Nwc), **trap** (En_Trap),
  **human**, **oF1s**, **yabusame_point** — no OoT3D `/actor` zar with a certain CMB match. Several
  are markers/props with no separate actor model (yabusame_point) or scene-baked geometry.
- **Candidate-pool false gaps:** `ironknack` (already covered by `object_ik`→`zelda_ik.zar`, which
  contains `ironknack.cmb`), the `gi_*` menu models, `link_*` variants, `ganon_*` cutscene archives
  — duplicate/alternate archives nothing needs to alias. Not gaps.
- **Scene-object bundles** (`oA1..oB4`, `oE1..oE12`, `spot04_objects`, 30 total): no OoT3D `/actor`
  zar — in OoT3D these are baked into scene geometry, not separate archives. Need a per-scene
  scene-geometry path, not an alias. Out of scope for the alias mechanism.

**Conclusion:** clean object→ZAR alias wins are effectively exhausted; `gol→kogoma` was the one
genuine gap fillable by a certain-identity alias this pass. Remaining coverage lives in non-alias
paths (scene geometry, boss render, En_Ossan multi-NPC) that each need live verification.
