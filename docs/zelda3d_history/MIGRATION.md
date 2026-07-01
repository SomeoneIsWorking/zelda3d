# Zelda3D migration manifest — what moves from soh3d, and its status

> **⚠️ Premise corrected (user, 2026-06-30).** This doc was written assuming zelda3d
> is a *clean rebuild that drops SoH/libultraship and migrates out only "our" work*.
> That **misread the user's intent** — see the rewritten `ARCHITECTURE.md`. The real
> plan is to **reuse** soh3d (incl. its SDL3 GPU unified renderer + RmlUi) and
> Zelda64Recomp, and **unify** them — not to leave SoH behind. Only Layer 1 below
> (the `cmb3d` asset-core extraction, already done + verified) still stands as-is; the
> Layer 2/3 "migrate-out, rebuild clean" framing is superseded and pending a redesign
> of the integration topology with the user. Don't action Layer 2/3 here until that lands.
>
> **Update (2026-07-01): topology has landed.** The MM-side integration is now planned in
> **`docs/MM_INTEGRATION.md`** (one shared renderer; MM logic from Zelda64Recomp re-hosted on
> soh3d's SDL3-GPU renderer + `cmb3d`). The Layer-2/3 tables below remain a useful inventory of
> soh3d pieces, but the live plan + milestone ladder is in that doc.

Source of truth for the port out of `soh3d` (the SoH/libultraship-based prototype) into
this clean, unified repo. SoH and libultraship are **not** migrated — only *our* work is.

Three layers, by coupling to the old SoH host (the spine decision — which N64 host —
is tracked in `ARCHITECTURE.md`):

## Layer 1 — 3DS asset core  ◻ OoT3D done / MM3D in progress (`src/cmb3d/`)
Engine-agnostic. Parses every Grezzo/3DS format we need; shared by OoT3D **and** MM3D.
Only deps: C++ stdlib + `stb_image`. Builds clean as `libcmb3d.a` (`cmake --build build`).

**OoT3D: verified** — `tools/cmb3d_probe` loads 1387/1387 CMBs from the OoT3D ROM (all v6,
all skinned, 0 failures). **MM3D: not yet** — the probe found MM3D uses a *different* asset
stack: **GAR** archives (not ZAR) and **LzS** per-file compression, neither of which the core
parses yet. Full work-list + derived format headers in `docs/mm3d_asset_parity.md`. Order:
LzS decompressor → GAR parser → CMB v7 verify → ZSI Majora stride.

| soh3d path (`Shipwright/soh/src/soh3d/asset/`) | role |
|---|---|
| `cmb.*`            | CMB model loader (meshes, materials, skinning) |
| `csab.*`           | CSAB skeletal animation |
| `zar.*`            | ZAR archive container |
| `ctxb.* pica_texture.*` | CTXB textures + PICA200 decode (ETC1/tiled) |
| `zsi.* zcol.*`     | scene info + 3DS scene collision |
| `texpack.* cityhash.*` | hi-res texture-pack replacement (Citra legacy hash) |
| `ctr_rom.* mat4.h` | RomFS reader + math |

## Layer 2 — renderer (SDL3 GPU + RmlUi)  ⬜ TO MIGRATE  → `src/render/`
**Ours, and explicitly kept.** Written against the Fast3D `gfx` backend interface — the
same `gfx_pc` lineage the recomp/RT64 world uses, which is *why* it re-hosts cleanly.
Decouple from libultraship's `fast/` headers as it moves.

| soh3d path | role |
|---|---|
| `libultraship/src/fast/soh3d_sdl3gpu.cpp` + `backends/gfx_sdl3gpu.cpp` | the SDL3 GPU backend (the one renderer) |
| `libultraship/src/fast/soh3d_hud_sdl3gpu.cpp`, `soh3d_gl.cpp` | HUD quads + shared GPU helpers |
| `include/fast/soh3d_sg_ubo.h` | UBO/push-block layout (single source of truth) |
| `libultraship/src/ship/window/gui/rml/RmlRenderInterfaceSdl3Gpu.*` | RmlUi → SDL3 GPU render interface |
| `libultraship/src/ship/window/gui/rml/SohRmlUi.*` | RmlUi document/context host (menu) |

## Layer 3 — game integration + behaviors  ⬜ RE-TARGET (not copy)  → `src/game/`
The SoH-coupled layer: hooks Actor structs / Play state / Fast3D draw sites to substitute
3DS models. **Re-targeted onto the new N64 host**, not copied — this is where the host
decision bites. Asset-facing logic (model assembly, anim retarget, scene props) is
salvageable; the SoH hook glue is rewritten.

| soh3d path | role | disposition |
|---|---|---|
| `soh3d.c` | monolith: hooks, REPL, scene/actor wiring | split + re-target per CLAUDE.md "no giant file" rule |
| `soh3d_model.cpp`, `soh3d_model_internal.h` | CMB instance draw via the backend | mostly salvage (asset-facing) |
| `soh3d_anim*.cpp/.inc`, `soh3d_link*.cpp` | N64-anim → CSAB retarget, player draw | salvage logic, re-hook |
| `behaviors/actor/*` + `actor_behavior.*` | per-actor OOP behavior modules | salvage; rebind id-dispatch to host actor ids |
| `soh3d_hud_tex.cpp`, `*_png.h` | HUD glyph/icon textures | salvage |

## Tooling (stays in `soh3d` or re-homed as needed)
`tools/` (kanban, REPL, oracle, parity, geom sweeps), the Azahar 3DS oracle, and the
`oot3d-decomp` repo are **host-independent** and keep working. MM adds: an **MM3D Azahar
oracle** and **MM decomp** coverage alongside OoT.
