# Project state

## Comparison baseline

The baseline is the unmodified Nintendo 3DS releases of *Ocarina of Time 3D* and *Majora's Mask 3D*
running on original hardware or through Azahar. Zelda3D's intended difference is one lawful native-PC
experience that consumes the player's own remake assets while reproducing each remake's presentation
and game-specific behavior outside a 3DS emulator.

## Current focus

S003 is the current focus.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | One launcher provisions, validates, builds, and chooses between the OoT and MM game cores | verified | — | G003 |
| S002 | 3DS containers, models, animations, scenes, collision, cameras, lighting, and face data are available to both engines | partial | S001 | G001, G002 |
| S003 | The PC renderer reproduces the reached PICA200 material, texture, lighting, fog, and transparency semantics | partial (texture formats **and** sampler filtering/wrapping: **verified for both games**; **OoT's PICA distance fog: measured and applied on both host routes**, MM's is missing per S005; material/fragment lighting still partial) | S002 | G001, G002 |
| S004 | OoT3D actor animation, facial, camera, and game-specific behavior replaces N64 behavior where grounded | partial | S002, S003 | G001 |
| S005 | MM3D actor animation, presentation, and game-specific behavior replaces N64 behavior where grounded | partial (**scene-authored fog: missing** — `Zelda3D_Fog3dSet` is never called from `2ship/`, and MM3D's ZSI does not carry OoT3D's env region, so the data is still to be recovered) | S002, S003 | G002 |
| S006 | An embedded Azahar oracle and parity tooling can compare the port with independent 3DS execution | partial | S001 | G001, G002 |
| S007 | The AppImage accepts four direct ROMs or bounded ZIPs and persists validated choices without shipping game content | partial | S001 | G003 |
| S008 | Linux CI builds the complete app and both cores and executes asset-free native contracts | partial | — | G003 |
| S009 | macOS arm64 CI exercises portable policy seams; complete app delivery remains open | partial | — | G003 |
| S010 | Windows x86_64 CI exercises portable policy seams; complete app delivery remains open | partial | — | G003 |
| S011 | Android has a native package target and an asset-free CI gate | missing | — | G003 |

## Capability details

### S001 — Unified launcher

Evidence: `./run.sh` resolves the locked environment and public dependencies, identifies all four ROMs,
builds both game cores, and opens the unified chooser without requiring private decomp tooling.

### S002 — Shared 3DS asset semantics

CMB, CSAB, ZAR, ZSI, CMAB, and `.faceb` readers feed 3DS meshes, animation, scene, collision, camera,
lighting, and facial data into the two native PC engines with N64 fallback.

Gap: format and content coverage remains incomplete across both retail games.

### S003 — 3DS material and renderer coverage

Reached materials preserve multi-stage combiners, multiple texture coordinates and samplers, vertex and
fragment lighting, alpha behavior, and scene-authored fog through the native renderer.

**Scene-authored fog is now measured and applied on both host routes**, so it is no longer part of
this item's gap. The PICA distance-fog mechanism (window, colour, 128-entry LUT, per-fragment depth)
was recovered and correct on the native route, and the **unified route — the one the title comparison
runs on — applied none of it**: its UBO declared the fog parameters as "size-parity padding", and the
packer copied them without the per-draw gate, so every unified draw was unfogged. Closing it moved
the title host-vs-oracle `union_rgb_mae` from **48.14 to 38.23** at the same cursor and raster, which
is the same -9.9 the native route shows for the same fog (-9.63) — two independent code paths
measuring the same quantity. The N64 F3DEX ramp is a *different* mode of the same enum, not a
replacement: mode `1` still reads the `aFog` varying, mode `2` reads the PICA LUT, and one number
chooses between them on either route.

