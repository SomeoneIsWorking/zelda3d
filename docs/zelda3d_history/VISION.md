# Zelda3D — product vision & requirements

The *what* and *why* (UX/product). The *how* (technical strategy, reuse, layering)
is in `ARCHITECTURE.md`; this file is the requirements it must satisfy. Captured
from the user directly — keep it current as the vision sharpens.

## The product

One polished PC app that is the **definitive 3D edition of Ocarina of Time and
Majora's Mask** — both games, unified under one renderer, one UI, one launcher,
with modern PC conveniences.

## Reuse, don't reinvent (see ARCHITECTURE.md)

- **OoT** = soh3d (Ship of Harkinian rendering OoT3D / Grezzo 3DS assets).
- **MM** = Zelda64Recomp.
- **Unified renderer + UI** = soh3d's existing single **SDL3 GPU** backend (N64 +
  3DS one pass) + **RmlUi**.
- **Move off the SoH *framework*** (OTR/resource system → libultraship host →
  Shipwright bulk) **gradually**, AFTER a unified build works — never by discarding
  the game or its enhancements.

## Enhancements to PRESERVE (do not lose these moving off SoH)

- **SoH stock gameplay enhancements** the user likes: faster block pushing, faster
  climbing, faster King Zora (mweep), text/cutscene time-savers, and similar.
- **Our own custom enhancements** built on top: **press-to-skip onepoint cutscene
  cameras** (`Shipwright/soh/src/soh3d/soh3d.c` "#2 press-to-skip"), plus the rest
  of the `src/soh3d/` work (3DS rendering, behaviors, anim retargeting).

## Input & controls (user, 2026-06-30)

A first-class PC control scheme, not an N64-pad emulation:

- **Intuitive keyboard & mouse**: WASD to move, mouse to look/aim, **number keys
  1–8 to select/use items** directly (no C-button juggling).
- **Controller hotswap**: detect controller connect/disconnect at runtime and
  switch the active input device live — **and the on-screen UI/prompts adapt to the
  active device** (KB&M key glyphs vs. the connected controller's button glyphs;
  the PromptFont glyph set is already vendored for this).
- **Expanded item mappings**: go beyond the N64's three C-buttons — bind items to
  the **D-Pad, B, Y, and modifier combos like R1 + A/B/X/Y**, giving many more
  direct item slots than the original.

## Rendering

- **One unified renderer** for both games (soh3d's SDL3 GPU backend); no N64-vs-3DS
  split, no per-game renderer. RmlUi for all UI/menus on that same backend.

## Sequencing

1. **Unify now, reuse as-is** — soh3d (OoT) + Zelda64Recomp (MM) under the one
   renderer/UI/launcher; fastest path to a working unified build.
2. **Shed SoH gradually** — peel away OTR → libultraship host → Shipwright bulk.
3. **Layer in the PC UX** — KB&M scheme, controller hotswap + adaptive prompts,
   expanded item mappings, keeping all preserved enhancements.

## Status

See `README.md`. Done so far: `cmb3d` asset core (both games, verified); the RmlUi
OoT/MM launcher (reused from Zelda64Recomp). Next: the unification per ARCHITECTURE.
