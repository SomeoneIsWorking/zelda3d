# SoH3D — progress & state

Goal: make **Ship of Harkinian** render **OoT3D (3DS)** character models and world
geometry instead of the N64 assets. Asset-conversion + renderer-integration task
(not a renderer merge). Azahar (3DS emulator) is built as the **visual oracle**.

## Layout
- `tools/` — 3DS asset toolchain (Python, dependency-free for extraction).
- `scripts/` — dependency install scripts (Fedora; run with sudo yourself).
- `Shipwright/` — SoH fork (own git, gitignored from parent).
- `Azahar/` — 3DS emulator fork, used as oracle (own git, gitignored from parent).
- `3ds/` — boot9/boot11 (not needed for extraction; for emulator). gitignored.
- `scratch/` — extraction output, logs, screenshots. gitignored.

## Done
- ROMs verified: N64 OoT USA v1.0 (`/mnt/Boy/ROM/N64/...z64`, supported hash),
  OoT3D USA decrypted NCSD (`/mnt/Boy/ROM/3DS/...3ds`).
- 3DS extraction pipeline, **verified on the pot** (`zelda_tsubo`):
  - `tools/ctr_romfs.py` NCSD→NCCH→RomFS (IVFC), no bootrom needed.
  - `tools/zar.py` ZAR archives.
  - `tools/cmb.py` CMB models: geometry/skeleton/material+texture refs. OBJ export.
  - `tools/pica_texture.py` PICA textures (ETC1 + 8x8 Morton-tiled formats).
  - pot → 130 verts / 160 tris, ETC1 + RGB565 textures decode correctly.
- **SoH builds & RUNS**: `soh.elf` (57 MB). Game archive `oot.o2r` (33 MB) +
  `soh.o2r` generated and staged in `build-cmake/soh/`. Boots headlessly to the
  title screen, **render captured** (scratch/screenshots/soh_vanilla.png).
- **Azahar builds**: `Azahar/build/bin/Release/azahar` (49 MB), Qt6 frontend.
- **Headless frame-dump tool** added to libultraship (branch `soh3d` in the
  submodule): `SOH_FRAMEDUMP=<path.ppm> SOH_FRAMEDUMP_FRAME=N ./soh.elf` under
  `xvfb-run`. Reusable for A/B vs the oracle. (exit 139 on teardown after dump is
  harmless — PPM is written before exit.)

### How to run SoH headless + capture
```
cd Shipwright/build-cmake/soh
SOH_FRAMEDUMP=/abs/out.ppm SOH_FRAMEDUMP_FRAME=500 \
  timeout 150 xvfb-run -a -s "-screen 0 1280x720x24" ./soh.elf
```

## In progress / next
- **Azahar Qt frontend build**: needs Qt6 — run `scripts/install_azahar_deps.sh`,
  then reconfigure with `-DENABLE_QT=ON` and build `citra_qt`. (Azahar has NO
  standalone SDL frontend; `citra_cli` is just a static helper lib.)
- **SoH game assets**: `GenerateSohOtr` uses `--norom` (only builds `soh.o2r`).
  The game `oot.o2r` is produced by SoH's built-in extractor at first launch
  (point it at the staged ROM `Shipwright/OTRExporter/oot_ntsc10.z64`).
- Then: oracle instrumentation in Azahar `video_core` (dump geometry/material/
  texture state + framebuffer for a target model), and the SoH-side integration
  (new 3DS-model resource + draw path, hooked where the N64 model is drawn).

## Integration design (SoH render path)

Draw path (pot example): `ObjTsubo_Draw` → `Gfx_DrawDListOpa(play, dlist)`
(`soh/src/code/z_cheap_proc.c`). That fn does:
1. `Gfx_SetupDL_25Opa` (state), 2. `gSPMatrix(..., MATRIX_NEWMTX, MODELVIEW|LOAD)`
— loads the actor's already-built world matrix, 3. `gSPDisplayList(dlist)`.
So the actor transform is on the RSP matrix stack before the dlist; any
replacement draws at the correct place for free. SoH already wraps pot draw in
GameInteractor `VB_POT_SETUP_DRAW` hooks — a clean place to divert.

Two integration layers:
- **A. CMB→F3DEX2 conversion** (no LUS changes): convert CMB to an N64 display
  list + N64-format texture, feed to `Gfx_DrawDListOpa`. Fast, reuses everything,
  but hits N64 limits (TMEM 4KB: pot's 64x128 tex won't fit in one load; s16
  verts; limited lighting). Good only as a stepping stone.
- **B. Native model draw path** (chosen end state): new Fast3D opcode / resource
  in libultraship that, at gfx_pc interpret time, takes the current MV+proj matrix
  and renders a native CMB mesh (full-res RGBA texture bound directly, bypassing
  TMEM) through the modern gfx backend. Hook actor draw (GameInteractor `VB_` or a
  per-object table) to emit it instead of the N64 dlist. Real 3DS quality.

Plan: implement B. First milestone — pot in-game via the native path, A/B'd
against the Azahar oracle render.

To generate the **game** archive (oot.o2r) headlessly:
`python3 OTRExporter/extract_assets.py -z build-cmake/ZAPD/ZAPD.out --non-interactive <rom>`
(run from `Shipwright/soh`-relative as the build does; do after Azahar build to
avoid CPU contention).

## Gotchas learned (CMB; wiki is partly wrong / MM3D-mixed)
- OoT3D cmb version = 6; MM3D = 0x0A. OoT3D has NO tangent attribute.
- SEPD VertexList stride = 0x1C (includes constant vec4), not 0x14.
- VATR slice entry = (size u32, offset u32) — size FIRST.
- Bone struct stride = 0x28. PRM indices are global into VATR (start + idx*stride).
- Texture glFormat = (dataType<<16) | formatConstant.