Gap: the renderer campaign and current codemap still identify wider material, fragment-lighting, actor,
and effect families whose parity is partial. **Both halves of the title-scoped host-vs-oracle
comparison now work, and the first host title image against a cached oracle frame exists**
(`tools/title_host_capture.py 1093` → `cs=1093` / `az=2016`, `content=0.4784`, `union_rgb_mae=47.80`;
`scratch/title_host_capture/title_cs1093_sxs.png`). Getting there took two fixes rather than a
capability change: the wordmark's unified pipeline faulted on a **sampler count that disagreed with
its own shader** — `kDualTex`/`kDualTexFog` were declared as binding one sampler while their generated
GLSL binds two, which SDL3 turns into a too-small bind-group layout — and the host half was capturing
at a shape the oracle never had, first because the harness wrote its `shipofharkinian.json` to a cwd
that SoH no longer reads (configuration resolves to the OS user-data dir, which also meant every run
rewrote the player's real config), and then because the tool hardcoded a 400x240 raster the cache had
stopped matching. Both are recorded and closed in
[issue #25](issues/0025-title-wordmark-unified-pipeline-crash.md); the sampler count is now derived from
the same feature table the GLSL comes from and locked against the generated source.

What that first comparison establishes and does not: the wordmark composition — lockup, sub-lockup,
shield, sword, credit line — is present at the oracle's scale and placement, so the 3DS-authored
content is composed correctly (lockup width ~445px host vs ~415px oracle of 800). The glow hue was
white where the oracle's is orange. It was first blamed on the unified path's placeholder combiner,
but a generated-source test refutes that — `kDualTex`/`kDualTexFog` never call `evalCycle`, they take
a branch implementing the three byte-classified PICA dual-texture shapes on `uSheen.y`, so the
wordmark is on the same proven shape mechanism as the native path. It is now **attributed and fixed**:
the unified path gated the per-draw RGB modulation on `lit`, which `ZELDA3D_HANDLE_FORCE_UNLIT` clears
without touching the modulation, so the force-unlit fire-glow drew untinted and additively saturated
to white — and scene geometry lost its scene tint the same way. The PICA distance fog the unified
path documents as not applied, and the dawn-layer stack, remain candidates for the *residual* and
neither is claimed. The tool's `content` metric moved
0.4784 → 0.4846 between two invocations that looked identical; that was **not** run-to-run noise but a
one-frame shift, since `--draw-list` armed and stepped before capturing. Measured, with the host
state read at cs=1093 in two separate processes:

| Comparison | mean-abs | pixels differing >16 |
|---|---|---|
| two identical runs | 0.73 | 1.9% |
| `--draw-list` vs plain (one extra frame) | **5.90** | **13.9%** (max 229) |
| oracle vs host | ~47.6 | — |

So the host title advance *is* reproducible — daytime, skybox, blend, ambient, fog, camera eye/at/
dir/up/fov and the rider's position and all three yaws are bit-identical across runs from the same
boot cursor (120) and the same 1945-frame budget — the instrument's noise floor is ~0.7 mean-abs, and
**one frame is worth ~5.9**. `capture_cursor_image` now takes the comparison image before publishing
the draw list, and a test locks that order (verified to fail when inverted). Read the metric as
resolving whole-frame differences only, never sub-frame ones.
The framing does not match — the host is close to the rider and the oracle's is wide — but that is
**not** established as a camera divergence: `title.rider-trajectory` already documents a cs-rate
desync (host 10/s against the oracle's 30/s, user-owned card #149) with this exact symptom, verified
benign at cs=1407. A five-cursor host sweep (cs 1069…1117) shows the mismatch exceeds a frame-domain
residue while being exactly the size a rate desync would be, so the discriminating step is a
cs-frame-locked A/B at cs=1093, not a camera change. Until it is,
`cmb-unlit-primary`, `cmb-lit-primary-alpha` and `cmb-fragment-lighting` stay open: they now have a
working comparison, not a passing one. Coordinator mapping methods are now recovered from the retail
shader
(`oot3d-decomp/docs/cmb_texcoord_mapping.md`, frontier `render.cmb-texcoord-mapping`): the shader's
entry point calls one of two mutually exclusive bodies, and only one of them contains the
texture-coordinator mapping switch. A real oracle capture closed the obvious follow-up by refuting
it — `ShaderMode.w` is not a synonym for "lit", so the host correctly needs no new transport and
must not gain a lit-ness gate; Majora's Mask's asset corpus refutes it a second time, since its
mapped materials are fragment-lit where OoT3D's are vertex-lit. ProjectionMap (method 4) is the one
unimplemented mapping, and both games' corpora put it on coordinator 1 only — where the shader emits
no `w`, so the question that seemed to need a PICA register measurement is not on the critical path
at all. The shader's uniform inputs are now readable offline too: `tools/pica_shader_uniforms.py`
replays the whole vertex float-uniform array out of a cached command list, so `uInvView` (identity on
every title draw) and the missing `TexMtx` third row (`(0,0,1,0)`) are measured rather than assumed.
**The two games are not equally placed, and the measurement says which side to work.**
`tools/cmb_fragment_lighting_survey.py --game oot|mm` measures both retail corpora with identical
definitions: **OoT3D has 197 of 11,172 materials (1.8%) consuming an enabled PICA `FRAGMENT_PRIMARY`;
MM3D has 5,993 of 6,791 (88.2%)**, plus 691 vs 69 for `FRAGMENT_SECONDARY`. The host maps those to the
vertex-lit primary and to black, so this is a rounding error on OoT3D and the **dominant lighting
mechanism of MM3D being essentially unimplemented** — which contradicts any reading of S003/S005 as
symmetric. It is also cheap to fix and rankable: **MM3D's single most common chain covers 2,978
materials (44% of its corpus) and the top five cover 3,692 (54%)**. The OoT3D figures reconcile exactly
with the existing row (202 consumers, `source_without_flag=5`, 202−5=197).

**The first MM3D number was an undercount caused by the iterator, not a property of the game, and
every "MM3D's corpus" figure recorded before this is understated the same way.** `cmb_corpus` claimed
MM3D "ships no `.zsi`-equivalent in `/actors/`" and concluded its population was actor archives only.
The qualifier was true and the conclusion was not: MM3D ships **424** `.zsi` files under `/scenes/`,
241 carrying a valid inline CMB (183 do not, exactly as 114 of OoT3D's 724 do not, so the extraction is
consistent rather than lossy). `iter_mm3d_cmbs` now also walks `/scenes/*.gar` and the inline scene
CMBs, both games share one `_iter_zsi_inline_cmbs`, and the populations are the same shape
(OoT3D 1,387 archive + 610 scene; MM3D 1,463 archive + 241 scene). That raised MM3D from
2,283/2,968 (76.9%) to 5,993/6,791 (88.2%). The surveys print their `corpus:` composition on every
run and derive the note from what was walked, never asserting it per game — a hardcoded per-game note
is precisely what went stale.

Whether the OoT3D-recovered `+0` flag and `+0xA0..+0xB3` colour block are even valid for MM3D was
tested with a control against each argument, and they disagree: the "authored colours are spiky"
argument is **refuted by its own control** (the material record is mostly constant, so other offsets
hold a median of only 4–8 distinct values, and MM3D's `specular0` is more varied than that), while the
descriptor probe-word argument is **validated by its control** — the u32 `0x62C884C0` at material
`+0xDC` is the most common word at exactly one of 84 sampled offsets in both games (a composite of two
typed fields, not a named enum). So the `+0xCC` descriptor layout is strongly corroborated and the
colour block plausible by adjacency, but neither is the binary read, and confirming them in
`mm3d-decomp` is the named next RE step. Separately, the PICA lighting **configuration builder** (`FUN_0040cdd8`) is now executed rather than described: it is transcribed to `oot3d-decomp/tools/pica_lighting_config.py` and reproduces all three oracle-observed registers for the grounded fixture from decompiled source (`config0=0x80000400`, `config1=0xff7fffff`, `light_enable=0x00000010`) under 19 mutation-verified tests. So the fragment-lighting *formula* and its *mode selection* are both portable; the one remaining unknown is the **producer** of the builder's input object — which CMB material fields populate the mode bytes, the eight slot-enable bytes and the three flag planes. That is a transport question, and it is what gates implementing `FRAG_PRIMARY`. The obvious candidate for the feed — that the builder's input object is the CMB nested descriptor at material `+0xCC` at zero offset — is now **measured and refuted** over every fragment-lighting-flagged material in both corpora (205 OoT3D, 6,428 MM3D): the descriptor's eight slot-enable bytes are zero in 205/205 and 6,428/6,428 cases, which would mean no material ever has a lit slot, and its `+0x20..+0x2D` region holds float bit patterns rather than mode bytes. So the input is a *runtime* lighting object — and that object's **feed is now mapped to one byte**. `FUN_004c6264` (252 B) is its constructor (zeroes four 8-byte slot planes and the mode block, sets only `+0x18A` and `+0x18D` to 1), `FUN_003fa5d0`/`FUN_003fa34c` set the eight slot-enable bytes, and `FUN_004c6364` (224 B) is the descriptor feed writing `+0x189`/`+0x18B`/`+0x191`/`+0x192`/`+0x193`/`+0x195` from descriptor fields **the shipping parser already retains**, so the feed needs no new asset data. Two consequences: the constructor's defaults alone reproduce the fixture's `config1=0xff7fffff` by a second independent path, and `config0` differs from the recorded `0x80000400` by **exactly bit `0x11`**, i.e. `param_1[0x18A]`. No function on the recovered chain writes that byte, and reading `FUN_00371758` shows it is a **pure 32-byte block copy** with no field logic, so it cannot clear that byte selectively — the open question is the **provenance of its 0x4C8-byte source**, and arithmetic rules the CMB out (the material stride leaves only `0x90` bytes in OoT3D / `0xA0` in MM3D after the nested descriptor at `+0xCC`, and the parser's descriptor is `0x2C` bytes). So the source is a separate toolchain-authored object in game data: the host can compute the builder and feed six of its fourteen mode inputs, but not the slot enables or the remaining mode bytes, which is why this is still a transport gap. The one-byte discrepancy is pinned by a standing red-to-green test rather than a sentence. **The obvious next search is now a dead end, with numbers:** looking for the template by signature in `code.bin` (4.36 MiB, 18.5% zeros) gives 27 aligned hits across the 1/2/3/4-light shapes and **not one repeated delta between them** — no table, no stride — against baselines of 473 enable-plane-shaped 8-byte windows and 484 all-zero 61-byte windows. So the template is not in the code image under that layout, which fits a data-container object; the next search must be the game's data archives or a runtime capture of the copy's source pointer. The tool prints both verdicts on every run so the
disagreement stays visible. Getting to MM3D at all also required fixing MM3D's silent-zero class in the
tool itself (the material chunk pointer at `0x28` is `qtrs` for version ≥ 7, so the entire MM3D survey
had been returning nothing), now routed through the single layout owner and pinned by a test verified
to fail on the old read.

**A real, user-visible texture bug, found by measuring the formats both games actually use.**
`tools/pica_texture_format_survey.py` walks every texture in both containers the host reads (`cmb`
model textures and `ctxb` banks) for **both** games and checks each distinct `(data_type<<16)|fmt`
against the shipping decoder. It found `0x67606758` -- 3dstool's **LA4**, 4-bit luminance + 4-bit alpha
in one byte per pixel -- **declared in `pica_texture.cpp`'s format table but never switched on**, so
`PicaDecode` returned empty and the texture was dropped. Two OoT3D textures use it:
`magic_fire/model/acto_magic_fire.cmb` (64x64, data_len 4096) and
`magic_love/model/m_shield_2_modelT.cmb` (32x64, data_len 2048) -- the Great Deku Tree's magic-fire and
magic-love **effects**, so this was visible gameplay content decoding to nothing. The Python mirror
`tools/pica_texture.py` listed the same format with no decoder either, which is why no earlier
measurement caught it: both sides *looked* like they supported LA4.

The depth is 8 bits per pixel (4 per **channel**) -- the byte counts are per-pixel, and reading "4 bits
per pixel" off the name looks for half the buffer. Nibble order is not a guess: it is Azahar's own
`Common::DecodeIA4` (`src/common/color.h`), high nibble luminance / low nibble alpha, the decoder the
oracle runs. Fixing it also exposed a **memory-safety hole**: only `GF_RGBA8` and `GF_RGB8` ever
length-checked their buffer, so every other format read `d[i*2]` with no guard and a truncated payload
was an out-of-bounds read. There is now one guard derived from a `PicaBitsPerPixel` table, covered by
`PicaTextureDecode.*` (7 tests, mutation-verified: removing the LA4 case fails 4 of them, swapping the
nibbles fails the order test, a wrong depth fails the per-pixel test) and
`test_pica_texture_format_survey.py` (10 tests). The survey now reports **every format in both games'
content handled** -- OoT3D 10,538 cmb + 1,650 ctxb textures across 14 distinct formats, MM3D 6,968 + 335
across 8 -- and that the C++ and Python tables agree, read out of the shipping source rather than
transcribed.

**Sampler filtering and wrapping, measured the same way, and also complete.** These are named scope
beside formats and had no evidence either, so `tools/pica_sampler_state_survey.py` walks every bound
texture unit of every material in both games and reports what the content actually asks for. It covers
**all three units**, not just unit 0, because multi-stage TEV binds units 1 and 2 and Zora's water is
the three-stage case. OoT3D uses 5 distinct minification enums, 2 magnification, 3 wrap modes; MM3D the
same. Every one is a named GL enum and every one the host resolves -- and the mechanism is present end
to end, not merely table-covered: the sampler gets `maxLod` 0.0 when the material requests no mips and
1000.0 when it does, so the 8,123 of OoT3D's 12,888 bindings (63%) that ask for mipmapped minification
get real mip selection over chains uploaded with `ci.num_levels`. The C++ side is pinned as a property
over the enum *space* rather than a list of the values found, so a sixth minification enum cannot
silently fall through a default. 20 new tests (5 C++, 15 Python), all mutation-verified.

**The title fire-glow's colour, root-caused and fixed.** The wordmark glow was recorded as
"unattributed — white where the oracle's is orange". It is now attributed, and the cause was a
**gate on the wrong variable**. The unified render path applied a CMB draw's per-draw RGB modulation
only when `lit` was set. `lit` means *"apply the character/prop lighting term"*, which is a different
question from *"apply this draw's RGB modulation"*, and `ZELDA3D_HANDLE_FORCE_UNLIT` clears the
first while leaving the second intact. The authoritative native path applies the same modulation
**unconditionally** (`vec3 shade = ubo.uTintSkin.xyz`), so the unified path was the divergent one.

Two things were wrong because of it, and both are user-visible:

* **The fire-glow lost its tint.** The glow is additively blended, so an untinted white texture drove
  all three channels to saturation. Measured over the brightest tenth of the halo: host
  **1 : 1.00 : 0.99** with 3809 saturated pixels, oracle **1 : 0.93 : 0.58** with 2571. Absolute
  brightness cannot see this — a saturated white and a saturated orange have the same luminance —
  which is why it survived every brightness metric recorded against this frame.
* **Every scene-geometry draw lost its scene tint.** `Zelda3D_SceneTint` hands geometry a real
  ambient-tinted value from the environment palette, those draws are `lit=0` too, so the whole world
  was rendering at full brightness regardless of the scene's light. That is a large part of why the
  host title reads hazier and brighter than the oracle's.

The gate is now the two conditions that actually mean something — `alreadyTransformed` (an N64 draw
uses `uPrimColor` as a combiner *source*) and `lightingMode 2` (a vertex-lit PRIMARY comes from the
PICA light bank) — and `PackCmbDrawModulation` lost its `tintEnabled` parameter rather than keep a
switch that encoded the wrong policy. After the fix: host **1 : 0.95 : 0.72**, 1837 saturated pixels
(green matches the oracle's 0.93 almost exactly). The residual blue ratio, 0.72 against 0.58, is
**not** closed and is not claimed; the tool now prints `glow_b/r` and `glow_sat_px` so the remaining
step is a number rather than an impression. `glow_metrics` is a first-class metric for exactly this
reason and is covered by `tools/test_title_glow_metrics.py` (8 cases).

The attribution came from the project's own instrument, not from reading the shader: `log fireglow`
reports the sampled `g_title_fire.cmab` ConstColor at the exact cursor, and at cs=1093 it reads
`rgb=(0.8000,0.4300,0.0000)` — correct orange — which placed the fault strictly downstream of the
draw call. The whole-image `union_rgb_mae` moved 47.80 → 48.14 and is **not** evidence either way:
that frame's dominant delta is the title-demo camera, since the host runs the N64 demo and the
oracle the 3DS demo.

**The unified route applied no PICA distance fog at all, and the title comparison runs on the unified
route.** Its own UBO said so: `uFog3d0`/`uFog3d1` were declared as "size-parity padding" for a
renderer that "doesn't apply the 3DS fog yet". The packer already copied **both** of them, so the
frame-level parameters were arriving and nothing consumed them — the omission was the **per-draw
GATE**, not the data. `SgUbo::uFog[3]` is set to 2.0 only when `gZelda3dFog3dOn && grp.fogEnabled`,
i.e. the frame's 3DS fog is on *and* this material's CMB sets `isFogEnabled`; the additive/effect
materials opt out, so a copy-the-parameters-only route is unfogged everywhere without a single
diagnostic.

The fog was worth naming as a number before it was worth porting. The native and unified routes are
different code, so comparing them against each other confounds the fog with every other difference;
instead the latch moved at its single owner, `gZelda3dFog3dForceOff` inside `Zelda3D_Fog3dSet`, which
feeds the same per-draw gate on both routes. Measured at title cs=1093, one oracle frame, one raster:

| route | fog | `union_rgb_mae` | `content` | host gold px | host glow sat px |
|---|---|---|---|---|---|
| unified | off | 48.14 | 0.4596 | 7122 | 1837 |
| **unified** | **on** | **38.23** | **0.4811** | 13375 | 1923 |
| native | off | 53.19 | 0.5627 | 3904 | 7756 |
| native | on | 43.56 | 0.5735 | 11201 | 7799 |

The unified route's **-9.91** and the native route's **-9.63** are the same quantity measured on two
independent code paths, which is the cross-check that this is the fog and not a coincidence. Latching
the unified route's fog back off reproduces the pre-change 48.14 / 7122 / 1837 **exactly**, so the
entire diff is the fog. It also retires the frontier's "the PICA distance fog ... is the named
candidate, with the dawn-layer stack second. Neither claimed" — the fog is now measured, the dawn
layers are what's left.

The port carries the **native mechanism**, not a second implementation of it: the same `fog3dNode()`
LUT node (`eyeDist = b/(a-t)` then the linear `fogNear`..`fogFar` window), the same 128-entry
in-entry interpolation, the same per-fragment depth from the interpolated world position (`a - b/d`),
the same sky exclusion, and the **same mode enum** — `0` none, `1` the F3DEX ramp, `2` the PICA LUT —
so one number moves a draw between fog modes on either route. The gate moved into
`Zelda3DUnified::PackCmbFogGate` next to the other packers, and the UBO slot it occupies was
`uNativeLayoutPad`, a size-parity filler in exactly the place the same fog needed. The block is
compiled into **every** variant rather than a subset, because the gate is runtime: a per-variant
subset would be a second fog policy that silently drops the fog for whichever material lands on a
variant someone forgot. Six C++ tests, all mutation-verified (fog3d off for one variant, swapped mix
arguments, a zero `vWorld`, a 0/1 gate instead of mode 2, and a dropped packer copy each fail
something), plus three new Python cases on the fog latch — including the one that matters, which
refuses a capture labelled "fog off" when the host reports the fog still on, because a fog can be off
for an unrelated reason and then the A/B has silently measured nothing.

**Majora's Mask has no PICA distance fog at all, and the natural fix is refuted.** `Zelda3D_Fog3dSet` is
called only from the SoH layer (`title_lighting.cpp`, `lighting/zelda3d_lighting.c`) and never from
`2ship/`, so `gZelda3dFog3dOn` stays 0 for the whole game and every MM draw is unfogged on both
routes. The per-material half already works for both games — `CmbVShaderGroup::fogEnabled` comes from
the CMB `is_fog` byte, and `cmb_glgroups.cpp` parses it for whichever game the archive came from — so
what MM lacks is only the frame-level submission.

The tempting move was to reuse OoT3D's generator for MM3D's scenes, and it does not work. Walking
MM3D's **424** `/scenes/*_info.zsi` files with OoT3D's own `parse_env` (the 8-byte big-endian scene
command stream, `ctype 0x0F`, `[16-byte header][count x 28-byte records]`), **only 5 files have a
`0x0F` command at all**, and the records they do have read as noise — `zFar` values of `-2.49e10`,
`+1.69e19` and `123150` in the same table, `fogFar` of `1.69e-19`. So MM3D's ZSI does not carry
OoT3D's `EnvLightSettings` region at that layout, and a generated MM table from this parse would be
fabricated numbers. MM's fog and ambient data live somewhere this parser has not found, and
recovering them is a named `mm3d-decomp` step, not a table to transcribe. Until then MM's fog is
recorded as **missing**, not approximated from OoT3D's table.

**Majora's Mask is inside the same gate as Ocarina of Time — checked, because I assumed otherwise
for a while.** The root `CMakeLists.txt` is the only supported configure root and it adds BOTH
games into one tree (`add_subdirectory(${ZELDA3D_OOT_DIR} .../soh)` and
`add_subdirectory(${ZELDA3D_MM_DIR} .../mm)`), so `Shipwright/build-cmake` is not a SoH-only gate.
Two facts make a shared-renderer change safe for MM without a second build tree:

* `Shipwright/libultraship` is the **single** shared renderer owner, added once and linked by both
  games, so a change to it is built once and cannot leave MM holding a stale copy.
* `mm_lib` is a **static** archive of MM's own objects and links no copy of libultraship, so it is
  correctly up to date when only shared code changed.

The suite carries MM's own tests too (`mm_gfx_print_test`, `mm3d_player_animation_policy_test`) —
515 tests total. Configuring 2ship *standalone* (`cmake -S 2ship`) does not work on Fedora and its
CMakeLists explicitly does not support it ("the root CMakeLists already add_subdirectory'd these"),
so that failure is not a gate and is not worth fixing.

**Multi-stage combiners — the first item in this campaign's scope — measured for Majora's Mask for
the first time.** `tools/tev_corpus_survey.py` had only ever been run against OoT3D. MM3D over 1,704
files / 6,791 materials: **zero layout-domain violations**, stage counts 1 through 6 all present
(653 / 4,083 / 1,497 / 440 / 107 / 11), 123 materials latch the combiner buffer and 46 read
`PREVBUF` with **0 unsafe reads**. That last one matters: the evaluator's `vec4(0)` for the combiner
buffer is now measured exact for *both* corpora, so the earlier OoT3D-only justification generalises
instead of being an unverified assumption about MM.

A content shape the survey was counting but not naming: **materials whose combiner chain reads a
texture unit their own bindings never declare.** The survey printed a bare count, which is why it sat
unnoticed; it now names each one, and there are exactly three across both games — OoT3D
`/scene/hiral_demo_0_info.zsi mat0` (stage0 `rgb_src` reads an undeclared **tex0**) and MM3D
`zelda_gi_bigbomb.cmb mat1` + `bb_model_model.cmb mat1` (stage1 `rgb_src` read an undeclared
**tex2** under `MULT_ADD`). All three are **neutral by construction**: the host substitutes its dummy
texture, which is uploaded opaque white, and white is the identity element for `MODULATE` and for
`MULT_ADD`/`ADD_MULT`'s first factor, so each evaluates as if the unused slot were absent. That is a
property of those ops rather than of the dummy, so the named list is where a non-neutral op would
surface.

Getting that number right needed `slots_used(op)`: PICA ops consume 1, 2 or 3 sources, and a
`TEXTUREn` sitting in an *ignored* slot is not consumed at all. A naive slot scan reported **6** MM
materials; the survey's own rule reports **2**, and the naive scan was the wrong one. I also claimed
mid-investigation that the dummy texture was uninitialised GPU memory — it is not; it is uploaded
opaque white a few lines past the creation call, and I had stopped reading too early.

Fragment lighting remains the dependency, not the combiners: **89.13%** of MM3D materials (6,053 of
6,791) consume `FRAG_PRIMARY`/`FRAG_SECONDARY` as a combiner source, which is the unimplemented
`FRAG_PRIMARY` path recorded below.

**The fragment-lighting blocker moved: the search it was stuck on was looking in the wrong place.**
The recorded blocker for `FRAG_PRIMARY` turned on finding a 0x4C8-byte "toolchain-authored template" by
searching the code image, and that search had dead-ended at "27 hits, no stride". The reason is now
measured: **the template is not reached through an address table at all.** `FUN_00308498`, which is on
the confirmed chain, passes its `arg1` **straight through** to both the pre-pass and the builder as the
builder's object pointer; the only preparation is three byte stores at the object's offsets 0-2. That
pointer is then forwarded down at least three frames (`0x003f9f68`'s `mov r6, r1`, and its single
caller at `0x003f9d58` setting `r1 = r6`) without ever being computed from a table. An address-table
search over `code.bin` could not have found it, because there is nothing to find.

Getting there also exposed a measurement trap worth keeping: `disasm.py` establishes
`byte offset = vaddr - 0x00100000`, and computing an ARM `BL` target in *offset* space while searching
for a VA returns **zero callers for every function** -- including the builder, which is provably
called. That reads exactly like "nothing calls this", and it is how a whole recovered chain can look
refuted when it is not. I made that mistake first this session. `oot3d-decomp/tools/callers_bl.py` now
does the search exactly and **self-validates on every run** (a correct ARM scan sends 70.6% of its
targets back inside the code image; a wrong decoder is refused rather than reported), with 14 tests in
`oot3d-decomp/tests/test_callers_bl.py`. It **independently confirms the recorded chain from the
binary** -- `FUN_00308498` <- `0x003fa5a8`, `FUN_0040cdd8` <- `0x003084c4`, `FUN_0040d040` <-
`0x003084b4`, `FUN_004c6264` <- `0x004c3528`, `FUN_004c6364` <- `0x004c3644`, each with exactly one
caller.

**And one load-bearing claim in the blocker record does not survive: `FUN_00371758` has zero ARM `BL`
callers.** The record names it as "the delivery mechanism" for the 0x4C8-byte copy. Thumb and
indirect/function-pointer calls remain open -- my Thumb-1 `BL`/`BLX` decoder produced a 1:1 target
ratio with 2.2% in-image, which is garbage, so no Thumb negative is claimed -- but the ARM evidence is
zero callers, and that identification is now marked **unproven** rather than load-bearing. Full record
in `oot3d-decomp/docs/fragment_lighting.md`.

**The fragment-lighting blocker's central unknown is resolved: the configuration object was live
runtime state the whole time.** The blocker said the 0x4C8-byte "toolchain-authored template" could not
be found. It is not in the data because it is not in the data — it is a **per-material runtime struct
at `CmbRenderer + 0x400 + material_index * 0x4C8`**, readable from the oracle on demand. The address was
never guessed: the record already mapped CMB `+0x00` to `CmbRenderer + 0x400` and named the live
renderer at `0x081d3aa0`; `tools/lit_object_dump.py` dumps four consecutive slots at the **title
screen** (no gameplay save needed — the title demo renders the same fragment path) with the harness's
bulk `dumprange`.

Consecutive slots differ in nonzero density (24.2% / 15.3% / 14.5% / 29.4%) and in their leading
floats — slot 2 holds a `0.7` / `0.3` material blend — so this is genuinely per-material authored data.
**`+0x18A` reads `0x80` on slot 0**: authored, non-zero, per-material. The old framing "no function on the
chain writes `+0x18A`" was never the obstacle; the byte is *source data*, exactly as the record's
"provenance question about the source bytes" argued, and the source can now be re-read whenever needed.

**The recovered builder is now validated end to end against live bytes**, which it never was before.
Feeding the dumps through `pica_lighting_config.py`, material slot 1 returns **exactly** the
`config0`/`config1` pair the oracle's own registers were recorded at (`0x80000400` / `0xff7fffff`). Its
`light_enable` of 0 against the fixture's `0x00000010` is **not** a discrepancy: the recorded fixture
is a one-light Gravekeeper's Hut material and slot 1 is an unlit title material. That reproduction is
now a standing test written against an all-zero object, so it needs neither captured data nor a ROM.

Two more of this project's recurring traps bit and are now recorded. The chain's top,
`FUN_003f9b5c`, has **zero ARM callers** — as does the `FUN_00371758` the record called the "delivery
mechanism" — so nothing in the ARM code image constructs this object; it arrives from outside as an
argument. And a `capstone` linear sweep **stops at the first literal pool**, which is how a function
prologue search came back empty: the start address had to be found by matching the ARM
`PUSH {...lr}` byte pattern instead.

**Still open, and now narrow:** the `+0x18A` byte's contribution to `config0` bit `0x11`, and the
per-slot enable bytes for a *lit* material. The live cases to resolve are slot 0
(`light_enable=0x76`, five slots occupied) and slot 3 (`0x7632`) of the title dump. Fragment lighting
remains the dominant renderer dependency for both games — **89.13%** of MM3D materials — so this is the
right next thread, but it is no longer blocked on finding a file.

**The title demo never enables PICA fragment lighting — measured, with a denominator.** With the
configuration object located, the next step was a ground-truth triple for a *lit* material. The oracle
exposes exactly that: `vsuni_log <path>` for per-draw discovery and `lighting_capture <draw> <path>` for
one draw's raw `config0`/`config1`, light-slot map and activated LUTs. `tools/lit_pica_capture.py`
drives both directly, bypassing `tools/cmb_fragment_lighting_oracle_probe.py` — which starts from the
absent `GAMEPLAY_STATE` and so cannot run at all.

Over **207 draws at two points in the title demo: `picaLit=1` on ZERO of them, with 159 vertex-lit**,
and 34 of one sample's draws untextured. `picaLit` is the authoritative `regs.lighting.disable` register,
not the independent CmbVShader boolean, so this is real state and not a logging artefact.

That is the explanation for this project's whole run of fragment-lighting negatives: the committed
probe's own `kokiri-save-overlay` fixture is labelled a PICA-disabled negative control, and so is
everything derived from it. The capture path is not broken — **the reachable scenes are vertex-lit.**
The two open questions (`+0x18A` → `config0` bit `0x11`, and per-slot enables for a lit material)
therefore need a *gameplay* scene and inherit
[issue #23](issues/0023-embedded-oot3d-oracle-cannot-reach-its-boot-hand.md). The object-location result
itself is not blocked and stands on its own. Recorded so no further title-side effort is spent here.

One more false-negative trap, now pinned: the `vsuni_log` draw id lives in **`n=`**, not `draw=`.
Matching the wrong token parses a log full of draws as *empty*, and the tool then reports "no draw has
fragment lighting enabled" — indistinguishable from the finding above. It was the wrong token first;
`tools/test_lit_pica_capture.py` (8 cases, mutation-verified) locks it, along with the rule that
`picaLit` must never be read as `vLit`/`fLit`.

**Vertex lighting measured for both games — and it is the mirror image of fragment lighting.** The
scope bullet is "vertex and fragment lighting", and until now only the fragment half had been
investigated. Measured over every material:

| | OoT3D | MM3D |
|---|---|---|
| **vertex**-lit (`material +0x01`) | **9,931 / 11,172 (88.9%)** | 122 / 6,791 (1.8%) |
| **fragment**-lit (`+0x00`) | 205 (1.8%) | **6,428 (94.7%)** |
| neither | 1,036 (9.3%) | 245 (3.6%) |
| both flags set | 0 | 4 |

This reframes the campaign's two biggest lighting items. `FRAG_PRIMARY` is not a long-tail gap in
Majora's Mask — it is **94.7% of its materials** — while for Ocarina of Time the dominant path is the
*vertex* one, at 88.9%. Corroborated independently and live: 159 of 207 draws across two `vsuni_log`
samples at the title carry `vLit=1`, which matches OoT3D's corpus share.

**The two render paths agree on the vertex-lit term**, verified by reading both rather than assumed.
Native gates the lit branch on `uAmbient.w > 0` and unified on `uParams0.y > 1.5`, and both are the
*same* predicate — `grp.vertexLighting && gZelda3dWorldLit && !forceUnlit`. The lit formula, the
diffuse-alpha rule (accumulated once per enabled light, no NdotL), the HasColor gate and the clamp
order (`min(abs(primary), 1.0)`, PICA clamping `o1` on register write) are identical. This is a
**verified agreement, not a claim of oracle parity**: no per-draw comparison of the lit term exists,
because every reachable oracle scene is vertex-lit while the only host frame with a paired oracle image
is dominated by the title-demo camera difference. Closing that needs a per-draw `PIXEL`/`vsuni_log`
comparison on a frame whose camera already matches.

Two facts worth having before anyone attributes a brightness difference: `matAmbient` is `ffffff00` for
**8,883 of OoT3D's 9,931** vertex-lit materials, so for 89% of them the ambient term is a no-op and the
whole result rests on the diffuse term; and `matAmbient`/`matDiffuse` span only 11 and 19 distinct RGBA8
values, so this is a small enumerable input space. Also worth recording: the per-draw RGB modulation
fixed in `80c95d21` is *discarded* for vertex-lit draws, because the lit branch overwrites `vColor0`
wholesale — consistent with the native path, and why that fix's measured effect is confined to
non-vertex-lit scene geometry and the title fire-glow.

**The title ambient compared host-vs-oracle, camera-independently — and the apparent hue bug is
refuted.** Since the title image comparison is dominated by the demo camera, I compared the one
lighting term that does not depend on the camera. It is worth comparing: per
`spot00_field_lighting_ground_truth.md` the terrain class has `matDiffuse = BLACK`, which makes every
directional term a provable no-op and leaves
`colour = saturate(2.0 * texel * bakedVertexColor * sceneAmbient/255)` as the whole result — so for
OoT3D's largest vertex-lit material class the ambient *is* the lighting.

At title cs=1093 the host **submits ambient (96,99,68)**, and the 3DS title palette in
`/scene/spot99_info.zsi` (the 4×28-byte entries before `" BDQ"`, ambient at `+0x0A`) reproduces that
exactly as slot 3→0 at w=0.875. The oracle's live `amb0` at daytime `0x2d95` is (48,66,111), which the
same four slots reproduce as slot 3→0 at w≈0.125. **Both engines sample one authored ramp at different
points of the dayTime cycle** — they run different title demos, so that is expected and is not a
colour divergence. `tools/test_title_ambient_parity.py` (9 cases, mutation-verified) locks it.

**Three of my own instruments lied on the way, and all three produced a convincing false bug.** They
are recorded because each would otherwise be repeated:

* `gZelda3dWorldAmbColor` is what the shader reads for ambient, but it is only written when
  `gZelda3dWorldAmbOverride` is 0 and that **defaults to 1** — so it sits at its init `(0,0,1)` forever.
  A probe reading it reports a pure-blue ambient the renderer never used. I "fixed" the probe to read
  it and briefly believed a blue-vs-grey bug; the submitted value is `gZelda3dAmbient`, which
  `Zelda3D_GL_SetLightParams` writes. `soh_z3dlive` now reports both, labelled.
* The host title cutscene clock does **not** advance under a bare `run N` — `soh_titlecs` reads
  `frame=0` for an entire session — so the 3DS palette is never submitted and the host reports
  whatever was left over. Driving the title with `advance_host_title` (as the parity tool does) is
  required, or the probe manufactures a divergence.
* My first blend matcher scanned only the first 512 daytimes of each schedule span. Slot 3→0 reaches
  its interesting weights around daytime `0x3d46`, some 4,762 past the span start, so the scan missed
  it and reported "not from the palette" — the opposite of the truth. It now solves for the weight
  analytically and then checks all three channels, and a bounded scan is mutation-verified to fail.

**MM3D scene coverage measured, and the "no coverage table" blocker is refuted.** `mm3d_draw.h` said
`Zelda3D_TryDrawRoom` "currently returns 0 unconditionally (no MM3D scene coverage table yet)". That
comment was **stale** — the table landed and the comment outlived it. `tools/mm_scene_coverage_survey.py`
measures the actual state against the MM3D ROM, in both directions:

| | count |
|---|---|
| scene-name table entries | 102 (sceneId `0x00`..`0x70`) |
| mapped to an MM3D folder | **102** |
| mapped names with >=1 per-room file | **102** |
| dead mappings (named but absent from the ROM) | **0** |
| per-room scene files in the ROM | 424 |
| reachable through the table | 304 |
| MM3D-only scene folders (no N64 counterpart) | 9 |

**All 102 N64 MM scenes map to a real MM3D folder that exists.** The generator's claim
("MM3D reuses the N64 internal scene segment names verbatim, lower-cased: ALL 102 N64 scenes resolve")
holds exactly — 102 real `DEFINE_SCENE` rows in `scene_table.h`, 102 mapped names, zero dead.

The 9 "unreachable" room files are **not a mapping gap**: `test01`, `test02`, `z2_01keikoku`,
`z2_02keikoku`, `z2_32kamejimamae`, `z2_meganeana`, `z2_turibori`, `z2_turibori2` and `z2_zolashop`
appear **nowhere in MM's N64 scene table** — they are MM3D-only scenes with no N64 `sceneNum` to be
reached from, so the host cannot address them by construction and should not. The separate 111
no-room-suffix files (`<name>_info.zsi`) are likewise unreachable *by construction*, since
`Zelda3D_MM_RoomModelId` always appends `room->num`.

So MM's scene divert is structurally complete, and the comment that said otherwise has been corrected
in place. What remains unproven for MM is not the mapping but the *pixels*: there is still no MM oracle
capture, so none of this is visually confirmed.

Building the survey also exposed a test-design error worth recording: my first suite exercised only the
ROM-gated `survey()` entry point, so **two real mutations — transcribing the table instead of parsing
it, and treating no-suffix files as reachable — both left all 14 tests green**. The classification now
lives in a pure `classify_scene_files()` that the tests call directly; both mutations fail, and the
`transcribe instead of parse` shape is the same failure mode as the LA4 format constant that was
"supported" on paper while `PicaDecode` returned empty.

**A real Majora's Mask coverage bug: 60 actor models were never found.** MM3D is not uniform in how it
names actor archives. Actors that are MM-only live at `/actors/zelda2_<name>.gar.lzs` (387 of the ROM's
460 archives); actors MM3D **shares with OoT3D keep OoT3D's archive name** and live at
`/actors/zelda_<name>.gar.lzs` (72 archives). `ResolveObjectModel` probed only the `zelda2_` prefix, so
every shared actor missed and silently fell back to the N64 model. Measured against the MM3D ROM:

| | object-table names resolved |
|---|---|
| `/actors/zelda2_<name>` only (before) | **324 of 460** |
| with the shared-prefix fallback | **384 of 460** (+60) |

The shared prefix is tried **second**, so the five names present under *both* prefixes
(`gi_ocarina`, `mag`, `mir_ray`, `ny`, `sb`) keep resolving to their `zelda2_` archive — no id that
already resolved changes which archive it gets. The mapping log now prints the archive path, because a
short name no longer identifies the archive.

The remaining **76** names have no archive under either prefix and still fall back to the N64 actor.
That is left deliberately unresolved, and the reason is recorded in
`2ship/2s2h/zelda3d/mm3d_object_names.inc`: six of them are *near-misses* of real stems
(`geldb`~`gelb`, `gi_rupy`~`gi_ruppy`, `gi_shield_2`/`gi_shield_3`~`gi_shield_02`,
`gi_golonmask`~`gi_goronmask`, `gi_bottle_22`~`gi_bottle_21`). Fuzzy-matching those would attach an
object to the **wrong archive**, which is worse than falling back to the N64 model. The other 70 are
simply absent from the MM3D ROM.

`tools/test_mm_actor_prefix_fallback.py` (10 cases) pins the ordering against the **shipping source**
rather than a paraphrase, that the fallback only adds ids and never drops one, that the five ambiguous
names keep their MM archive, and that the near-misses resolve to nothing. Mutation-verified: probing
the shared prefix first fails 1 case and errors 1. Both MM builds compile and both MM tests pass
(`mm_gfx_print_test`, `mm3d_player_animation_policy_test`).

Worth noting how this was found: it is the same shape as the LA4 defect — a value that exists and is
reachable, behind a name the host does not construct. Neither was visible from the code, only from
enumerating the ROM and comparing it against the table the game indexes.

**A generated table could be written to a path nothing builds — and had already drifted.** Applying the
"enumerate the ROM against what the host constructs" method to Ocarina of Time turned up three things
in the scene-name generator:

* **All 324 `.zar` paths the OoT3D layer constructs exist in the ROM.** 325 paths are constructed and
  324 of them are `.zar`, all present; the one non-`.zar` hit, `/scene/spot99_info.zsi`, is present too.
  No missing archive.
* **`tools/gen_scene_names.py` wrote to a path nothing includes.** It emitted
  `Shipwright/soh/src/zelda3d/zelda3d_scene_names.inc`, while the build includes
  `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_names.inc` (via `scene_replacement.c`'s
  `#include "../tables/zelda3d_scene_names.inc"`). So regenerating produced a second, **untracked** copy
  of the same data and left the tracked table stale. The two had already drifted in their header count
  (102/111 generated vs 101/110 written) while every *name* still matched — the worst kind of
  divergence, because it looks correct until someone edits the copy that is not the one that compiles.
  The generator now writes the compiled table, and the only hand-written comment inside it (why
  sceneNum `0x6E` maps to `spot99` at all) is preserved by **sceneNum key**, not by position, so
  inserting a row cannot shift it onto the wrong scene.
* **My own survey had the same undercount it was measuring.** `mm_scene_coverage_survey.py` reported
  "MM3D's own `SCENE_UNSET` scene ids: **0**" for a table that has 11. MM3D writes its empty slots as
  `((unset))`, and a `[^)]*` capture group stops at the `(` of `(unset`, leaving a stray `)` that no
  longer matches `\s*\*/` — so all 11 rows silently failed to parse. That is the bucket that has to
  stay separate from the mapping-gap count, so the undercount was a *wrong verdict*, not a cosmetic
  slip. The corrected survey reports **113 entries / 102 mapped / 11 UNSET**, matching the generator's
  own header, and 102 of 102 still resolve with zero dead mappings.

Both generators are now covered by `tools/test_gen_scene_names.py` (8 cases): each writes the table the
build includes, no stray second copy exists, **regeneration is idempotent** (the only way a generated
table's staleness is detectable without reading it), the header count equals the table contents, and
the preserved comment stays on its own row. The MM survey's parser gained two cases, and restoring the
`[^)]*` group fails both — mutation-verified.

One documented multi-stage-TEV approximation is now **closed by measurement rather than by argument**:
`PREVIOUS_BUFFER` was listed as reading zero because PICA's initial combiner-buffer color is an
uncaptured runtime register, but `tev_corpus_survey.prevbuf_before_latch` walks each chain in stage
order tracking the latch per channel and counts only the reads that reach the un-latched register —
**OoT3D 14/14 safe, MM3D 7/7 safe, zero unsafe in either**, so the `vec4(0)` substitution is exact for
every `PREVBUF` read in both retail corpora. Tracking per channel is what makes that true: a chain-wide
"does this material ever latch?" check is wrong, because a stage latches its own output and cannot read
what it has not written. The survey gained `--game oot|mm` so both games are measured by one walk, and
`TEXTURE3`'s remaining fallback is now grounded at **1 consumed OoT3D material and 0 MM3D**. What
remains of the approximation list is fragment-lighting's `FRAGMENT_PRIMARY`/`FRAGMENT_SECONDARY` and
ProjectionMap.

What remains is confirming `uInvView`'s relationship to the view matrix on a draw whose model-view
rows are not identity, coordinator 1's per-vertex component step, and one gameplay-only measurement
for every method-4 material. `tools/cmb_shader_uniform_coverage.py` now states what the cached corpus
can and cannot settle: it walks every cached command list and reports which CmbVShader uniform blocks
each one wrote, and it finds exactly **one** layout-identified capture (the title wordmark). Two
consequences were corrections rather than confirmations — a written index is not a readable one
because **float-uniform indices are per-material** (a gameplay `c92` reads 1065353216.0 where the
title's reads 3.0, so the same index carries different quantities for different materials), and block
presence is not answerability (the one identified capture has an *identity* model-view, which is
exactly the case question (1) excludes). Items (1), (2) and (4) therefore share one capture
requirement: a gameplay capture recording the material identity and spanning c4..c7, c76..c78, c89
and c92..c95.

### S004 — OoT3D behavior coverage

Grounded actor modules replace selected animation, face, camera, movement, and draw behavior in the OoT
engine.

Gap: complete actor and game-flow coverage is not reached; behavior remains a per-system RE frontier.

### S005 — MM3D behavior coverage

The MM core shares the 3DS asset/renderer layer and has title-specific animation and behavior adapters.

Gap: MM3D coverage is substantially incomplete and must be established independently from OoT results.

### S006 — Independent oracle comparisons

Evidence: the repository embeds Azahar, exposes state and rendering probes, records closed parity cases,
and carries positive/negative controls for its trusted comparison instruments. The maintained libretro
fork now provides ordered Vulkan frame handoff; a validation-enabled run of 30 captured frames and the
ordinary harness CLI both complete successfully.

Gap: **gameplay observation is blocked on one artifact, not on the route.** The cold title recipe does
not reach the current-contract gameplay PlayState (the file-select handoff has no save slots and the
required system-title app is absent from the harness save directory), so no fresh gameplay checkpoint
can be written and `GAMEPLAY_STATE` is absent. The gameplay *route* is sound and has been used:
`boot_to_gameplay` loads a cached state and skips the title entirely, and it produced every cached
gameplay capture under the render-contract marker in force today. The only gameplay state on disk is
an unmarked predecessor, and the current Azahar build **cannot deserialize it** —
`terminate called after throwing an instance of 'boost::archive::archive_exception'` — so it is stale
rather than misnamed. Reseeding it needs the cold title route, which is what fails. That single missing
savestate is what `render.cmb-texcoord-mapping` items (1), (2) and (4) are waiting on, and
`tools/cmb_shader_uniform_coverage.py` now names the other half of their requirement: a capture that
also records the bound C material, because float-uniform indices are per-material and Azahar's
per-draw log prints `idx` as `is_indexed`, not a material. See
[issue #23](issues/0023-embedded-oot3d-oracle-cannot-reach-its-boot-hand.md).
**Gameplay is not the only reachable oracle surface.** Loading the cached title savestate and running
from a cached title checkpoint does work on this host: `tools/title_oracle_probe.py uniforms 1093`
completed and cached 102 per-draw uniform records under the current `p45-00401070` contract, and
`pica-command-list` served draw 77's raw command list from cache. Title-scoped evidence — including
the `ShaderMode.w` correlation behind S003 — is therefore available now; only gameplay-scoped
evidence is blocked.

### S007 — Packaged player setup

The documented AppImage first-run flow validates direct ROMs or one bounded nested ZIP per selection,
persists choices under user configuration, and excludes ROM-derived archives from the package.
N64 acceptance shares extraction's exact-revision whole-image CRC validation. 3DS acceptance verifies
the consumed decrypted RomFS structure and every asset/hash block, including dumps whose decryptor
retained encryption flags. This proves asset-container integrity, not Nintendo signature authenticity
or executable/other-partition contents. Lucent owns archive safety and resource bounds; failed content
validation or configuration writes preserve the previous managed ROM and selection.

Gap: the final artifact and first-run release gate remains open, so source-path behavior does not yet
verify the packaged release end to end.

### S008 — Linux CI coverage

The Linux product job restores only the public build submodules at their gitlinks, builds pinned
SDL3 and the real `zelda3d_app`, `soh_core`, and `mm_core` targets through the shared Python build
owner, runs CTest, and executes the application's simultaneous-core ABI/symbol-isolation probe.
Checked-in asset-name headers and the port's custom `soh.o2r` need no game input. Player-owned
`oot.o2r`/`mm.o2r` extraction remains a launcher provisioning postcondition, separate from compilation.

Local combined-gate evidence: the coherent Clang app/core build passes 499 registered CTest cases
(487 executed, 12 declared asset-dependent skips, zero failures). The application loads both cores
simultaneously and verifies three private symbol pairs with zero shared addresses. An unchanged
second Ninja build compiles zero C/C++ translation units; the existing custom-archive target still
refreshes `soh.o2r`. The normal Clang gate verifies 5,551 Clang compile entries, format-checks 122
changed files, lints 82 changed translation units, and structure-checks 3,385 source files.

This is not a warning-clean whole-product claim: the rebuild reports 176 unique inherited warning
sites/messages across 60 first-party files, all byte-identical to the pre-batch HEAD. These are not
176 independent causes: 55 diagnostics share the dungeon-item subscript macro and 29 share the
separate MM scene-command factory hierarchy. No warning-bearing file is changed by this batch;
the remaining baseline requires its own semantic audit and repair rather than warning suppression.

Gap: the full hosted job has not yet produced a successful run; package installation and real-title
gameplay/performance remain unverified by this asset-free boundary.

### S009 — macOS arm64 CI coverage

The portable workflow targets GitHub's `macos-14` arm64 runner and defines the same locked Python and
Clang native-policy gate used on Linux.

Gap: no complete macOS application build is recorded. The product launcher currently hardcodes ELF
`.so` filenames and Linux `/proc/self/exe` discovery; MM also applies ELF `-export-dynamic` link
options without a platform guard. Those production owners need portable implementations before the
macOS job can honestly exercise the complete application. The present seam job is partial evidence.

### S010 — Windows x86_64 CI coverage

The portable workflow targets `windows-latest` and defines the same asset-free launcher/configuration
and Clang native-policy gate, including the production Windows vcpkg refusal/configuration contracts.

Gap: the launcher unconditionally uses `dlfcn.h`, `libgen.h`, `unistd.h`, `dlopen`, `realpath`, and
Linux core filenames. The game-core entry exports have no Windows export contract, and MM's build
applies GNU compile/link flags unconditionally. These are production portability defects, beyond
vcpkg provisioning; the current seam job does not establish a complete Windows application build.
Setup persistence uses UTF-8 JSON and native filesystem paths, with wide Windows environment
handoff; both 3DS asset consumers retain those native paths. The inherited N64 exporter still narrows
selected/workspace/output paths to `std::string` and passes the original absolute ROM path to ZAPD's
`char*` arguments. Preserving native paths through that workspace and exporter boundary remains
required before claiming end-to-end Unicode Windows extraction or setup.

### S011 — Android delivery

Missing capability: Zelda3D has no Android application target, package, or runtime integration, so an
Android CI job would be a false platform claim. Android remains missing until an actual consumer of the
shared Android port boundary exists.
