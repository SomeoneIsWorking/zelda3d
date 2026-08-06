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
| **2ship** | 2 Ship 2 Harkinian (MM N64 PC port) | `2ship/` (dir/code say `2s2h` — see below) | `2s2h` / N64 `z_*` |
| shared base | libultraship (windowing, input, Fast3D, resources) | `Shipwright/libultraship/` | `Ship::` / `LUS::` |
| **zelda3d** | umbrella for the 3DS render layer + its shared code | see the two branches | `zelda3d` / `Zelda3D_` |
| **soh3d** | OoT3D render layer (in soh) | `Shipwright/soh/src/zelda3d/` | `zelda3d_*` / `Zelda3D_` |
| **2ship3d** | MM3D render layer (in 2ship) | `2ship/2s2h/zelda3d/` | `mm3d_*` / `Zelda3D_` |
| shared zelda3d | unified Link/player across soh3d + 2ship3d | `Shipwright/zelda3d_shared/` | `Zelda3D_` |
| shared zelda3d | CMB (3DS model/texture format) library | `Shipwright/cmb3d/` | `cmb3d` |
| reference | OoT3D decomp (ground truth for soh3d) | `oot3d-decomp/` (submodule) | — |
| reference | MM3D decomp (ground truth for 2ship3d) | `mm3d-decomp/` (submodule) | — |

Build targets: `soh` (→ `Shipwright/build-cmake/soh/soh.elf`) and `mm`
(→ `Shipwright/build-cmake/mm/mm.elf`). Run via `tools/zelda3d_game.sh` (soh) / `tools/mm_game.sh` (mm).

## The build layering — one runtime, one engine, two peer games

The configure root is the **repo root** (`/CMakeLists.txt`). Configure with `cmake -S . -B
Shipwright/build-cmake -G Ninja`.

```
launcher   Shipwright/zelda3d_app     one binary; dlopens a game core, holds no game code
             │
games      Shipwright/soh   (OoT)  ── peers. Neither hosts the other.
           2ship            (MM)
             │
shared     Shipwright/zelda3d_shared  port code common to both games (two mechanisms — see below)
             │
engine     Shipwright/libultraship    window/renderer/input/resources — knows no game
           Shipwright/cmb3d           3DS asset formats (CMB/CSAB/ZAR/ZSI/…)
```

This mirrors Dusklight's `aurora` (engine) / `borealis` (app services) / `dusk` (port layer) /
decomp split — see `docs/dusklight-adoption.md`. The layer Dusklight has no need for is **shared**:
it hosts one game, we host two.

**Until 2026-08-06 the configure root was `Shipwright/CMakeLists.txt` — OoT's own directory** — and
it reached out of its tree with `add_subdirectory(${CMAKE_SOURCE_DIR}/../2ship)`. That was not just
untidy: because `CMAKE_SOURCE_DIR` *was* `Shipwright/`, `2ship/CMakeLists.txt` located the engine's
RmlUi assets as `${CMAKE_SOURCE_DIR}/libultraship/assets/rml`, i.e. MM silently depended on OoT's
directory being the build root. Paths are now named (`ZELDA3D_ENGINE_DIR`, `ZELDA3D_OOT_DIR`,
`ZELDA3D_MM_DIR`, `ZELDA3D_ENGINE_ASSETS_DIR`, set in the root `CMakeLists.txt`), so no component
has to know which directory the build was configured from. Each game also carries a fallback for
those variables so it still configures standalone.

### Still structurally wrong: the game chooser lives inside one of the games

`Shipwright/zelda3d_app/zelda3d_main.cpp` holds no game code — but the **RmlUi chooser it should be
presenting still runs as an OoT gamestate inside the soh core** (`soh/src/zelda3d/launcher/`). So
picking a game requires OoT to have booted first, which is the same "OoT is the host" inversion the
build root had, one layer up. The launcher currently chooses from argv/env instead, which is what the
headless tooling needs anyway.

Moving it means owning a `Ship::Context` before any core is loaded and handing it over — the
"Ship::Context ownership" half of N3 in `docs/MM_NATIVE.md`. Dusklight's model for exactly this is
`launchUILoop()` (`src/m_Do/m_Do_main.cpp:158`): a second, simpler frame loop — events →
`aurora_begin_frame` → `ui::update()` → `aurora_end_frame` — with **no game executing at all**, used
for its prelaunch/disc-picker screen. That is the shape to copy.

