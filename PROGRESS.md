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
- ROMs verified: N64 OoT USA v1.0 (`<rom-dir>/ROM/N64/...z64`, supported hash),
  OoT3D USA decrypted NCSD (`<rom-dir>/ROM/3DS/...3ds`).
- 3DS extraction pipeline, **verified on the pot** (`zelda_tsubo`):
  - `tools/ctr_romfs.py` NCSD→NCCH→RomFS (IVFC), no bootrom needed.
  - `tools/zar.py` ZAR archives.
  - `tools/cmb.py` CMB models: geometry/skeleton/material+texture refs. OBJ export.
  - `tools/pica_texture.py` PICA textures (ETC1 + 8x8 Morton-tiled formats).
  - pot → 130 verts / 160 tris, ETC1 + RGB565 textures decode correctly.
- **SoH builds & RUNS**: `soh.elf` (57 MB). Game archive `oot.o2r` (33 MB) +
  `soh.o2r` generated and staged in `build-cmake/soh/`. Boots headlessly to the
  title screen, **render captured** (scratch/screenshots/soh_vanilla.png).
- **Azahar builds & RUNS OoT3D**: `azahar` (49 MB). Boots the ROM (title id
  0004000000033500), inits DSP/save archive, emulates without crashing. Run with
  `XDG_CONFIG_HOME`/`XDG_DATA_HOME` pointed at `scratch/azahar_{cfg,data}` to keep
  it out of the user's real config. Logs to `<data>/azahar-emu/log/azahar_log.txt`.
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

### MILESTONE — OoT3D pot renders in-game (2026-06-15)
First OoT3D asset rendered through SoH's modern renderer, in a live scene, with
its full-res 3DS texture at the correct world transform. **Approach A** (CMB →
F3DEX2 dlist + full-res RGBA32 texture), which turned out to be the right first
step — see the corrected TMEM finding below.

Pipeline:
- `tools/cmb_to_c.py <model.cmb> <out_base>` converts a CMB to a self-contained
  C source: a `Vtx[]`, the texture decoded to RGBA32 (`pica_texture.decode`),
  and an `Gfx[]` display list with **manual** texture-load commands carrying the
  *real* pixel width (so LUS uploads it at full res — NOT through 4 KB TMEM).
  Geometry is batched <=10 tris / gSPVertex load. Generated files contain
  ROM-derived assets → **gitignored, never committed**; regenerate from the ROM.
- `soh/src/soh3d/` — `soh3d.{c,h}` (env toggles) + the generated
  `soh3d_pot_model.{c,h}`. SoH globs `src/*.c` (GLOB_RECURSE) so new files are
  picked up after a `cmake build-cmake` reconfigure.
- Hook: `ObjTsubo_Draw` (z_obj_tsubo.c) calls `SoH3D_DrawModel(play, dl, actor,
  SOH3D_POT_WORLD_SCALE)` when `SOH3D=1`.

