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

### MILESTONE — generalised table-driven divert (2026-06-15, session 4)
The SoH3D divert was hand-coded into each actor's Draw (`ObjTsubo_Draw`,
`EnGs_Draw`) with an `if (SoH3D_Enabled()) { SoH3D_DrawModel(...); return; }`
block. Replaced with ONE central divert + a table, so adding an object is a
one-row change with no actor-source edits:
- `soh3d.c`: `sModelTable[]` maps `actorId -> { dlist, worldScale }`, and
  `SoH3D_TryDrawActor(play, actor)` looks the actor up; on a hit it draws the
  OoT3D model via `SoH3D_DrawModel` and returns 1.
- `z_actor.c` `Actor_Draw`: the single `actor->draw(actor, play)` call site (the
  chokepoint for EVERY actor) becomes
  `if (!SoH3D_TryDrawActor(play, actor)) actor->draw(actor, play);`.
- The per-actor edits in `z_obj_tsubo.c` / `z_en_gs.c` are reverted to stock.

`Actor_Draw` only runs once an actor has a non-NULL draw, so load/spawn timing
(e.g. the pot's VB_POT_SETUP_DRAW gate) is preserved. **Verified**: the Gossip
Stone rendered through the new central divert is **pixel-identical** (0 differing
px) to the pre-refactor per-actor render.

To register another object: extract+convert its CMB (see the pot/GS recipes),
add `{ ACTOR_<ID>, <model>_dl, <scale> }` to `sModelTable[]`, add the generated
`#include`/`extern` via `soh3d.h`. (GameInteractor `VB_*` hooks were considered
but the `Actor_Draw` chokepoint is simpler and id-driven — no per-actor hook
plumbing.)

### TOOLING — live REPL for a long-lived headless SoH instance (2026-06-15, session 4)
Replaced the env-flag -> rebuild -> 7-min headless render loop with an interactive
REPL so experiments cost seconds. Tooling-first (a hard rule — see memory):
- `tools/soh3d_render.sh` — headless launcher with GUARANTEED Xvfb/soh teardown via
  a trap (soh.elf exits 139 on teardown, which leaked Xvfb under plain xvfb-run —
  that was the "instances left behind" bug).
- `tools/soh3d_repl_launch.sh` — boots ONE long-lived instance with the REPL FIFO
  enabled (default warp Gerudo Valley 0x117, which loads OBJECT_KIBAKO2).
- `soh3d.c SoH3D_ReplPoll` (env SOH3D_REPL=<fifo>) — reads commands each frame:
  `mul/diff/tint` (live scene tint), `scale <name> <f>`, `spawn <name>`, `enable`,
  `dump <path>` (on-demand frame dump, no exit), `state`. Tint params + per-model
  world scales are now live globals; the model table carries a name per entry.
- libultraship `gfx_sdl2.cpp` — on-demand dump trigger (`gSoh3dDumpPending`/Path)
  so the running instance dumps any frame without exiting.
- `tools/soh3d_repl.py` — driver: `ready`, `cmd`, `shot [box]`, `zoom`, `region`,
  `isolate` (diff two shots of the same scene to isolate the one changed object),
  `probe`. Use these; never hand-run xvfb or inline measurement python again.

### OPEN BUG — 128x128 RGBA32 textures don't upload (gsDPLoadBlock 12-bit lrs)
The OoT3D crate (Obj_Kibako2, 128x128 ETC1 wood) renders a teal/green blotchy
texture, NOT its (verified-brown) wood texels. Diagnosed via the REPL:
- `state` confirms at mul=10 the tint PRIM clamps to (255,255,255), so a full-bright
  shot is pure texture — and it is green, while every tex0 texel is brown.
- A solid-MAGENTA texture (cmb_to_c.py new `--solid R,G,B` diag flag) ALSO renders
  teal, so our texture array is never sampled — the crate shows a stale/other texture.
- Root-cause candidate: `gsDPLoadBlock` encodes lrs (texels-1) in a 12-bit field
  (handler reads `C1(12,12)`), so >4096 texels truncate (16383 & 0xFFF = 4095) ->
  wrong size_bytes -> garbled/partial load. The pot (8192 texels) is ALSO affected
  (loads ~64x64 not 64x128) but wasn't visually obvious; the GS's verified face is
  its 128x64 body, not a true 128x128. So 128x128 was never actually proven before.
- FIX ATTEMPTED: emit `gsDPLoadBlockWide` (full lrs in w1) for >4096 texels
  (cmb_to_c.py). Did NOT fix the crate — still teal. Kept (it IS needed).
- NARROWED with REPL + interpreter.cpp logging (logging since reverted):
  - `GfxDpLoadBlock` IS called for the crate every frame with `lrs=16383 siz=2`
    (RGBA32), so the wide opcode dispatches fine and size_bytes=65536 is computed.
  - BUT `Interpreter::ImportTexture` / `ImportTextureRgba32` is NEVER called for a
    >=16KB texture, and never for the crate's first texel (213,196,94). The only
    RGBA32 import seen is a 32x32/4096B UI texture. So the 128x128 load happens but
    the texture is never IMPORTED/uploaded at draw time -> the crate samples stale
    TMEM (the teal). The pot (64x128) renders, so smaller raw RGBA32 imports work.
  - NEXT: trace why ImportTexture is skipped for the render tile of a 65536B load.
    Suspects: TextureCacheLookup returning a stale hit (key collision on
    {origAddr,fmt,siz,origSizeBytes}); the render tile's tmem_index not pointing at
    the slot LoadBlock wrote; `textures_changed[i]` not set; or a size cap in the
    draw-time texture binding. Put the log back at ImportTexture top filtered on the
    crate's first texel (213,196,94) and at the TextureCacheLookup call.
  - NOTE: headless auto-warp boots are FLAKY (often hang in the entrance fade with
    Play_Draw taking the transition `goto`, which is BEFORE SoH3D_ReplPoll, so the
    REPL goes unresponsive). Retry the launch; or move ReplPoll earlier in Play_Draw
    / into Play_Main so it runs during transitions.

### TOOLING — Azahar texture-decode ORACLE (data-driven) (2026-06-15, session 4)
Built the first piece of the "compare SoH3D vs Azahar" oracle the user asked for,
as a C++ tool in the Azahar fork that needs NO emulator run / in-game navigation:
- `Azahar/src/soh3d_oracle/` (new CMake target `soh3d_oracle`): links Azahar's OWN
  `Pica::Texture::LookupTexture` + `etc1.cpp` + `citra_common` (no GL/Vulkan/core).
  Decodes a raw PICA texture blob -> PPM = the emulator's ground-truth decode.
  Build: `cmake --build Azahar/build --target soh3d_oracle`.
- `tools/oracle_compare.py <cmb> [tex]`: pulls the CMB texture's raw bytes, runs
  BOTH the oracle and the converter's `pica_texture.decode`, and diffs per channel
  (worst/mean |Δ| + histogram), trying both V orientations.

**FOUND + FIXED a real ETC1 decode bug, data-driven (the full oracle loop).**
The oracle showed the converter's ETC1 decode was NOT bit-exact vs Azahar — crate
tex0: ~90.7% channels exact, ~9.3% off by 33-128 (worst 66). The spatial diff
(`oracle_compare.py` histogram by `(x%8,y%8)`) localised the errors to EXACTLY
ETC1 subblock2 (the `c2` half: differ when local x>=2 OR y>=2, exact in the
lx<2&ly<2 corner). That + worst=66 (= 8 five-bit levels × ~8.25 after 5->8
expansion) pinned it to the differential DELTA. Root cause: `pica_texture.py`
`_s3` (3-bit sign-extend) used the C idiom `(n<<29)>>29`, which relies on 32-bit
overflow into the sign bit — but Python ints are arbitrary precision, so it does
NOT sign-extend (`(4<<29)>>29 == 4`). Differential deltas with bit 2 set (n=4..7,
i.e. -4..-1) stayed positive, corrupting every differential-mode block's c2.
Fix: `_s3(n) = n-8 if (n&4) else n`. After the fix ALL OoT3D ETC1 textures
(pot, GS tex0/tex1, crate) decode **bit-exact** vs Azahar: worst|Δ|=0, mean|Δ|=0.

Impact: every OoT3D texture rendered in-game before this was subtly mis-coloured
(~9% of texels, up to 66/255). Regenerate the generated models to pick up the fix.
This is the payoff of the Azahar oracle — a bug invisible to eyeballing, caught and
fixed to provable exactness. (The separate crate 128x128 LUS UPLOAD bug above is
unrelated and still open — correct texels still won't upload until that's fixed.)

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
6. **DONE (generalised divert)** — A central table-driven divert replaces the
   per-actor Draw edits. See the MILESTONE below.

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