### Sharing code between the two games — TWO mechanisms, and the choice is forced

`zelda3d_shared/` offers two, because port code splits cleanly by whether it names a game type:

| | **`zelda3d_shared` STATIC LIB** | **`zelda3d_shared/port/` SHARED SOURCE** |
|---|---|---|
| Contract | sees **no** game-specific type — everything crosses as plain enums/PODs | may include `z64.h`, `Actor`, `PlayState` … |
| Compiled | **once**, linked by both games | **once per game**, into each game's own target |
| Copies of the code | one binary, one source | two binaries, **one source** |
| Use for | asset/format/policy code (`cmb3d`, Link mesh-mask, the extractor's I/O) | the N64↔PC port glue the decomp calls into |

**Why the second mechanism has to exist.** `gu_pc.c` is *byte-identical* in both games — and it
includes `"z64.h"`, which is OoT's 2,354-line decomp master header in one tree and MM's 108-line one
in the other. Identical source, different compile context. A static library is compiled once against
one include path, so it physically cannot hold that file; the only honest way to have one copy is
one *source* file pulled into both targets. Each game's CMakeLists globs `${ZELDA3D_SHARED_DIR}/port/`
into its own source list.

> **A similarity percentage is text, not code — the same trap as a grep count.** `mixer.c` measures
> ~99% common between the two games (21 differing lines in 822) and reads like pure copy-paste. It
> is not shareable: among those 21 lines is the audio DMEM base address, **0x3C0 in OoT vs 0x0330 in
> MM** — the two games' microcode memory layouts — plus MM's `ROUND_DOWN_16` on the DMA length.
> Merging on the strength of "99% identical" would have silently mis-addressed every audio buffer in
> one of the two games. Read the diff before believing the percentage.

### What is left to share — measured by SEAM, not by similarity

Ranked by value over risk. Each row states the *actual* divergence, because ranking these by
percentage-common put the two worst candidates at the top.

| Candidate | Size | The real seam | Verdict |
|---|---|---|---|
| `framebuffer_effects.{c,h}` | 177 | two: the port-shell include (`OTRGlobals.h` / `BenPort.h`) and the identity-matrix symbol (`gMtxClear` / `gIdentityMtx`) | **ready** — but the first seam is really the port-shell split below; do them together |
| Extractor (`Extract.cpp`, `FastCrc32C.c`) | ~800/side | 153 lines, **all** per-game ROM CRC tables and version names | **tractable refactor**: share the extraction logic, let each game supply a `RomVersionTable`. Gates a fresh install booting — verify by running a real extraction, not by compiling |
| GUI framework (`UIWidgets`, `Menu`, `MenuTypes`, `Notification`) | ~3.9k/side | 76–90% common; the residue is per-game widget sets and CVar names | **largest genuine win left.** Do it widget-by-widget, not in one merge |
| Port shell (`OTRGlobals.cpp` ↔ `BenPort.cpp`) | 2.8k / 2.4k | same class name, same `InitOTR`/`DeinitOTR` skeleton, different filename and per-game singletons | **structural, highest leverage.** Needs an interface split (shared lifecycle + per-game hooks), not a copy-merge |
| `resource/` importers + types | 8.4k / 7.4k | 156 matching names, 28 identical `.cpp` — but only **3** also have an identical header (claim C066) | **poor target.** Tops the similarity ranking and is nearly all header-driven per-game divergence |
| `mixer.c` | 822 | per-game audio DMEM base address (claim C065) | **do not share** until parameterised and both games' audio verified end to end |
| `CrashHandlerExt.cpp` | 94 / 79 | OoT walks `ActorDB` + `scene_table.h` + `ACTORCAT_MAX`; MM uses `GetActorCategoryName` and a different list-head field | **do not share** — only the outer scaffolding is common; the bodies are two different actor systems |

Already done: `gu_pc.c` and `mixer.h` (→ `port/`, shared source), `ObjectExtension` (→ the static
lib, since it names no game type). When picking up `framebuffer_effects`, note that its identity-matrix
seam is safe — OoT's `gMtxClear` and MM's `gIdentityMtx` are the *same* matrix despite the names, the
former hardcoding the packed N64 fixed-point words (claim C067).

`Enhancements/` game logic is genuinely per-game (mostly <50% common) and should stay forked. So are
`src/overlays/` and `src/code/` — two different decomps with **zero** byte-identical files despite
618 shared basenames.

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