**Draw path — must build its own matrix (root cause).** Do NOT reuse
`Gfx_DrawDListOpa` for OoT3D models: it loads the actor's inherited matrix, which
carries the N64 0.01 actor scale (`Actor_Draw` does `Matrix_Scale(actor->scale)`
before the actor's Draw). With that inherited fixed-point matrix the OoT3D dlist
renders **nothing at all** (not just wrong-sized) — verified: the identical dlist
renders fine through a fresh `MTXMODE_NEW` matrix but is invisible through the
inherited one. `SoH3D_DrawModel` (soh3d.c) builds its own
translate+rotateY+scale matrix at the actor's world pos and emits the dlist, so
SoH3D owns the transform. Calibrated world scale: **0.12** (OoT3D pot ~162 model
units -> matches the N64 pot's on-screen height; tuned via the SOH3D_SPAWNPOT
spawn comparison in Deku Tree). Generated model is baked at --scale 1.0; the
world scale lives in `SOH3D_POT_WORLD_SCALE`.

Regenerate the pot model (from the extracted CMB):
```
python3 tools/cmb_to_c.py scratch/extract/tubo2_model.cmb \
  Shipwright/soh/src/soh3d/soh3d_pot_model
```

### Headless verification path (no input scripting)
Reaching an in-game scene headlessly is solved with an env-gated auto-warp that
reuses SoH's debug Select overlay + `Select_LoadGame`:
- `SOH3D_WARP=1` — boot straight into Select, then auto-warp. Default entrance
  Kakariko Village (`SOH3D_ENTRANCE=<decimal>` to override).
- `SOH3D=1` — enable OoT3D-model rendering (the `ObjTsubo_Draw` divert).
- `SOH3D_SPAWNPOT=1` — spawn one real Obj_Tsubo beside Link (the actual
  ObjTsubo_Draw path). A/B with SOH3D=0 (N64 pot) vs SOH3D=1 (OoT3D pot) in the
  same scene. params=0 needs the dungeon keep object, so use a dungeon
  (`SOH3D_ENTRANCE=0` = Deku Tree).
Verified renders: `scratch/screenshots/final_3ds.png` (OoT3D pot via the real
ObjTsubo_Draw path, Deku Tree, calibrated size) and `full_n64.png` vs
`full_s012.png` (N64 vs OoT3D height match). Code: graph.c (boot→Select),
z_select.c (auto-warp), z_play.c + soh3d.c (spawn helper).

### CORRECTION — there is NO 4 KB TMEM limit in the LUS modern path
PROGRESS/handoff previously said approach A hits "TMEM 4 KB: pot's 64x128 tex
won't fit". **Wrong.** `Interpreter::ImportTextureRgba32` reads the texture
straight from the source pointer at the tile's full dimensions; the 4 KB assert
in `GfxDpLoadBlock` is commented out, and the code explicitly supports
manually-built DLs that set the real pixel width. This is the same path SoH's HD
texture packs use. So approach A renders full-res textures fine — it is the
correct first milestone, not just a stepping stone. Approach B (native opcode)
is only needed later if per-vertex lighting/normals fidelity demands it.

### MILESTONE — multi-material model (Gossip Stone) renders in-game (2026-06-15, session 3)
Generalised the pipeline from the 1-mesh/1-material pot to genuine multi-mesh,
multi-material, multi-texture models, and proved it on the **OoT3D Gossip Stone**
(`zelda_gs.zar` -> `gossip_stone2_model.cmb`: 2 meshes, 2 materials, 2 distinct
fully-opaque 128x128 / 128x64 textures), hooked via `EnGs_Draw` + `SOH3D_SPAWNGS`.

Toolchain changes:
- `cmb.py` now parses **MATS** (per-material primary texture index + wrap modes +
  UV coordinator scale/translate + alpha test) and computes **bind-pose bone
  matrices**; `triangles()` applies each mesh's bound-bone world matrix. Multi-bone
  props (e.g. the treasure-chest lid, bone 2) are stored in bone-LOCAL space and
  render scrambled without this — verified on `tr_box` (lid then sits atop the base).
  The single-bone pot never exposed it (bone 0 = identity).
- `cmb_to_c.py` groups triangles by material and re-binds texture+combiner per
  material in one dlist (multiple RGBA32 texture arrays in the C file).

**Root cause of the "model renders solid black" bug (the real one).** Clearing the
`G_FOG` *geometry-mode* bit stops the RSP computing per-vertex fog, but the RDP
**blender** configured by the caller's SetupDL can still blend the framebuffer
toward the scene fog colour. In a foggy scene (Kakariko at night, fog ~ (0,0,30))
that painted the *entire* model the fog colour regardless of texture/combiner — a
solid black/blue silhouette. Proven by bisection: a solid-red PRIMITIVE combiner
*also* rendered black, so it was downstream of the combiner. Fix: the converter
emits an explicit `gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2)`.
The pot only escaped this because Deku Tree's fog setup didn't tint.

**Verified QUANTITATIVELY (do not eyeball — see `tools/compare_render.py` and the
[[verify-quantitatively]] memory).** Lower-frame pixels matching tex0's gray-green
palette: **0.2% before the fix (fog-black) -> 33.6% after**; the N64 Gossip Stone
scores 0.2% (its stone is gray-blue, so the green is unambiguously the OoT3D
texture). Calibrated world scale ~0.13 (`SOH3D_GS_WORLD_SCALE`).

**Dead ends / corrected notes from this session (don't re-walk):**
- The `lrs` 12-bit truncation in `gsDPLoadBlock` (>4096 texels truncate) is REAL but
  IRRELEVANT to our textures: wide vs non-wide `gsDPLoadBlock` produced a
  pixel-identical pot render, so SoH3D's static textures load fully via the
  resource/cache path regardless. Kept plain `gsDPLoadBlock` (proven primitive).
- The treasure chest (`tr_box`) is a POOR multi-material test: both its main textures
  are ~95-100% transparent decals composited by a PICA multi-texture fragment
  combiner — no single binding-0 texture is the visible surface, so the single-texture
  converter renders it wrong. PICA combiner emulation is future work. Pick models
  whose materials each use a distinct mostly-opaque texture (the Gossip Stone does).

### MILESTONE — flat scene-ambient tint (color fidelity) (2026-06-15, session 4)
Unlit OoT3D models rendered full-bright: they did NOT darken with the room and
sat ~2.5x too bright vs the N64 model in the same scene. Fixed without
reintroducing per-vertex lighting banding.

**How.** The unlit dlist now modulates the texture by the **PRIMITIVE** register
instead of vertex SHADE: `cmb_to_c.py` emits `G_CC_MODULATERGBA_PRIM` (= TEXEL0 *
PRIM) for both the opaque and alpha-test material paths (still `G_CC_MODULATERGBA`
under `--lit`). `SoH3D_DrawModel` (soh3d.c) computes ONE flat tint colour from the
LIVE interpolated scene lights — `play->envCtx.lightSettings`: `ambient + 0.5 *
(light1Color + light2Color)`, clamped — and emits `gDPSetPrimColor` before the
dlist. Reading it live means the model tracks time-of-day automatically; one prim
colour for the whole dlist means it's flat by construction (no banding). The dlist
deliberately emits no prim colour of its own so the caller's wins. Re-cal knobs:
`SOH3D_TINT_DIFF` (diffuse fraction, default 0.5), `SOH3D_TINT_MUL` (overall, 1.0).

**Verified QUANTITATIVELY** (`tools/compare_render.py model`, A/B same scene/spawn;
see [[verify-quantitatively]]). Model-region mean luminance:
- Gossip Stone / Kakariko: full-bright **150.5** -> tinted **62.7**; N64 **60.2**
  (tinted within ~4% of N64; full-bright was 2.5x too bright).
- Pot / Deku Tree (darker scene, single material): tinted **56.6**; N64 **65.4**
  (~13%) — confirms it generalises across scenes/objects, neither black nor
  full-bright.
The frac=0.5 / mul=1.0 DEFAULTS land within tolerance with no per-scene tuning, so
no magic constants are baked. Residual hue gap (OoT3D stone is gray-green, N64 is
gray-blue) is the genuine OoT3D texture palette, not a tint error.

## Next phase (implementation)
1. **DONE** — First in-game OoT3D pot via approach A (see MILESTONE above).
2. **DONE** — Real `ObjTsubo_Draw` path renders the OoT3D pot at calibrated size
   (0.12), via `SoH3D_DrawModel`'s own matrix. Root cause of the earlier
   non-render documented above.
3. **DONE (lighting)** — Pot renders unlit / full-bright so the OoT3D texture
   shows at its authored brightness. Root cause of the earlier darkness: SETUPDL_25
   has `G_LIGHTING` on, so F3DEX2 read the white vertex *color* as a *normal* and
   shaded by the (dark) scene. The converter now clears `G_LIGHTING | G_FOG` and
   uses white vertex color (unlit). `--lit` emits real CMB normals for N64
   per-vertex lighting instead, but on this low-poly pot in low ambient that
   bands hard, so unlit is the default. True 3DS-style per-pixel lighting is a
   later, bigger task.
4. **DONE (flat scene tint)** — Unlit models now darken/colour-shift with the room
   via a flat per-draw PRIMITIVE tint (no per-vertex banding). See the MILESTONE
   below. (Materials→textures map was already done in session 3's multi-material
   work; the converter no longer hardcodes texture 0.)
5. **Azahar oracle instrumentation**: headless frame dump (glReadPixels, mirror
   of the LUS one) + draw-call dump of geometry/material/texture for A/B compare.
6. **Generalize**: drive the converter from a per-actor table / GameInteractor
   `VB_*` hooks instead of editing each actor's Draw; then a second object.

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
  list + full-res RGBA32 texture, feed to `Gfx_DrawDListOpa`. **This is what's
  implemented and verified (see MILESTONE).** The old "TMEM 4 KB limit" worry was
  wrong — LUS uploads textures at full tile size (CORRECTION above). Remaining A
  limits are s16 vertex precision and N64-style lighting, not texture size.
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
