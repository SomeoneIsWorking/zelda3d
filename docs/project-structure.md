# Project structure & naming — the canonical map

This is the single source of truth for what this project **is** and how its parts are named. Every
doc, commit message, and instruction file should use these terms consistently.

## The two-tier taxonomy

The project is two layers. **zelda** = the N64-asset PC-port engines (the Shipwright family, which
render the game from N64 assets). **zelda3d** = the 3DS-asset render layer we build *on top of* those
engines, substituting OoT3D / MM3D (Nintendo 3DS) models, world, animation, camera-math and lighting
for the N64 originals. Each tier has an OoT branch and an MM branch:

```
zelda   (base: N64-asset PC ports)          zelda3d  (our layer: 3DS-asset render, built on zelda)
├── soh    = Ship of Harkinian    (OoT)      ├── soh3d    = OoT3D rendered on soh
└── 2ship  = 2 Ship 2 Harkinian   (MM)       └── 2ship3d  = MM3D rendered on 2ship
```

- **zelda3d is a layer on zelda, not a sibling.** The zelda3d code lives *inside* each engine
  (`soh/src/zelda3d/`, `mm/2s2h/zelda3d/`) and falls through to the N64 (zelda) path for anything not
  yet ported. `zelda3d` is also the C/C++ symbol prefix and namespace for that layer in BOTH engines.
- **soh3d / 2ship3d** name the two branches of the zelda3d layer. They are the human-facing project
  names; there is no separate "soh3d" or "2ship3d" build target — each is the zelda3d code within its
  engine's target (`soh` / `mm`).

## Where each part lives

| Term | What it is | Location | Symbol/file prefix |
|------|-----------|----------|--------------------|
| **zelda** | umbrella for the N64 base engines | `Shipwright/` | — |
| **soh** | Ship of Harkinian (OoT N64 PC port) | `Shipwright/soh/` | `soh` / N64 `z_*` |
| **2ship** | 2 Ship 2 Harkinian (MM N64 PC port) | `Shipwright/mm/` (dir/code say `2s2h` — see below) | `2s2h` / N64 `z_*` |
| shared base | libultraship (windowing, input, Fast3D, resources) | `Shipwright/libultraship/` | `Ship::` / `LUS::` |
| **zelda3d** | umbrella for the 3DS render layer + its shared code | see the two branches | `zelda3d` / `Zelda3D_` |
| **soh3d** | OoT3D render layer (in soh) | `Shipwright/soh/src/zelda3d/` | `zelda3d_*` / `Zelda3D_` |
| **2ship3d** | MM3D render layer (in 2ship) | `Shipwright/mm/2s2h/zelda3d/` | `mm3d_*` / `Zelda3D_` |
| shared zelda3d | unified Link/player across soh3d + 2ship3d | `Shipwright/zelda3d_shared/` | `Zelda3D_` |
| shared zelda3d | CMB (3DS model/texture format) library | `Shipwright/cmb3d/` | `cmb3d` |
| reference | OoT3D decomp (ground truth for soh3d) | `oot3d-decomp/` (submodule) | — |
| reference | MM3D decomp (ground truth for 2ship3d) | `mm3d-decomp/` (submodule) | — |

Build targets: `soh` (→ `Shipwright/build-cmake/soh/soh.elf`) and `mm`
(→ `Shipwright/build-cmake/mm/mm.elf`). Run via `tools/zelda3d_game.sh` (soh) / `tools/mm_game.sh` (mm).

## Naming conventions (canonical human-facing name ↔ embedded code name)

The clean 6-term taxonomy is the human-facing vocabulary. Some of it is an **alias** over an embedded
code name we deliberately do NOT rename:

- **2ship = the vendored `2s2h`.** "2 Ship 2 Harkinian" is the upstream MM PC port; its own dir and
  code use `2s2h`/`mm` (660+ files). We keep `2s2h`/`mm` in code (renaming would diverge hard from the
  upstream tree for no functional gain) and use **2ship** as the canonical human-facing name in prose.
- **2ship3d = the MM3D render layer** at `mm/2s2h/zelda3d/`, whose files carry the `mm3d_*` prefix.
  `2ship3d`/`mm3d` refer to the same thing; prefer **2ship3d** in prose, `mm3d_*` stays the file prefix.
- **zelda3d** is BOTH the umbrella concept AND the literal code prefix/namespace (`Zelda3D_*`,
  `src/zelda3d/`) — it is shared by soh3d and 2ship3d, which is why the prefix is engine-neutral.
- **soh3d** is the OoT3D layer; its files carry the umbrella `zelda3d_*` prefix (they live in
  `soh/src/zelda3d/`). "soh3d" is the branch name, `zelda3d_*` the file prefix — not a contradiction.

The repo/working dir and the GitHub repo are named `soh3d` for historical reasons (the OoT work came
first); it now hosts both soh3d and 2ship3d under the zelda3d umbrella. Renaming the repo to `zelda3d`
would better reflect that but is disruptive (remote URL, `.env`, all tooling paths) — not done.

## Code names — RESOLVED: keep the embedded names, no renames

The user is indifferent to `2ship` vs `2s2h` as a label (2026-07-17), so the code keeps its embedded
names and prose may use either. No renames are done:

- `2s2h` / `mm` stay (renaming to `2ship` would be ~660 files diverging from upstream 2 Ship 2 Harkinian
  for zero functional gain).
- `mm3d_*` stays as the 2ship3d layer's file prefix (renaming to `2ship3d_*` is ~28 files of churn with
  no benefit the user cares about).

Prose uses the clean taxonomy (zelda / zelda3d · soh / soh3d · 2ship / 2ship3d); code keeps `2s2h`,
`mm`, `mm3d_*`, `zelda3d_*`. `2ship`≡`2s2h` and `2ship3d`≡`mm3d` are interchangeable.
