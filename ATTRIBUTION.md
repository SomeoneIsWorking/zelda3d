# Attribution

zelda3d builds on several other projects. This file records what was borrowed and under which terms.
It is a working record, not legal advice — where something below is marked **unverified**, it means
the licence was not confirmed from a file in this tree and should be checked against upstream before
the project is distributed.

## Project licence

**zelda3d as a whole is GPL-3.0** (see `LICENSE`).

The reason is Zelda64Recomp: its launcher and menu UI are GPL-3.0, and GPL-3.0 is copyleft, so any
work that incorporates them must be distributed under the same terms. The MIT-licensed components
below are GPL-compatible, so combining them is fine — their own files stay MIT, while the combined
distribution is GPL-3.0.

Note this was already true before it was written down: `Shipwright/libultraship/assets/rml/rml.rcss`
and `LatoLatin-Regular.ttf` are byte-identical copies of Zelda64Recomp's, and had been in the tree
without a `LICENSE` or this file. That is what prompted writing both.

## Borrowed from Zelda64Recomp — GPL-3.0

<https://github.com/Zelda64Recomp/Zelda64Recomp> (`COPYING`: GNU GPL v3)

- The **launcher and menu UI**: `launcher.rml`, `recomp.rcss`, `rml.rcss`, and the accompanying
  fonts, icons and SVG artwork.
- Their launcher is itself a two-game split screen, with Majora's Mask implemented and Ocarina of
  Time left as "Coming Soon™". zelda3d fills in the Ocarina side.

Not taken: their recompiler runtime, game logic, or patches — zelda3d renders OoT3D/MM3D over
Ship of Harkinian and 2S2H instead, which is a different approach entirely.

## Fonts

- **Lato** (`LatoLatin-*.ttf`) — SIL Open Font License 1.1. Licence text ships alongside the font at
  `Shipwright/libultraship/assets/rml/LatoLatin-LICENSE.txt`.
- **Chiaro**, **NotoEmoji**, **promptfont** — used by Zelda64Recomp's UI; each carries its own terms
  upstream. **Unverified here** — confirm before shipping any that get pulled in.

## Vendored engine components

Verified from a licence file in this tree:

| component | path | licence |
|---|---|---|
| libultraship | `Shipwright/libultraship/LICENSE` | MIT |
| ZAPDTR (our fork) | `Shipwright/ZAPDTR/LICENSE` | MIT |
| OTRExporter | `Shipwright/OTRExporter/LICENSE` | MIT |

**Unverified here** — no licence file is present in-tree for these, because the submodule flatten
dropped the upstream repo roots. Both are believed MIT upstream; confirm before distribution:

| component | path | upstream |
|---|---|---|
| Ship of Harkinian (soh) | `Shipwright/soh/` | <https://github.com/HarbourMasters/Shipwright> |
| 2 Ship 2 Harkinian (2s2h) | `2ship/` | <https://github.com/HarbourMasters/2ship2harkinian> |

## Third-party libraries

- **StormLib** — our fork of <https://github.com/ladislav-zezula/StormLib> (submodule).
- **RmlUi**, **prism**, **dr_libs**, **monocypher**, **libgfxd**, **tinyxml2** — pulled by CMake
  FetchContent or vendored inside ZAPDTR, each under its own upstream licence.

## Game assets

**No game assets are included in this repository, and none may be committed to it.** Ocarina of Time,
Majora's Mask, and their 3DS remakes are required separately and are supplied at runtime via a
gitignored `.env`. See `CLAUDE.md`.
