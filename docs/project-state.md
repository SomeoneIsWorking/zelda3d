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
| S003 | The PC renderer reproduces the reached PICA200 material, texture, lighting, fog, and transparency semantics | partial (texture formats **and** sampler filtering/wrapping: **verified for both games**; **OoT's PICA distance fog: measured and applied on both host routes**, MM's is missing per S005; scene fog: **OoT closed, MM3D CONFIRMED by prediction** — the recovered MM3D record's window reproduces MM3D's authored PICA LUT to 1.19 byte steps (mean-abs 0.00466, control 0.02117), so the fog port is feasible with the existing host mechanism, and MM3D's camera near plane is ~77 against OoT3D's 7; material/fragment lighting still partial — **the per-light transport contract is now MEASURED on MM3D's registers** (per-slot: `diffuse`, `specular_0` and the light direction; MM3D's two lit slots are both `directional`, exactly antiparallel in x, and NOT the same colour, so neither a negated-direction nor a copied-colour reduction works), and **the two-light configuration counterfactual is now MEASURED in MM3D** (`max_light_index=1`/`slot_mapping=[0,1,...]` 12/12, `config0=0x80000400` 12/12, `config1` `0xff7fffff` 11/12 and `0xff7effff` 1/12 so it is not a constant; the host's slot count of 2 is confirmed with a denominator), and the builder's one open `+0x18A` bit is cross-title **and is now NAMED: it is `config0` bit 17 = `shadow_secondary`**), and **the captured configuration is now REDUCED to its shading terms: ZERO optional terms for 11 of MM3D's 12 lit captures and exactly `lut:Distribution0` for the 12th, which makes fragment lighting a bounded port rather than the large speculative surface it was framed as (see "The captured configuration needs NO optional lighting term")** | S002 | G001, G002 |
| S004 | OoT3D actor animation, facial, camera, and game-specific behavior replaces N64 behavior where grounded | partial | S002, S003 | G001 |
| S005 | MM3D actor animation, presentation, and game-specific behavior replaces N64 behavior where grounded | partial (**scene lighting SUBMITTED and the PICA fog WINDOW is now FED to the renderer** — MM's `z_kankyo` captures the blend schedule and `Mm3d_UpdateFogWindow` feeds `Zelda3D_Fog3dSet` with the recovered window and MM3D's measured camera near plane ~77, using the SHARED window rule `Zelda3D_EnvBlendWindow` that OoT3D also uses, so the two-LERP exists once (8 unit tests) -- and **its runtime effect in MM gameplay is now OBSERVED and CROSS-CHECKED**: the `fog` REPL command reports the live window, the captured schedule, AND independently re-reads the recovered 3DS record, so the diagnostic can be wrong. In `z2_clocktower` (scene 111) the live window is `near=926.0 far=20000.0 zFar=52000.0 camNear=77.0` at blend weight 0, which is **exactly** recovered slot 1 (`926, 20000, 52000`, residual 0.00 over all three distances); at weight 0.032 it reads `911.0 / 20000.0 / 51617.7`, which is MM's own LERP between slots 1 and 0, and the diagnostic then says `no recovered slot matches (expected mid-blend)` instead of claiming agreement. The 3DS record demonstrably reaches MM's renderer. Getting that run required fixing two real defects, both recorded below; **scene lighting is now SUBMITTED**: the recovered 3DS records are installed into MM's own `lightSettingsList` at `2ship/2s2h/z_scene_2SH.cpp:281` `Scene_CommandEnvLightSettings`, so MM's N64 blend — time LERP, config LERP and the additive `adjLightSettings` term — operates on 3DS data with no reimplementation; **fog's window is validated against live hardware** (slot 2 predicts MM3D's authored PICA LUT to 1.19 byte steps) the fog COLOUR still needs MM's runtime `adjLightSettings` (its blend is ADDITIVE, so the colour is outside the table's convex hull), so **this is not MM fog PARITY**: the window feeding the curve is the recovered 3DS one and is unit-tested against the validated value, but the colour the renderer actually hazes toward is still the N64 one, and no MM frame has been compared either way; previously: scene-authored fog **data RECOVERED and independently validated** — MM3D's env region is command `0x0F` in its scene ZSI, inflated first (182 of 424 are LzS), base `ptr+0x28` stride `0x20` with the N64 `EnvLightSettings` at `+0x0B`; **MM3D also has an oracle now**; the table is generated with 102/113 scenes populated; the fogged-frame counterfactual EXISTS -- the opening runs PICA fog mode 5 on 75-84% of its draws and its per-draw fog colour matches the frame-level az_fog register exactly, including the transition between frames 3200 and 3800, so an earlier 'the opening is fog mode 0' reading of this project was wrong; the submission is outstanding and is NOT a copy of SoH's -- MM's z_kankyo ADDS a per-slot adjLightSettings offset to each of its four source colours before the time LERP, so the shared contract needs that term; so it renders unfogged but no longer lacks data) | S002, S003 | G002 |
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
`mm3d-decomp` is the named next RE step. Separately, the PICA lighting **configuration builder** (`FUN_0040cdd8`) is now executed rather than described: it is transcribed to `oot3d-decomp/tools/pica_lighting_config.py` and reproduces all three oracle-observed registers for the grounded fixture from decompiled source (`config0=0x80000400`, `config1=0xff7fffff`, `light_enable=0x00000010`) under 19 mutation-verified tests. So the fragment-lighting *formula* and its *mode selection* are both portable; the one remaining unknown is the **producer** of the builder's input object — which CMB material fields populate the mode bytes, the eight slot-enable bytes and the three flag planes. That is a transport question, and it is what gates implementing `FRAG_PRIMARY`. **RETRACTED -- an earlier revision of this row claimed the object is NOT the per-material record, and that was wrong.** It rested on a memory read taken without the title demo running, so the region came back unpopulated; re-measured with `run 400`, `+0x180..0x1C0` is populated (slot 0: 32/64 non-zero, `+0x18A = 0x80`) -- exactly what `oot3d-decomp/docs/fragment_lighting.md` recorded on 2026-09-27 as "FOUND: the object is live runtime state at `CmbRenderer + 0x400 + index * 0x4C8`". My own `--slots` run had already reproduced those numbers and I published a refutation anyway: **a project-recorded finding was overturned on a read that could not support it.** The RAM scan that followed is void for the same reason -- it searched for the constructor's `+0x18A == 1 AND +0x18D == 1`, while the live objects read `0x80/0x00` and `0x00/0x00`, so all 7508 candidates were false positives and its stage-2 copy test measured noise. `+0x18A` is *source data* in the object, not a constructor-written marker, which is the doc's own reading. What survives is the method: the non-zero-density gate, the only reason the false negative was caught, and `run 400` as the precondition of every memory read here -- **and that precondition is now mechanically enforced rather than remembered**: `tools/harness_memory_read.py` is the single owner of it, `lit_object_dump.py` and `lit_object_scan.py` both call it, it raises on a failed boot, and it refuses an all-zero read (a live heap is never uniformly zero, so a zero read means the warm-up or the address is wrong; a *partially* zero read is real data and passes). A declined range returns `None` as end-of-segment while a short transfer raises, distinguished by a structured `reason` rather than by message text -- a test caught the first version conflating them. Verified end to end: booting without `warm()` and reading `0x081d3ea0` now raises `reason=empty` naming the missing warm-up, which is the exact mistake that caused the retraction. See `docs/re-frontier.md`. **The object is located and live-readable; the open question is the PROVENANCE of its 0x4C8 source**, which the doc already localised to a separate per-material object in game data rather than the CMB material.
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

**The same bug class had a second instance: the per-draw texcoord scroll, also live on the title.**
Having found one "the unified route copies the frame-level values and drops the per-draw value", the
next question is how many more there are, so the audit was run by hand over the native per-group
writes against the unified packer's copies. The fog gate was one; the **UV scroll** is another, and
the field it needed was already in the UBO as dead weight: `uEnvColor` was written as `0.0` and read
by nothing on either route — a size-parity filler in exactly the slot a real input wanted.

The native applies the scroll in its vertex stage as `aUv + uExtra.yz`, i.e. an ADD after the V
flip (`1.0 - aUv.y + uExtra.z`). The unified's non-sphere tex0 path was `vec2(aUv0.x, 1.0 - aUv0.y)`
with no term at all, and the per-*group* coordinator transform it did carry (`uTex0Xf`) is a
different, per-group input, so it could not stand in. That the omission was **visible content** and
not a dead field needed a number rather than a reading, so `uvScroll` was added to the per-draw
`[Zelda3D_SG] draw N` list — the per-group `uv=` in an `sgdump` row is the group's own UV, not the
draw's scroll, so a capture without it cannot tell "the band does not scroll" from "no draw asked it
to". At title cs=1093: **2 of 101 draws carry a non-zero scroll, both `(0.57222, 0)`, on models 2002
and 2004** — the OoT3D sky cloud band's `.cmab` rate, i.e. 37500/65536 of a texture width.

The port adds the term after the flip, in **both** non-sphere tex0 branches, because a draw that
lands on the variant without it would lose the scroll purely through classification. It is added
unconditionally because it is zero for every draw that did not ask for one, which is what makes that
safe rather than a second policy. `UnifiedShader.ThePerDrawUvScrollIsAddedOnEveryNonSphereTex0Path`
locks both branches and forbids the pre-flip form; mutation-verified — turning the post-flip add into
a pre-flip subtract fails, and removing the term fails.

What is **not** claimed: a pixel win. At cs=1093 `union_rgb_mae` moves 38.23 → 38.09 and `content`
0.4807 → 0.4760. This instrument's measured noise floor is **0.73 mean-abs** and one frame is worth
~5.9, so 0.14 is five times below the floor and the sign of the `content` move is opposite. The claim
is that the unified route now does what the native route does, on an input measured to be live; the
metric does not resolve it and is not cited as if it did.

**The fog port is closed at the PICA registers, not only in pixels — and the fog COLOUR is a trap, not
a bug.** The oracle's per-draw fog field at the title (`vsuni_log`, 102 draws at az=2016) reads
**`fog=5/0(129,96,53)` on 59 draws and `fog=0/0` on 43**. So the 3DS programs exactly one fog mode
per draw and "not this material" is a real per-draw mode-0 — the host's `fogEnabled` boolean models
the oracle's own field rather than approximating it — and `flip=0` everywhere, so the host's single
no-offset window is the only shape in this frame. The host's recovered LUT lands where the oracle's
does: node 127 = **+0.9803** at eyeDist **871.8** against the recorded oracle 0.979 at eye 834.

The colour nearly produced a fix to a non-bug. Read at *unmatched* points — the oracle at az=2016,
the host at cs=120 — the fog colour is (129,96,53) against (24,18,34), which reads exactly like a
wrong colour source and would have justified changing code. Both are time-blended, so two different
instants are not a disagreement. At a matched pair (host cs=1093 vs oracle az=2016) the host is
**(154,114,57)** against the oracle's (129,96,53): same hue, ~19% apart, and the *ambient* shows the
identical offset already recorded above (host palette w=0.875 against the oracle's w=0.125). Both
paths already feed the 3DS palette into the one field the shader reads — the title through
`ApplyPalette` -> `envCtx.lightSettings.fogColor`, gameplay through the `pal[...].fogCol` two-stage
blend in `Zelda3D_SceneLightSettingsOverride`, whose comment carries the same Zora measurement
(N64 (25,100,100) teal against the oracle's live PICA (104,135,181)). **The colour was never wrong;
only the gate was missing.** `soh_fog3d` now prints the colour alongside the window, because a fog
with the right depth and the wrong colour is still wrong and a diagnostic that reports the window
alone makes that invisible.

**The hand audit became a tool, after it had already found two bugs by hand.** Both carriage bugs
were found by reading the packer and comparing, which finds the first two and misses the third, so
`tools/unified_carryage_audit.py` mechanises it: it reads the `SgUbo` field list from its owning
header, separates what the packer writes per DRAW from what it writes per FRAME, works out which
fields each route's *generated GLSL* actually reads, and then requires every per-draw field to be
either carried field-for-field or named in a `SUBSTITUTIONS` table with a reason. It prints what it
scanned (31 `SgUbo` fields, 17 per-draw, 6 per-frame) and what it matched either way, and exits
non-zero on a gap. Current verdict: **14 carried, 3 substituted, 0 dropped, 0 dead**; `uShadow` and
`uFog2` are per-FRAME fields no route reads and are listed as such rather than as gaps.

Writing the tool caught three defects in the tool, each of which would have made it report all clear
while the fields it was written to check were missing from its own view:

* **Per-draw vs per-frame was inverted for exactly the interesting fields.** The first rule was
  "written on `ubo`, minus those also written on `base`" — but `uParams`, `uExtra`, `uTintSkin` and
  `uFog` are all set on `base` for the frame and then *overridden* per group, so the subtraction
  deleted all four. It reported 10 per-draw fields and consulted no substitution at all. A field is
  per-draw if any group can override it, full stop.
* **Carriage was read from helper DEFINITIONS, not call sites.** The first version scanned
  `unified_ubo.h` for `source.uX`, so `PackCmbFogGate`'s `source.uFog[3]` counted as carried whether
  or not the packer called it — and deleting the call, which *is* the original bug, still reported
  clean. A definition nobody invokes is not carriage. Now the packer's unified block is scanned for
  `Zelda3DUnified::<helper>(` calls and only those helpers' bodies contribute.
* **The `SgUbo` field list silently dropped `uTevStages`.** The declaration is
  `uint32_t uTevStages[6 * 4];` and the subscript pattern was numeric-only, so one field was missing
  from the authority the audit measures against. A test that the parse is non-vacuous caught it.

Mutation-verified against the real tree: deleting the `PackCmbFogGate` call reports `uFog` dropped and
exits 1; deleting the `CopyCmbVertexLightBank` call reports five fields dropped; deleting a `memcpy`
reports that field. `tools/test_unified_carryage_audit.py` (13 cases) plus the generator/parser tests
from the previous turn are now in the hosted CI list, since they are ROM-free structural gates for
the shipped renderer.

**The fragment-lighting transport is closed, and one of its load-bearing readings was wrong.**
`oot3d-decomp`'s `fragment_lighting.md` had recorded, as a blocker, that the per-draw lighting object
is "a **separate 0x4C8-byte object**, authored per material by the 3DS toolchain and held in game
data" — so the only remaining route was to find that blob in the data archives. There is no such blob.

`FUN_004c34ac` (408 B, the single caller of the object's constructor `FUN_004c6264` at `0x004c3528`)
builds the per-material state: zero four words, construct the object at **record + 0x10**, advance by
`0x73` words — so the record is **0x1CC bytes at stride 0x1CC**, and the object is constructed and fed
at build time, not copied. Its last statement is `FUN_004c6364(piVar8 + 3, *piVar8 + 0xcc)`: the
descriptor feed, whose input is **material + 0xCC**. `FUN_004c6364` then reads offsets
`{0x10,0x12,0x14,0x18,0x1C,0x1E,0x1F,0x20,0x23,0x24,0x26,0x28}` through the pointer it just stored, and
those twelve are exactly `CmbMaterial::fragment_lighting_descriptor`'s twelve field names in the
shipping parser. **So the mode bytes need no new asset data — the host already retains every field the
feed reads**, and the gate is `material + 0x00`, which it already parses as
`CmbMaterial::fragment_lighting`. The note's own arithmetic ("a 0x4C8-byte source cannot live inside a
material entry") was correct and was the tell: the source was never 0x4C8 bytes.

**The reading that would have produced a plausible bad port.** That same function's `+0x138` /
`+0x13C..+0x158` block was recorded as the fragment-lighting mode bytes. It is **alpha-blend state**.
Measured over both corpora at the same base the host uses (`mats + 0x0C`, stride 0x15C/0x16C):

| field | OoT3D | MM3D | reading |
|---|---|---|---|
| `+0x138` | 2 values (0: 9661, 1: 1511) | 3 values (0: 5697, 1: 1093, 2: 1) | blend enable |
| `+0x13C` | 4 values, 100% GL blend enums | 4, 100% | `blendSrcRGB` = 0x0302 x10781 |
| `+0x13E` | 6 values, 90.3% GL enums | 3, 89.3% | `blendDstRGB` = 0x0303 x9970 |
| `+0x140` | 2 values, 100% (0x8006 x11166) | 2, 100% | `blendEqRGB` = FUNC_ADD |
| `+0x00` | **205 of 11172** | **6428 of 6791** | **fragment-lighting gate** |

Bounded enum indices cannot be GL enums, and the `+0x00` counts reproduce the two figures this project
already had from the *combiner* side (`cmb_fragment_lighting_survey.py`: 205 OoT3D, 6,428 MM3D) — an
independent direction, so `+0x00` is the gate and the host's blend names at `cmb.cpp:276-284` are
right. Had that block been ported as fragment-lighting input it would have produced plausible,
corpus-wide, entirely wrong lighting for every material.

**The Thumb caller is now excluded by a decoder that works.** The note said the only unexcluded case
was a Thumb `BL`/`BLX`, because the previous decoder matched 100% of halfword positions with a 1:1
target ratio and put 2.2% of targets back in the image — reading data, not instructions, and correctly
refused as non-evidence. `oot3d-decomp/tools/callers_thumb.py` (15 cases validated against
hand-computed encodings rather than against the corpus, since a corpus ratio cannot tell a wrong
decoder from a false positive) gets two details right that decide the whole thing: the PC is
`(address & ~3) + 4` with the low two bits cleared, and `BLX`'s second halfword is
`11 J1 0 J2 H imm10H`, so the offset field is **10** bits and bit 0 is the H flag. Reading 11 bits folds
H into the offset and is wrong for every BLX — that one bit alone dragged the measured in-image
fraction from 70% to 21%. Over 22,223 matches it finds **zero** whose target is a known function entry,
so `code.bin` has no Thumb branch reaching code at all. The control is therefore the function-start
fraction, not the in-image fraction: this image is mostly ARM, and a Thumb match inside ARM data still
lands in the image, which is why a correct decoder cannot post the ARM scanner's 70% here.

**What is left for fragment lighting is now short and none of it is the transport:** the eight
slot-enable bytes, set by `FUN_003fa5d0` (1608 B) and `FUN_003fa34c` (672 B) in the CMB *renderer* rather
than by the construction chain, so PICA's `lights_num` must be read out of those two functions and
matched against the host's two-enabled-slot model instead of assumed equal; and a measurement conflict
— the construction chain's stride is 0x1CC while the live dump measured 0x4C8 at
`CmbRenderer + 0x400`, so those are two different arrays and the live dump must not be read as this
one. The configuration counterfactual still needs one lit material, and the title demo never enables
fragment lighting (0 of 207 draws, `picaLit` register), so that half remains behind issue #23.

**The per-draw fragment-lighting path is now fully read, and it has one missing input, not an RE
blocker.** `FUN_003fa34c` (672 B) is short enough to state completely: gate on `material[+0x00]`, set
`object[+0x164 + i] = 1` for each of **three** light slots whose `+0xE4` equals `1.0f`, compute
`object[+0x10..+0x12]` as a clamp+scale of the material's own `+0xA0/+0xA4/+0xA6`, and call
`FUN_00308498` — the already-confirmed chain. `FUN_003fa5d0` (1608 B) is the same routine with the full
per-slot pack, and negates the slot direction inside the same enable test, so what it submits is
light-TRAVEL.

Three of the four inputs are already in the host's parsed data — the gate is
`CmbMaterial::fragment_lighting` (205/11172 OoT3D, 6428/6791 MM3D, both re-measured this turn), the
`object[+0x10..+0x12]` terms are `mat_ambient`/`mat_diffuse`, and the mode bytes are
`fragment_lighting_descriptor`. The builder is already transcribed and mutation-tested, and the
fragment-lighting *maths* is the oracle's own software rasterizer rather than a recovered unknown.

**The fourth input is a real finding, and it is a constant where a predicate belongs.** The rig is a
**three-slot array at stride 0x60**, and the third slot is enabled by the same `+0xE4 == 1.0f` test as
the first two. `per_draw_light_setup.md` records two opposed directional terms and concludes "the
standard N64 two-light rig" — true of the two configurations *observed*, and the oracle's per-draw log
agrees that two slots are occupied in every sample, but nothing in that data distinguishes "two slots"
from "three slots, third disabled every time". The host encodes the observation: `Zelda3D_GL_
SetLightParams(ambient, light1Color, light2Direction, light2Color, 2)` passes a literal `2` that becomes
the enabled-slot count. That is right for every configuration measured and **silently drops a third
slot** if one is ever enabled. Recorded rather than attempted: the honest claim is "three slots, we
observe two", and widening the light-bank UBO on an unverified third slot is the same shape of
speculative change as the `+0x138` misreading above. The `+0xE4` producer is also not in the
decompiled set (1,321 of the image's functions), so "which field is `+0xE4`" stays open — the *rule* is
read from its consumer, which is enough to evaluate the predicate once the producer is found.

**This does not license aliasing `FRAGMENT_PRIMARY` to the vertex `PRIMARY`.** The section supplies
inputs; it does not supply the counterfactual a port must be checked against, and the title demo never
enables fragment lighting (0 of 207 draws on `regs.lighting.disable`), so that check is still behind
issue #23. The 0x1CC-vs-0x4C8 stride conflict is also unchanged.

**The fragment-lighting counterfactual is now blocked for three measured reasons, not one hint.**
The recorded reason was a *state* argument — 0 of 207 title draws on the authoritative
`regs.lighting.disable` register. Two more, and the second is a content fact:

* **The title's content contains no fragment-lit material at all.** The 205 fragment-lit OoT3D
  materials live in 150 files, and `/scene/spot99_info.zsi` — the title's own scene — has **none**. So
  even a perfect title route with every draw captured could not produce a fragment-lit fixture: there
  is no material in that scene that asks for one. That is a property of the retail content, not of the
  route, and it is why "spend more title-side effort" is the wrong instruction rather than a hard one.
* **The equipment screen is the only large fragment-lit population reachable without gameplay, and
  the title does not enter a menu to reach it.** `menu_link_ura.cmb` and `menu_link_omote.cmb` are
  30 of the 205 — the equipment screen's Link model. Reaching it needs the menu, and pressing START
  mid-title does nothing (the title is a **scripted playback**, so input is ignored until the script
  ends); running the host title past `cs=2400` to its end and pressing START there leaves the same
  scene — 65 draws, one model, `fragLit=0` on all of them, static camera, cursor still advancing past
  the script's end. The host never transitions out of the title presentation, matching the recorded
  oracle behaviour ("Start at the logo → 200 frames black → sky-only screen, stuck 2400+ frames").
* **MM3D could not supply one either — and that reason is now REFUTED and replaced with a
  measurement.** This bullet previously read "MM3D has no oracle capture at all". MM3D has an oracle
  (see below), and the counterfactual was sitting in it. On the authoritative `picaLit` field
  (`regs.lighting.disable`), MM3D's opening is fragment-lit on **116–143 of 131–161 draws per frame**
  across frames 1200–3800. So the third blocker dissolves, and the two OoT3D reasons above are
  untouched: they are about OoT3D's *content*, and no MM3D evidence can speak to them.

**What MM3D's live registers actually say** (`lighting_capture`, 12 captures at frame 2000 of 24
armed — the misses are draw indices that stop being submitted when the scene advances between the
arming frame and the next one, reported here rather than dropped):

* **Two lights, not one and not three.** `max_light_index = 1` and `slot_mapping = [0,1,0,0,0,0,0,0]`
  on 12 of 12, with both slots carrying real `specular0`/`diffuse`/`xy` data. This is the first
  **two-light** fragment-lit fixture in the project — Gravekeeper's Hut is a ONE-light material, so
  the OoT3D fixture could never discriminate the host's hardcoded slot count. `Zelda3D_GL_SetLightParams`'s
  count of 2 is now consistent with every capture, with a denominator.
* **`config0 = 0x80000400` on 12 of 12** — the same word the OoT3D Gravekeeper capture recorded. Two
  independent titles with separate material compilers land on it, so it is the platform's baseline
  configuration word, not a fixture coincidence.
* **`config1` is NOT a constant:** `0xff7fffff` on 11 of 12 and `0xff7effff` on 1, a real per-material
  difference. **The difference is at bit 16 (`0x10`), NOT bit `0x11`, and it is `disable_lut_d0`** --
  an earlier revision of this row said "bit `0x11`, which the recovered builder names
  `MODE_SPOT_INDEX`", which is wrong twice over and is corrected here. The XOR is `0x00010000`,
  which is bit 16 under any reading; bit `0x11` (17) is **set in both words** and so cannot be
  their difference. `Azahar/src/video_core/pica/regs_lighting.h:200` names bit 16 `disable_lut_d0`
  and bit 17 `disable_lut_d1`. The recovered builder independently agrees that bit **16** is fed
  by object byte `0x18F` (`MODE_SPOT`), not by `0x190` (`MODE_SPOT_INDEX`, which feeds bit 17) --
  so the caption disagreed with the builder it cited as well as with the register map. The
  consequence is not cosmetic: this is a **specular** difference, not a spot one, and section
  "The captured configuration needs no optional lighting term" below shows it is the only
  optional term any captured MM3D draw needs. Any host that hardcodes `config1` is still wrong for
  the material that differs -- now for a knowable reason. Pinned by
  `tools/test_pica_lighting_registers.py::CapturedWordDiffTests`, which also asserts bit 17 is set
  in both words so the wrong reading cannot come back.
* **This strengthens the one open bit.** The builder's constructor default predicts
  `config0 = 0x80020400` — the observed word plus bit `0x11` (17), which comes solely from the
  constructor's `+0x18A = 1`, a byte `FUN_004c6364` does not write. **That bit is now NAMED: it is
  `config0` bit 17 = `shadow_secondary`** (`regs_lighting.h:203`), and the builder's
  `CONFIG0_BITS[0x18A] = 0x11` is consistent with that. The single OoT3D fixture already
  showed that bit clear; MM3D shows it clear on 12 of 12 draws in a different title. "The ordinary
  lit path clears `+0x18A`" is now cross-title, not a single observation. Which code clears it is
  still open, and is recorded as such rather than guessed.

**The captured configuration needs NO optional lighting term, which is what makes `FRAG_PRIMARY`
portable at all.** Until now the blocker on fragment lighting has been framed as "the PICA
fixed-function light/LUT calculation is not ported", i.e. as a large speculative surface:
`ComputeFragmentsColors` (`Azahar/src/video_core/renderer_software/sw_lighting.cpp:24-333`)
evaluates six lighting LUTs, distance attenuation, spotlight attenuation, a bump map and shadow. A
port that implements "all of it" for both titles is a lot of speculative surface, and both
captured titles' *titles* being fragment-free is what kept it unexercised.

But `config0` and `config1` are **per draw**, and they say exactly which of those terms can be
non-trivial. Reducing the oracle's own captured words through the register map
(`tools/pica_lighting_registers.py`, which PARSES `regs_lighting.h` rather than transcribing it,
because a wrong bit does not raise — it produces a clean number) gives:

```
config0=0x80000400 config1=0xff7fffff
  lighting config = 0 (Config0)
  bump_mode=0 clamp_highlights=False enable_shadow=False
  Distribution0          config_supports=True  lut_enabled=False
  Distribution1          config_supports=False lut_enabled=False
  Fresnel                config_supports=False lut_enabled=False
  ReflectRed             config_supports=True  lut_enabled=False
  ReflectGreen           config_supports=False lut_enabled=False
  ReflectBlue            config_supports=False lut_enabled=False
  SpotlightAttenuation   config_supports=True  (gated per slot, all 8 slots disabled)
  per-slot: shadow=False spot_atten=False dist_atten=False
  ACTIVE TERMS (0): none
```

**Zero.** So for 11 of MM3D's 12 captured lit draws, `ComputeFragmentsColors` degenerates to a
closed form with **no half-vector, no LUT, no attenuation, no shadow and no bump**: for each
enabled slot `num = light_enable.GetNum(slot)`, with `L = normalize(position)` (both MM3D slots are
`directional`), `n_dot_l = max(dot(L, N), 0)` (or `|.|` when `two_sided_diffuse`),

```
diffuse  = global_ambient + Σ_slots ( light.diffuse * n_dot_l + light.ambient )
specular = Σ_slots ( light.specular_0 + light.specular_1 )
```

each clamped to `[0,1]`. The single most counter-intuitive consequence: **the specular is FLAT.**
With `disable_lut_d0 = 1` the code leaves `d0_lut_value` at its initial `1.0f`
(`sw_lighting.cpp:214`), so `specular_0` is added with no `N·H` term at all. A port that
implements textbook Blinn-Phong specular here would be wrong on **75–88% of MM3D's materials**,
and would look plausible.

**The 1-of-12 variant is the whole exception**, and it is exactly the bit identified above:
`0xff7effff` clears bit 16, so `disable_lut_d0 = 0` while `Config0` does support `Distribution0`,
and the reduction is `ACTIVE TERMS (1): lut:Distribution0` — one real half-vector specular
(`N·H` through the distribution-0 LUT, with `lut_input.d0` selecting the dot product). So the port
needs the reduced form as the common case and `Distribution0` as the exception, and nothing else.

**The instrument is falsifiable in both directions**, which is the only reason to believe it. The
same reducer, fed a maximally-enabled but still legal hardware word (bit 18 kept set so the word
could exist), reports **14 active terms** — bump, shadow, clamp-highlights, per-slot shadow, spot
and distance attenuation, all six LUTs and both Fresnel alpha terms. An instrument that could only
ever say "nothing needed" would be indistinguishable from a broken one, so the positive direction is
tested too (`test_instrument_can_report_the_other_answer`).

23 tests, all mutation-verified: the wrong-bit mutation, an emptied `Distribution1` exclusion set,
removing the hardwired-bit-18 refusal, a shadow gate reading the wrong 8-bit field, closing the
documented `Config7 == 8` hole, and reading `bump_mode` from the wrong field each fail
(`tools/check_pica_lighting_registers_mutations.py`, 6/6 caught, target restored byte-identical).

**Two instrument defects found while verifying it, both of the "reports success while measuring
nothing" class this project keeps re-learning, and both recorded so neither is repeated.** (1) The
`SpotlightAttenuation` sampler is gated per slot by `disable_spot_atten[0-7]`, **not** by a
`disable_lut_sp` bit — bit 18 is a hardwired dummy precisely because no such bit exists
(`regs_lighting.h:203-205`). Treating the sampler set as one list raised `KeyError` on the first
real capture, so the two sets are now explicit. (2) **The mutation harness was measuring a stale
`.pyc`.** A mutation that preserves the file *size* (`8:` -> `7:`) leaves the cached bytecode valid
under Python's `(mtime, size)` staleness check, so the "mutated" run imported the ORIGINAL module
and the check reported a clean pass for a mutation that was never in effect; one mutation also
`continue`d without restoring, leaving the previous mutation on disk for the next run. The driver
now purges `__pycache__` and asserts the target is restored byte-identically, and it reports
`applied=` per mutation so a harness failure is visibly different from a surviving mutant.

**What this does and does not unblock.** It does NOT change the state of the port: `fragPrimary`
is still bound to the vertex-lit primary and `fragSecondary` to zero at
`zelda3d_sdl3gpu_shaders.cpp:386-387` and `unified_shader.cpp:437-438`, so **the user-visible
result of this turn is unchanged** and no parity row may move on it. What it changes is that the
remaining work is now a bounded, specified surface rather than an open one: the TEV evaluator
already takes `fragPrimary`/`fragSecondary` as parameters (`zelda3d_tev_glsl.h:71`), the combiner
source codes 1 and 2 already exist (`cmb_glgroups.cpp:33-36`), the per-material gate is already
transported (`cmb_glgroups.cpp:189` -> `zelda3d_sdl3gpu_pass.cpp:923`), and the eight slot-enable
bytes plus the mode bytes are already transcribed and mutation-tested. The remaining unknowns are
the per-slot **colour** values (`diffuse`/`ambient`/`specular_0`/`specular_1`) and their producer,
plus a counterfactual — and the counterfactual is the cheap half now, because MM3D's reproducible
Lost Woods state is 75.2% fragment-lit with no gameplay `PlayState` needed.

**The material side of the specular is RECOVERED and the host simply was not reading it.** The
colour block is five authored RGBA8 entries — `+0xA0` emission, `+0xA4` ambient, `+0xA8` diffuse,
`+0xAC` specular 0, `+0xB0` specular 1 (`oot3d-decomp/docs/fragment_lighting.md:20-27`) — and the
tracked decomp `build/decomp/003fa5d0.c:62-77` reads exactly `+0xAC/+0xAD/+0xAE` and
`+0xB0/+0xB1/+0xB2` to form `material.specularN * light.specularN` before submitting a PICA
`LightSrc` record. The host read only `+0xA4` and `+0xA8`; **emission and both speculars were
unread**, so it had no specular term anywhere and `fragSecondary` could only ever be black. They
are now parsed (`cmb_color_block.cpp`), which is transport, not a port.

**The magnitude is not small, and it is now measurable.** Combined with the reduction above, PICA's
FRAGMENT_SECONDARY for MM3D's captured slots is `Σ(specular_0 + specular_1)` = **(1.082, 0.894,
0.780)**, which **clamps to essentially white**. The host adds black. So for the 691 MM3D and 69
OoT3D materials that consume FRAGMENT_SECONDARY, the host is not slightly dark on that term, it is
substituting black for a near-white constant. This is the largest single known error in the arc and
it is now bounded by a number rather than a suspicion — but it is **not yet fixed**, because the
other half of the product is still missing (below).

**LIVE CONFIRMATION on MM3D hardware, and a CORRECTION to the reading above (2026-09-29).**
The oracle is now runnable (`source .env`), MM3D's Lost Woods state regenerates from the ROM
(`tools/mm3d_oracle_state.py drive`: draws 3390, fog (40,140,220), LUT min 0.4915 — all three match
the recorded values exactly), and `tools/mm3d_lit_specular.py` captured a real fragment-lit draw
(draw 89 of 113, `picaLit=1` on 85). On that draw the reduction is **confirmed against live
registers**: `config0=0x80000400`, `config1=0xff7fffff` → **zero active terms**, exactly as predicted.
`max_light_index=1`, `light_enable=0x00000010`, `slot_mapping=[0,1,0,...]`, as recorded.

**The single-draw `specular0 == diffuse` reading was a coincidence, and the control caught it.**
One captured draw showed `specular0` bit-identical to `diffuse` on both its slots, which would have
been a free simplification. Over **8 captured draws / 16 non-zero slots it holds on 2 (12.5%)**, so
the light specular is **independent** of the diffuse and must be transported as its own quantity. A
two-sample reading could not have distinguished that, which is the whole reason the spread was run.

**AND THE CORRECTION ABOVE IS ITSELF WRONG -- withdrawn, because I read a Ghidra register name as
a single object.** The two sections before this one first recorded that the products never read the
material, and that `iVar6 + 0xac` is read as both a byte and a float. **Both halves of that are
refuted, and the original `material.specularN * light.specularN` reading is CORRECT.** `iVar6` is
assigned TWICE in `003fa5d0.c`: `iVar6 = *param_2` (the material colour block) before the loop, and
`iVar6 = *(int *)(param_1 + 0x10) + iVar10 * 0x60` (the light record) inside it. So line 69's
`*(byte *)(iVar6 + 0xac)` is the **material** and line 173's `*(float *)(iVar6 + 0xac)` is the
**light record** -- two different objects that happen to share an offset and a Ghidra register
name. The products at lines 165-186 are exactly `light.spec0 * material.spec0 * fVar1`, with
`fVar14/15/16` already computed from the material bytes at `+0xAC/+0xAD/+0xAE` before the loop.

**This is the second time in one session that I published a refutation from a plausible reading
without checking the pointer it was read through**, and the first was retracted the same way. The
generalisation is now explicit: a Ghidra decomp reuses registers across scopes, so an offset read
means nothing until the BASE POINTER at that point in the function has been identified.

**The light record's real layout, now recovered (0x60 stride, all reads are floats):**

| offset | field |
| --- | --- |
| `+0x88/0x8c/0x90` | diffuse |
| `+0x98/0x9c/0xa0` | ambient |
| `+0xa8/0xac/0xb0` | **specular 0** |
| `+0xb8/0xbc/0xc0` | **specular 1** |
| `+0xd8/0xdc/0xe0` | direction (negated) |
| `+0xe4` | enable (`== 1.0f`) |

`0x94/0xa4/0xb4` are unread, and the material triples are RGBA8 at 4-byte stride with a pad byte
(`0xa7`, `0xaf`). This is what the 2-of-16 live control is consistent with: the light specular is a
**separate** field at `+0xa8`, which is why it does not track the diffuse at `+0x88`.

**The light side is the remaining input, and it is NOT decompiled.** `FUN_004093f8` (a 48-byte
wrapper, not in the decompiled set) submits the per-slot records to `FUN_0040d1a8`, which serialises
them into the PICA light block at `0x140 + slot*0x10`. So the reduced form now has: per-slot
diffuse (host has it, `uLitDif1/2`), per-slot ambient (host has it, folded into `uAmbient`), the
per-fragment normal (host has `vNrmView`), and **material specular (now parsed)**, but still no
per-slot **light** specular. That is a single named function to decompile, and it is the whole
remainder of the transport.

**There is no N64/F3DEX analogue to cross-check the specular against, and that is worth knowing
before someone looks for one.** The F3DEX2 RSP here is *interpreted*
(`Shipwright/libultraship/src/fast/interpreter_rsp.cpp:232-288`, inside `Interpreter::GfxSpVertex`),
it runs **per vertex**, and its lighting is diffuse-only: `intensity = dot(n, coeff)/127`, then
`r = ambient + intensity * light.col`, clamped. No `N·H`, no half-vector, no view vector, no
specular anywhere. `grep -rni specular` over `libultraship/src/fast/` and `cmb3d/` returns **zero**
hits, and the only mentions in `soh/src/zelda3d/` are comments recording that the term is unused.
So a flat additive specular is PICA-specific, it cannot be validated against the N64 path, and the
vertex-lit CmbVShader path will never grow one on its own.

**Neither recovered table can supply the light specular, and that is now a TRUE NEGATIVE rather
than an absence of looking.** `Zelda3dLightSlot`
(`Shipwright/zelda3d_shared/lighting/zelda3d_env_record.h`, the struct both `.inc`s expand into) is
`amb[3] l0dir[3] l0col[3] l1dir[3] l1col[3] fogCol[3] fogNear fogFar zFar` -- pure
`EnvLightSettings`. **There is no specular, highlight, shininess or coefficient field in either
game's table.** The first version of the search reported `matched 0` and that zero was meaningless:
it unpacked the 10-bit-per-channel PICA `LightColor` and compared it unscaled against 8-bit table
bytes, so it searched a range the table cannot contain and a true positive was undetectable. It
now bridges with `round(v10 * 255 / 1023)`, matches only the four COLOUR fields of each record
(a whole-file regex was also matching direction vectors and the fog-distance triple), and proves
itself: it round-trips a real table colour through the 10-bit encoding on both tables and both
PASS. **1248 colour triples over 784 OoT3D records and 1545 over 3018 MM3D records, matched 0.**

**So the remaining work is new RE on the 3DS env-light source, and it is now scoped to one
structure**: the 0x60-stride light record's `+0xa8/0xac/0xb0` (specular 0) and `+0xb8/0xbc/0xc0`
(specular 1), which the recovered `EnvLightSettings` tables demonstrably do not populate. Note this
is a DIFFERENT record from the scene env table: the light array is per-draw runtime state, and its
producer is the open question, not the scene palette. Second required change, named so it is not
discovered late: **`Zelda3D_GL_SetLightParams` has no specular parameter at all**
(`Shipwright/soh/src/zelda3d/render/scene_lighting_submission.cpp:92`), so plumbing the term
requires changing that signature and the UBO with it.

**A trap this measurement walked into, recorded so it is not walked into again.** `light_enable` is
**not a bitmask of enabled slots** — Azahar reads it as a per-slot *light index*
(`pica_core.cpp:100`, `regs.light_enable.GetNum(slot)`), and the capture's `slot_mapping` array is that
index per slot. A slot count derived by popcounting the word, or by treating `0x10` as "slot 4 is on",
produces a confident wrong answer. `max_light_index` and `slot_mapping` are the authority. A second
trap: `lighting_capture` only *arms* the request and the PICA hook fills the file when that draw is
actually submitted, so arming and reading immediately yields a 0-byte file that looks like a silent
failure.

The instrument that made the OoT3D reasons measurable is `fragLit=` on the per-draw `[Zelda3D_SG] draw N`
list, added this turn: the corpus says which materials carry the flag, and the draw list says which of
them were actually drawn, so "no fragment-lit draw" is now a per-frame number instead of an inference
from a register. What would still open the OoT3D half is a state that already has a menu — the
equipment screen is worth 30 of 205 materials in one reachable place.

**Majora's Mask's scene lighting was NOT missing, and MM3D now has an oracle. Two of this project's
own negatives were wrong; both are now closed.**

*Wrong negative 1 — "MM3D's ZSI does not carry OoT3D's `EnvLightSettings` region."* It carries it, in
the same place: command `0x0F` in the scene-header ZSI, which is `SCENE_CMD_ID_ENV_LIGHT_SETTINGS` in
both games (`2ship/include/z64scene.h`). The reason the search found nothing is that **182 of MM3D's 424
scene ZSIs are LzS-compressed and were parsed as plain bytes.** Inflating them first takes the hit
count from 5 of 424 to **110 of 424**. The control that condemns the old measurement: parsing those 182
compressed files as plain yields **256 distinct "ctypes"** spanning the whole 0x00–0xFF range, 234 of
them outside OoT3D's 22-value command set — uniform noise across the entire byte space, which is the
signature of data, not a command stream.

*Wrong negative 2 — "the record layout is a different format."* The layout is **different and now
measured**: base `ptr + 0x28`, stride **0x20**, colour block at **+0x0B** (OoT3D: `ptr + 0x10`, stride
`0x1C`, block at `+0x0A`). The discrimination is two-sided, so it is not a tuned threshold: at the MM
layout OoT3D scores **0 of 95**; at the OoT layout MM scores **0 of 110**; at each game's correct
layout, **110/110** and **95/95** clear the plausibility filter.

*And the field map is confirmed by an authority that is not mine.* MM's own N64 `EnvLightSettings`
(`2ship/include/z64environment.h`) is 0x16 bytes with `ambient@0x00, light1Dir@0x03, light1Color@0x06,
light2Dir@0x09, light2Color@0x0C, fogColor@0x0F, blendRateAndFogNear@0x12`. The recovered 3DS record
puts those six colour triples at `+0x0B, +0x0E, +0x11, +0x14, +0x17, +0x1A` — the **same internal
spacing, uniformly shifted by +0x0B**, behind `[f32 zFar][f32 fogFar][u16 fogNear | blendRate<<10]`.
So the 3DS record is the N64 struct prefixed by the two distances N64 keeps as `s16` and the packed
blend/fog-near, and the byte at `+0x1D` the field scan could not name is MM's
`blendRateAndFogNear`/`zFar` tail. `fogColor` is at **`+0x1A`**, and an OoT3D-derived consumer hard-coded
to `+0x0A` would read MM3D's `fogColor` as its second light colour.

**MM3D also has an oracle now.** The Azahar harness is game-agnostic: it takes a ROM path
(`tools/soh3d_harness/main.cpp:117-134`) and hands it to `retro_load_game` with no identity check, and all
18 patches in `AZAHAR_PATCH.md` are observation hooks — no OoT3D addresses, no HLE overrides. MM3D's
image was refused for one reason only: its content is decrypted but its NCCH `flags` byte has
`no_crypto` clear, and `ncch_container.cpp:281` rejects encrypted NCCH. `tools/ctr_oracle_rom.py` sets
that bit **in a copy** — one byte, at a derived offset, refusing any image whose partition 0 has no NCCH
header. MM3D then boots and renders, and its PICA fog register is **scene-driven**: `(0,0,0)` to frame
1600, **`(187,110,110)` from 1800**, **`(90,110,0)` from 3600**. Those frame-level readings are NOT the whole fog state, and reading only them produced a wrong conclusion recorded in three authorities. MM3D's PER-DRAW log -- the field the host's `fogEnabled` models -- shows PICA fog mode 5 on **75-84% of the opening's draws** (135/161 at frame 2000, 98/131 at 3800), and the per-draw colour matches `az_fog` exactly including the `(187,110,110)` -> `(90,110,0)` transition between frames 3200 and 3800. So **MM3D's fogged-frame counterfactual EXISTS in its opening with no gameplay state**, and the colour join between the two registers is closed -- the "still open" claim is withdrawn. The apparent mode=0-vs-mode-5 discrepancy is also **resolved: there is only ONE register.** `soh3d_fog_dump` (`pica_core.cpp:994`) and the per-draw `fog=` field (`pica_core.cpp:414-421`) read the SAME `regs.internal.texturing` triple. It is one register sampled at two times: `az_fog` is a snapshot taken between draws, while the per-draw field is read *inside* `WriteInternalReg` at the `trigger_draw` hunk and so reports what the draw actually used. An `az_fog` reading of `mode=0` is therefore **not** evidence that fog is off -- which is exactly how the wrong conclusion at the top of this section was reached. Still open: which scene's record the opening's colours come from, since the opening is not a scene ZSI.
The layout is confirmed by MM's own struct, which is a stronger check than a colour match
would have been.

**So MM's fog moves from "missing" to "data recovered and independently validated; submission and a
fogged-frame counterfactual outstanding."** That is the opposite of what this file said a few hours
ago, and the specific reason it was wrong — compressed bytes parsed as plain — is the same class of
error this project has now hit twice in this campaign, so it is worth naming as a standing trap.

**CORRECTED, and it is my own statement from a few hours ago that was wrong: MM3D's opening IS fogged,
and its fogged-frame counterfactual exists without gameplay.** I recorded "the opening is `mode=0`
throughout frames 600–4000, so the fogged-frame counterfactual is out of reach", and that conclusion
came from reading **one** field — the frame-level `az_fog` register. MM3D's per-draw log says otherwise,
and the per-draw field is the one the host's `fogEnabled` boolean actually models (it is the same field
OoT3D's own reading used):

| frame | draws | per-draw `fog=5` | per-draw `fog=0` | per-draw fog colour |
| --- | --- | --- | --- | --- |
| 1200 | 92 | 51 | 41 | `(0,0,0)` |
| 2000 | 161 | **135** | 26 | `(187,110,110)` |
| 2600 | 144 | 118 | 26 | `(187,110,110)` |
| 3200 | 139 | 112 | 27 | `(187,110,110)` |
| 3800 | 131 | 98 | 33 | `(90,110,0)` |

So PICA fog mode 5 is active on **75–84% of the opening's draws**, and "not this material" is a real
per-draw mode 0 — the same shape OoT3D's title shows (59 of 102 at mode 5, 43 at mode 0). The
counterfactual I called blocked is therefore **available in MM3D's opening today**.

**The fog-colour join is also closed, and it is the join I said was open.** I could not match the
oracle's live fog colour `(187,110,110)` to the recovered table and concluded the sampled frame had fog
off. Both halves were wrong: the per-draw colour and the frame-level `az_fog` register **agree exactly**,
including the transition — `(187,110,110)` through frame 3200, `(90,110,0)` at 3800, and `az_fog`
switched between the same two frames (3600). Two independent fields, one transition, same values. The
remaining open question is only *which* scene's record the opening's colours come from, since the
opening is not a scene ZSI at all.

**The apparent register discrepancy is resolved: there is only ONE register.** `soh3d_fog_dump` (`pica_core.cpp:994`) and the per-draw `fog=` field (`pica_core.cpp:414-421`) both read the same triple -- `regs.internal.texturing.{fog_mode, fog_flip, fog_color}` -- so "the frame-level register says mode=0 and the per-draw field says mode=5" is not two registers disagreeing, it is **one register sampled at two different times**. `az_fog` is a snapshot taken whenever the command is issued, which lands between draws, when the game has just cleared the texturing fog state; the per-draw field is read *inside* `WriteInternalReg` at the `trigger_draw` hunk, so it reports what the draw actually used. The per-draw field is therefore the authority for every fog question, and an `az_fog` reading of `mode=0` is **not** evidence that fog is off. This is the same trap this project already recorded for the host side -- read `az_fog` after a frame, never before -- arriving from the other direction.

**MM3D's PICA raster state is now measurable from live registers, and the answer has a sharp edge.**
Over **667 draws across frames 1200–3800**, MM3D's opening uses **exactly one active TEV stage on every
draw (667/667); zero draws use more than one.** Stage-0 operations are op 1 (526), op 0 (66), op 8 (58),
op 2 (12), op 9 (5) — five distinct ops, 27 distinct sources — and the per-frame spread is stable, so no
one atypical frame carries it. Texture units, by contrast, genuinely are multi-unit: 538 draws bind one
unit, **91 bind two and 27 bind three**, across 8 distinct texture0 formats.

The sharp edge: **this is the OPENING, not gameplay**, so it bounds what is reached rather than
measuring it, and it means **a host validated only against MM3D's opening would never once execute TEV
stage 1 or later.** It is evidence that the single-stage path is reached, and explicitly *not* evidence
that the multi-stage path is right — that evidence remains the corpus survey plus Zora water's 3-stage
chain checked against live OoT3D registers. Recorded here so nobody later reads a green MM3D comparison
as multi-stage coverage.

**MM3D's opening has a small orthographic layer, and NO screen-space sky.** Classifying all 667 draws
by projection gives **656 perspective and 11 orthographic (2 per frame, 3 in the first)**, and all 11
orthographic draws are unlit — the same signature OoT3D's `cmb_shader_mode_correlation.py` used to
isolate the 2D overlay layer. The consequence for `sky/environment rendering`: **MM3D's sky is
perspective geometry, not a full-screen quad**, so a host that renders sky as a screen-space quad would
differ structurally from what MM3D does, and no per-pixel gate on the opening would catch it.

That classification took two corrections, both the same failure shape, and both are recorded because
the first answer in each case was a confident, plausible, wrong histogram:

* **Reading `proj0` for orthographicity is wrong.** The common `proj0` is `(0, 2.4142, 0, 0)` — a
  column of a scale matrix whose z element is 0 — while the same draws carry
  `proj2 = (0, 0, 1.0003, 5.0016)`, whose w term of `5.0016` is a perspective divide. Testing `proj0`
  classified **657 of 667 draws as orthographic**, i.e. "98.5% of MM3D's opening is screen-space
  quads" — which reads as a finding and is close to the exact opposite of the truth. The test belongs
  on `proj2`'s w term.
* **`dif0 == 0` is not a litness signal.** Most MM3D draws carry `dif0=(0,0,0,0)` while *also*
  reporting `picaLit=1` — 135 of 161 draws at frame 2000 — so testing `dif0` called **656 of 667 draws
  unlit** and inverted the fragment-lighting picture this same session measured. `picaLit`, which reads
  `regs.lighting.disable`, is the authority. The corrected split is 559 perspective+lit, 97
  perspective+unlit, 11 orthographic+unlit.

The general lesson is the one this project keeps re-learning and it is now cost three findings this
session: a wrong element in a tuple does not raise, it produces a clean-looking histogram. Both wrong
answers above would have been committed as evidence.

**But MM3D's opening is NOT a usable fog parity surface, and the counterfactual must be qualified
accordingly.** Reading all 128 `az_fog` LUT entries rather than the per-draw log's four samples:

| frame | entries below 0.999 | first | value at 96 | value at 127 | non-zero slope spans |
| --- | --- | --- | --- | --- | --- |
| 2600 | 2 of 128 | 126 | 1.0000 | 0.9844 | 124..126 |
| 3200 | 2 of 128 | 126 | 1.0000 | 0.9844 | 124..126 |
| 3800 | 14 of 128 | 114 | 1.0000 | 0.8232 | 112..126 |

MM3D's opening fog LUT is **1.0000 flat through entry 125**, with a total attenuation of 1.6% at the far
plane in two of the three frames. So the registers are real and the per-draw colour joins the
frame-level register — the counterfactual genuinely exists — but the **curve it produces has almost no
dynamic range**, and that has a hard consequence: a host with the wrong fog window, or with PICA fog
disabled outright, would still match MM3D's opening. **Any MM3D fog-parity number taken from the opening
is therefore vacuous**, and this file will not record one. A discriminating comparison needs a scene
whose recovered palette has a meaningful window, which needs a gameplay state.

The same reading shows `depthScale=-1` on all three frames — `viewport_depth_range` unset in the
between-draw snapshot, the same "sample it between draws and it tells you about nothing" trap as
`az_fog`'s `mode=0`.

**MM3D IS DRIVABLE PAST ITS OPENING, and it reaches real 3D game content — no gameplay state needed.**
This is the largest change to what MM3D can be measured on, and it was found by pressing confirm
rather than by more analysis. The harness's `input <mask>` writes the real libretro joypad mask
(`SetInputMask`, routed through the fork's `RETRO_DEVICE_ID_JOYPAD` callback), and MM3D's opening
responds to it. The sequence, each step confirmed by a captured frame rather than a draw count:

1. `run 900`, then press confirm. The opening cutscene plays (its PICA fog palette steps
   `(0,0,0)` -> `(187,110,110)` -> `(90,110,0)` as scenes change, at 6,600-11,500 draws per 150
   frames, up to 63,451 distinct colours in a frame).
2. Pressing confirm pages through a **story crawl** — static white text on black, 96.6% black and
   exactly 15 distinct colours, byte-identical across two consecutive 60-frame windows, one confirm
   per page. The text is the shared Ocarina of Time prologue: *"In the land of Hyrule, there echoes a
   legend. A legend held dearly by the Royal Family that tells of a boy..."* (the crawl also runs
   against OoT3D's font, which is the `textures/font` material both games use).
3. After the last crawl page, a long **unpressed** stretch (900 frames) is required: the exit is an
   animated dark-blue transition that does not advance on its own. Pressing into it skips a state
   that was never observed.
4. The landing state is **full 3D MM3D game content** — the opening's Lost Woods sequence, Link riding
   Epona between trunks with foliage, fogged in a blue haze under PICA fog palette **`(40,140,220)`**.
   It animates continuously and does not settle in 8 x 120-frame windows.

**The fog qualification from earlier is therefore LIFTED, and that is the practical consequence.**
The opening's 1.6% attenuation was the reason no MM3D fog number could be taken; further into the same
sequence the reachable frames measure **50.8% total attenuation at the far plane** — every one of the
128 LUT entries below 0.999, the first at entry **0**, a non-zero slope on all 128, from 0.997 at the
near end to **0.4915** at the far end. So a host with the wrong window, or with PICA fog off, would
**not** match these frames, and **MM3D fog is now a comparable family.**

The curve's SHAPE also differs from OoT3D's measured gameplay case, which matters for the port. OoT3D's
Zora window (`fogNear=800` of `zFar=12000`) gave a **flat 1.0 through entry 125** and 0.979 at 127 —
the host's `fog3dNode`'s flat-then-ramp shape. MM3D here ramps from the **very first** LUT entry with
no flat leading region at all. Both are the same function with a different window, which is the useful
result: **the host's mechanism is parameterised correctly and the window is what must come from the
scene**, so a hard-coded window would be wrong for MM3D and right for OoT3D. That is an argument for
carrying the recovered per-scene window and against any constant, and it is now measured on both
titles rather than reasoned.

Also measured on the same 6780-draw frame: PICA fragment lighting ON on **5100/6780 (75.2%)**, per-draw
fog mode 5 on **4380/6780 (64.6%)** (the remaining 2400 are mode 0), and still exactly **one** active
TEV stage on every draw.

**Two honesty limits on the above, both recorded rather than smoothed over.** The frame at the moment
those numbers were taken is the **dark-blue transition wipe into** the Lost Woods, not the steady
forest — the forest frame (Link on Epona, confirmed by image) was reached in an earlier run at a
different point in the sequence, and a later run with 1400 extra frames was still inside the wipe.
And the reason is a real limitation: **the cutscene drive is sensitive to exact frame counts** and is
not reproducible frame-for-frame, so "frame N of the Lost Woods" is not yet a stable address the way
OoT3D's cached gameplay savestate is. The fog figures belong to the Lost Woods *sequence*, and calling
them the forest's steady state would be asserting something the runs do not show.

What this opens, in the objective's own terms: MM3D now has reachable frames that exercise the
families the campaign cares about — scene fog with a real palette, a skinned character model, alpha
foliage, multiple texture units, and depth — **without a gameplay `PlayState`**. It does *not* open
the gameplay route: this is a cutscene, so the high-level `playstate`/`scene`/`actors`/`warp` commands
still need MM3D's own recovered addresses, and `docs/issues/0023` still applies to reaching a gameplay
state. But "MM has zero visual evidence" and "MM has no oracle capture" are both now false, and
`render.mm-scene-coverage` has a reachable surface.

**The fog-colour join is REFUTED, and the reason is now a PROOF rather than a suspicion: MM's blend
rule is ADDITIVE, so its result is not a convex combination of the table's colours at any weight.**
MM blends `list[slot] + spA4[...]` for four sources, then time-LERPs the two pairs, then config-LERPs
(`2ship/src/code/z_kankyo.c:1394`), where `spA4` is `adjLightSettings` — the *difference* between
adjacent light settings. Adding a difference moves the result **outside** the convex hull of the raw
slot values, so no interpolation over the table can reproduce it, and the correct instrument is not a
blend at all.

That was tested rather than argued. With the correct `fogCol` values read from the right capture
groups — `(90,133,180) (28,20,0) (0,0,30) (5,55,75) (130,180,180) (28,20,0) (0,0,30) (114,115,101)` —
the live `(40,140,220)` is **not inside the convex hull of any 4 of them**, and the control says the
miss is meaningful: only **1 of 60 random colours** is reachable in that hull, so this is a sparse
space rather than one where everything matches. (The first run of that test read the wrong capture
groups and got `(amb[0], amb[1], fogCol[0])` — a plausible triple from the wrong field. Same shape as
the `proj0`/`proj2` and `dif0`/`picaLit` mistakes earlier this session.)

**The window and the colour now have different verdicts, and keeping them apart is the point:**

| recovered field | verdict on live MM3D hardware |
| --- | --- |
| `fogNear`/`fogFar`/`zFar` (slot 2) | **PREDICTS the authored LUT to 1.19 byte steps** |
| `fogCol` (all 8 slots) | **not reachable by any interpolation** — MM's rule is additive |

So the fog *curve* is implementable now, from the recovered table and the existing host mechanism. The
fog *colour* is not derivable from the table at all and requires MM's runtime `adjLightSettings` —
which is the submission. Those are separable: the host's `Zelda3D_Fog3dSet` takes the window, and the
colour is a separate field, so the window half of MM's fog can land without the colour half and
**must not be reported as MM fog parity** until the colour joins too.

 The Lost Woods frame's `(40,140,220)` is not a table value (495 distinct `fogCol` values
across 1509 slots, 0 matches), and it is not a convex combination of **any pair** of them either, at a
2-per-channel tolerance. The control says the test could have detected a hit: **35% of random colours
are also reachable from some pair**, so the colour space is dense enough that a match would mean
something, and the absence of one is informative rather than merely inconclusive.

The cause is the term this file already records as the reason MM's submission cannot be a copy of
SoH's: MM blends `list[slot] + spA4[...]` for **four** sources, then time-LERPs the two pairs, then
config-LERPs. So a two-slot convex blend is the wrong *model* for MM, not merely the wrong window —
the additive `adjLightSettings` offsets are runtime-computed differences between adjacent light
settings and are not table values at all. A second cause cannot be separated with what is measured
here: an opening **cutscene** may use a cutscene-specific palette rather than the scene's env palette.
Both are recorded; neither is guessed away.

**MM3D NOW HAS A REPRODUCIBLE, PIXEL-EXACT ORACLE STATE — this is the session's largest MM3D unblock.**
Everything above was addressable only as "the state after this exact frame count from a cold boot", which
is an anecdote rather than evidence. `tools/mm3d_oracle_state.py` fixes that: `drive` recreates the
state, `verify` proves the round trip, `info` prints what is recorded.

* **The drive**: `run 900` -> 14 confirms at 150 frames apart (paging the crawl) -> **900 unpressed**
  (the exit transition does not advance on its own) -> 1400 settle. Those counts are load-bearing, not
  tuning, and changing them lands elsewhere in the cutscene.
* **The state**: `savestate` writes 23 MB to gitignored `scratch/harness/save/mm3d/lost_woods.state`.
  It is ROM-derived and is never committed; the tool regenerates it.
* **The round trip, across a process boundary**: save, tear the emulator down, boot a **fresh**
  process, load, and compare. It reproduces **byte-for-byte** — draw count 3390, PICA fog
  `(40,140,220)`, fog-LUT minimum **0.4915**, and identical frame SHA `4ac8d6cc2fc9d4b9`
  (mean-abs **0.0000** against this project's 0.73 instrument noise floor). A same-process load would
  have proved neither, since `LoadStateBuffer` rejects a foreign title anyway
  (`savestate.cpp:266`; OoT3D `0x0004000000033500` vs MM3D `0x0004000000125500`).
* **The frame is compared byte-for-byte deliberately.** Draw count, fog colour and fog-LUT minimum
  would ALL still match if a restore recovered PICA registers without recovering the image — which
  would let a bogus parity claim straight through. Byte comparison is the only version of this check
  that can fail.
* **A methodological error, caught and recorded**: the first round trip measured a mean-abs
  difference of **23.5** and looked like a failed restore. It was the experiment's own ordering —
  fingerprinting advances 60 frames, and the state was being saved *after* it, so the two frames were
  60 frames apart in the cutscene's animation. Saving before fingerprinting makes them identical. A
  "restore failed" conclusion from a 23.5 difference would have been confidently wrong.

So MM3D now has the same kind of stable measurement point OoT3D has had all along, reached **without**
a gameplay `PlayState`. `docs/issues/0023` still governs reaching gameplay, and the high-level harness
commands still need MM3D's own recovered addresses — but the cutscene-reachable 3D surface is now
stable enough to carry recorded oracle evidence.

**Is the shipped `fog3dNode` form right for MM3D? OPEN, and the control is what says so rather than
the fit.** The mechanism was recovered and validated on OoT3D — one `fog3dNode` on both host routes,
reproducing OoT3D's measured LUT node values byte-exactly at the Zora window. Whether that same
functional form is right for **MM3D** is a separate question, because MM3D is a different game with its
own material compiler. With a reproducible MM3D state the question became cheap to ask, and the
attempt to answer it is recorded because **the answer was "this test cannot decide", and the reason it
cannot is the useful part.**

* **Measured**: MM3D's 128-entry LUT runs 0.9971 at entry 0 to **0.4915** at entry 127, with a **knee in
  the last ~8 entries** — 0.979 at 104, 0.968 at 112, 0.934 at 120, then 0.4915 at 127. That is a
  visibly different shape from OoT3D's Zora case, which was flat 1.0 through entry 125 and 0.979 at
  127. **The knee is solid** and it is a port-relevant shape difference: a host whose fog was shaped only
  against OoT3D would mis-render MM3D's fog near the far plane.
* **The fit is noise, and the control proved it.** A 4-parameter search over
  `(zFar, scale, fogNear, fogFar)` scored mean-abs 0.01329 on MM3D's real LUT and **0.01669 on a
  SHUFFLED** one. Those do not separate, so the fit demonstrates nothing — a 4-parameter search against
  128 points will always find something. The fitted curve is also visibly wrong where it matters,
  predicting 0.981 at entry 120 against a measured 0.934 and missing the plunge entirely, and it landed
  on the coarse grid's edge (`zFar=1000`, `fogNear`/`fogFar` 60/63), so the search was under-resolved as
  well as under-controlled.
* **CORRECTION — the "REFUTED" verdict below was an ARTEFACT of a bug in the instrument, and the
  real answer is the opposite. MM3D's fog window from the recovered record DOES predict MM3D's authored
  curve.** The prediction is not a fit: the window comes from the table
  (`tools/gen_mm3d_scene_lighting.py`), and the form from the shipping code
  (`zelda3d_fog.cpp:23-24,35-38` + `unified_shader.cpp:371-376`), itself first checked against OoT3D's
  Zora window where it reproduces the recorded `d(127/128) = 834.2` (recorded 834), `LUT(127) = 0.9786`
  (recorded 0.979) and `LUT(125) = 1.0` flat. `tools/mm3d_fog_prediction.py`:

  | prediction | result |
  | --- | --- |
  | **A** `cameraNear = 7` (OoT3D's measured near plane), 8 slots | best mean-abs 0.01986 |
  | **B** `cameraNear` solved from `LUT(0)`, 8 slots | best mean-abs **0.00466** |
  | control, shuffled LUT | 0.02117 |

  **B separates from the control by 4.5x**, and the winner is **slot 2** — `fogNear=40`,
  `fogFar=12800`, `zFar=12800`, **`cameraNear=77.0`**. On the LUT's own 1/255 byte grid a mean-abs of
  0.00466 is **1.19 byte steps**. The predicted curve tracks the measured one (0.997 at entry 0 down to
  0.915 predicted against 0.934 measured at entry 120); the only real divergence is the final entry,
  where the game's cliff is steeper than the form predicts (predicted 0.5652, measured 0.4915, error
  0.0737 — 19 byte steps, at the one entry where the whole remaining drop happens).

  **So the fog port for MM3D is feasible with the existing host mechanism and the recovered table.**
  The earlier conclusion — that MM3D "needs its own curve recovered from `mm3d-decomp/`" — was wrong,
  and it was wrong because of the instrument, not because of the data. The bug: the validity check on
  a solved `cameraNear` was written as `cn < fogNear`, which is **backwards**. The form returns exactly
  1.0 whenever `d(0) = cameraNear < fogNear`, so "below fogNear" *is* the clamped case where the
  inversion is meaningless. With that check all 8 slots were rejected and the verdict came out
  REFUTED; with the correct condition — `fogNear <= cn <= fogFar`, plus refusing to invert at all when
  `LUT(0) >= 1.0` — the same data is CONFIRMED. **No MM3D data could have exposed this**, because
  either verdict looks equally plausible on it; only a synthetic target generated from a known window
  did, and that is now a standing self-test in the tool (`--selftest`), which also proves the
  instrument can say NO on an unproducible target.

  **What survives, and it is still the structural finding:** the fog table is *game-authored*
  (`pica_core.cpp:644-655` — uploaded, never recomputed). The host *computes* an equivalent curve from
  the window rather than storing the table, and this result says the two agree for MM3D to ~1 byte
  step. So the port stands, but on a **measured equivalence** rather than on an assumption that
  computing beats storing. The one place the equivalence visibly fails is the final LUT entry, which
  is worth recovering if a per-pixel gate ever complains.

  **And a new measured fact: MM3D's camera near plane is ~77, against OoT3D's measured 7.** That is a
  per-title constant the host must not assume is shared, and it is invisible in the scene record — it
  came out of the prediction, which is the only thing here that could see it.

* **A better instrument than a fit: the derivative.** The LUT dump prints each entry as
  `value/diff`, and `diff` is that entry's difference from its neighbour — the authored curve's
  **derivative**, sampled at the same 128 points. A derivative identifies a function family far better
  than the function does, and it involves no fitting, so there is nothing for a control to have to
  catch. MM3D's differences:

  | entry | value | diff | | entry | value | diff |
  | --- | --- | --- | --- | --- | --- | --- |
  | 0 | 0.9971 | -0.0005 | | 96 | 0.9844 | -0.0010 |
  | 48 | 0.9946 | -0.0005 | | 104 | 0.9790 | -0.0010 |
  | 80 | 0.9897 | -0.0005 | | 112 | 0.9678 | -0.0024 |
  | 88 | 0.9878 | -0.0005 | | 120 | 0.9345 | -0.0098 |
  | | | | | 127 | 0.4915 | **-0.4919** |

  So MM3D's curve is a **long shallow ramp of about -0.0005 per entry for ~120 entries, then a cliff:
  the last 7 entries carry a -0.4919 step, essentially the whole remaining drop.** Only **19 distinct
  difference values** occur across 128 entries, and the slope changes by more than 0.002 at just 7
  entries (first at 121). So the curve is *steepness-quantised* — the game computes it at coarse
  precision, not as a smooth per-entry evaluation.

  That is a genuine narrowing, and it cuts both ways. It is **consistent in character** with the host's
  `1/(zFar - t)` form, which produces exactly this flat-then-cliff shape when the projection's far plane
  sits at `zFar ≈ 1` in normalised units. But closing the parameters against the host's form
  **fails algebraically**: with `LUT(0) = 0.9971` and `LUT(127) = 0.4915` the two endpoint equations
  force `fogNear ≈ 1057 × fogFar`, which contradicts `fogNear < fogFar`. So the family is *not*
  identified, and the discrepancy is most likely the quantisation above rather than a different curve.

  **The RE target is now a signature rather than a topic**: a routine producing a monotone ramp of about
  -0.0005 per entry for ~120 of 128 steps, a cliff of about -0.49 in the final 7, and only ~19 distinct
  slope values. That is searchable in `mm3d-decomp/` by constants and loop bounds, and it is a better
  thing to look for than "the fog function".

* **What would decide it**: evaluate the form with MM3D's **actual** window and no fitting at all. The
  window (`fogNear`/`fogFar`/`zFar`) is in the scene record, and the Lost Woods *cutscene*'s palette is
  not in the recovered table — the colour join already failed for a recorded reason (MM's additive
  `adjLightSettings` term, plus a cutscene-specific palette). So the deciding input is MM3D's runtime
  env values for that state, which is the same missing input as the submission.

**The structural finding that reframes all of it: PICA's distance fog is a game-AUTHORED 128-entry
table, not hardware-computed.** `fog.lut[]` is filled only by register writes to
`texturing.fog_lut_data[0..7]` (`pica_core.cpp:644-655`), and `fog.lut_dirty` is only ever *set* — a
flag recording that the table changed — never used to recompute anything. So the emulator faithfully
stores what the game computed, and the 128 values are **authored data**.

That matters for the port because the host does the opposite: `unified_shader.cpp:519-520` *computes*
the table in the shader, `fog3dNode(i/128)` for i in 0..128, from a window
(`zFar`, `scale`, `fogNear`, `fogFar`). For OoT3D that is exact — the node reproduced OoT3D's measured
LUT byte-for-byte at the Zora window, which is a measurement that OoT3D's own game-side formula *is* the
eye-linear window. For MM3D it is an **approximation of an authored table**, and the knee MM3D's table
has is precisely where an approximation would show. So the honest statement of where this stands is:

* OoT3D fog: the host's computed curve is byte-identical to the game's authored table. Closed.
* MM3D fog: the host's computed curve is compared against an authored table of known different shape,
  and the comparison is **open** — not because the host is known wrong, but because a fitting test
  cannot decide it and the window is not yet known.

The longer-term correct answer is to reproduce the game's own LUT computation rather than approximate
it, which means recovering what MM3D computes. That is an RE step in `mm3d-decomp/`, and it is now
worth naming as one: the fog row's remaining work is not "tune the host's window", it is "recover the
curve MM3D authors". A host that keeps computing the curve from a window can only ever be right where
the game's formula coincides with that window, which OoT3D is and MM3D demonstrably is not.

So: **MM3D fog is comparable (the dynamic range is real) and the mechanism is unverified for MM3D.**
Both halves are recorded together, because "comparable" and "verified" are different claims and
conflating them is how a wrong port gets declared done.

**A census that was discarded, not reported.** MM3D's per-draw log carries a field named
`texMappingMethod`, and reading its first component as an integer yields a tidy-looking distribution
(`1`: 638, `0`: 20, `3`: 9 on unit 0) that reads exactly like the mapping-method population the
ProjectionMap row cares about. It is noise: the harness writes that field as **four consecutive f32
vertex-shader uniforms at index 92** (`Azahar/src/video_core/pica/pica_core.cpp:411`,
`log_v4("texMappingMethod", 92)`), not as a texmap register. Components come out as `0.640625`,
`0.3125` and `-2.51715e+16` — float bits, not enum values. The census was deleted rather than written
down, and the field's name is a trap: a plausible integer histogram from a float tuple.

**The PICA fog WINDOW is now fed, and the blend rule that produces it is shared rather than
duplicated.** MM's `z_kankyo` calls `Zelda3D_EnvBlendCapture` right after its own window blend —
`sp95`/`sp97` for the time pair, `sp94`/`sp96` for the config pair, with `temp_fv0` and `var_fs3` — and
`Environment_Update` calls `Mm3d_UpdateFogWindow(play)` immediately after `Environment_UpdateLights`, the
only point where that captured schedule is fresh. The window comes from
`Zelda3D_EnvBlendWindow(palette, ...)`, **the same function OoT3D's override now uses**, over MM3D's own
table. So the two-LERP-over-four-slots rule exists once, in
`Shipwright/zelda3d_shared/lighting/zelda3d_env_blend.{h,c}`, and the two games differ only in their
data and in their camera near plane.

Three things the structure check and the compiler caught, all of which would otherwise have shipped
silently:

* **A legacy decomp seam may not grow AT ALL.** Inlining the capture into
  `2ship/src/code/z_kankyo.c` took it from 3728 to 3762 lines and failed `verify_clang`'s structure
  check. The fix is the remedy the check quotes — "extract or compact the seam" — so the capture became
  a shared function and the seam gained one call; the remaining four functional lines (two includes,
  two calls) were offset by removing six blank lines, and the file is back at exactly 3728. The
  behaviour and its explanation live in the owner, not in the seam.
* **`PlayState` has no `sceneNum` in MM** — it is `sceneId` (`z64play.h:40`). The two games also spell
  the camera target differently: SoH's `View` has `lookAt` where MM's has `at`, both at offset 0x34.
  Two cross-game vocabulary differences that make a copied line fail to compile, which is the good
  outcome.
* **`gZelda3dEnvBlend` is now DEFINED in the shared owner**, and SoH's `zelda3d_lighting.c` no longer
  defines it. Moving a declaration without moving every *definition* is a link-time multiple
  definition, and it only appeared at the final `libsoh_core.so` link — after `soh_lib` had built
  clean twice.

The new shared rule is gated by 8 tests in `zelda3d_env_blend_tests.cpp`, where the refusal cases carry
as much weight as the arithmetic: no captured schedule, an index past the palette, a window whose far
is at or before its near, and a non-positive far plane all end with the fog OFF rather than a
plausible ramp. One test pins `z2_lost_woods` slot 2's window — the one the MM3D fog validation actually
used — so a change to the rule fails a test rather than quietly changing fog in the product. Two of the
eight were wrong on their first run and both are worth naming: the test palette held `vector::data()`
from a temporary that had already been destroyed, so the function read freed memory and returned 20241
and 3e24 instead of 25 and 125; and a second test used `fogFar = 0` as a placeholder and was correctly
REFUSED for a degenerate window. Both are the project's recurring shape — a plausible wrong number
instead of an error.

**MM's submission has ONE substitution point, and it is not in the zelda3d layer at all.** MM's own
N64 code already computes the whole blend, including the additive term, from a single list pointer:

    2ship/src/code/z_scene.c:372-376   Scene_CommandEnvLightSettings
        play->envCtx.numLightSettings  = cmd->lightSettingList.num;
        play->envCtx.lightSettingsList = Lib_SegmentedToVirtual(cmd->lightSettingList.segment);

    2ship/src/code/z_kankyo.c:1324     lightSettingsList = play->envCtx.lightSettingsList;
    2ship/src/code/z_kankyo.c:1361-62  func_800F6CEC(play, sp97, &spA4[0], lightSettingsList);
                                       func_800F6CEC(play, sp95, &spA4[1], lightSettingsList);

`func_800F6CEC` — which produces the `spA4` additive term — **takes the list as a parameter**, so it
derives from whatever list is installed. That means the MM3D port is a **data substitution, not a
reimplementation of MM's blend**: install the 3DS records as the list and the N64 blend, the additive
`adjLightSettings` term, the time LERP and the config LERP all operate on 3DS data for free. This is
also why the additive term is not the obstacle it looked like: it never has to be reproduced, only fed.

That is the right shape for this codebase for a second reason — it keeps the N64 blend in exactly one
place, which is the "one implementation of each rule" rule, and it leaves the N64 game logic untouched
except at a single scene-command handler that is *already* the scene-lighting entry point.

**And there is a stride trap that would make the substitution silently wrong.** `EnvLightSettings` is
**0x16** bytes (`2ship/include/z64environment.h:214`) and the N64 code indexes `lightSettingsList[i]`, so
it steps by 0x16. The 3DS record is **0x20** bytes. A naive pointer swap therefore walks a 0x20-stride
array in 0x16 steps, and **slot 0 looks right while every later slot reads the wrong bytes** — the exact
failure this campaign has produced repeatedly, in a new place. So the generated table must present the
N64 struct at **0x16 stride** (the colour block, the packed `blendRateAndFogNear` and the s16 `zFar`
filled from the 3DS record), with the 3DS-only `f32 zFar`/`f32 fogFar` carried in a parallel array for
the PICA window. The generator's current output is 0x20-stride and is correct *as data*; it needs a
0x16-stride projection before it can be installed as a list.

With that, the submission is: substitute the list and the count at `Scene_CommandEnvLightSettings`,
then feed the PICA window (`zFar`, `fogFar`, `fogNear`, and the **camera near plane ~77** that the
prediction recovered) to `Zelda3D_Fog3dSet`. The window half is validated against live hardware; the
colour comes out of the same substitution rather than needing separate work.

**The submission is NOT a copy of SoH's, and the reason is in MM's own N64 source.** SoH's
`Zelda3D_SceneLightSettingsOverride` re-runs one rule: LERP the 3DS palette's two time-slot indices by
`wTime`, LERP that pair by `wConfig`. MM's `z_kankyo.c` has the same *shape* — four source indices, a
time weight, a config weight — but each of the four source colours is **first added to a per-slot
`spA4[]` offset** before the time LERP:

    A = lerpColor(lightSettingsList[sp95] + spA4[1], lightSettingsList[sp97] + spA4[0], timeW)
    B = lerpColor(lightSettingsList[sp94] + spA4[3], lightSettingsList[sp96] + spA4[2], timeW)
    result = LERPIMP_ALT(A, B, configW)                        // 2ship/src/code/z_kankyo.c:1394

`spA4` is `adjLightSettings`, built as the *difference* between adjacent light settings. So the shared
`Zelda3dEnvBlend` shape (four indices, two weights) is right for both games, but MM needs **four
additive colour offsets carried alongside it**. Folding MM into SoH's rule without that term is not a
simplification — it is a second, silently-wrong blend policy, which is the same class of error as the
per-draw carriage bugs this campaign already found twice. The term is therefore named here as a
requirement of the shared contract rather than approximated away, and the struct is not extended
until an MM consumer exists to read it.

**And it is not a compression artifact, which was the obvious next excuse.** The two containers'
first bytes differ: OoT3D's ZSI opens `5a 53 49 01` ("ZSI\x01", content at 0) while MM3D's opens
`4c 7a 53 01` ("LzS\x01", payload at 0x10) — and `parse_env` starts its command walk at byte 16,
so on an MM3D file the first "command" it reads is type `0xff`. But **only 182 of MM3D's 424 scene
files carry an LzS header at all**, and `cmb_corpus`'s scene iterator finds a plain `"cmb "` marker
in them with `bytes.find`, which a compressed stream could not contain. So three readings were
measured, each against the same OoT3D control:

| reading | OoT3D (control) | MM3D |
|---|---|---|
| command walk from byte 16 of the file | **114 of 724**, real lighting | 5 of 424, `zFar=-2.49e+10` |
| skip the 16-byte LzS header first | 35 of 724, garbage — the skip is *wrong* | 5 of 424, garbage |
| the 242 files with **no** LzS header, no skip | — | **0 of 242** |

The middle row is the useful one: applying the same skip to the game where the region is known
*degrades* the control, which is what makes the third row decisive. MM3D's scene ZSI uses a different
scene-header format, not OoT3D's env command at another offset. The question for `mm3d-decomp` is
now sharp and answerable: **what replaces `SCENE_CMD 0x0F` in MM3D's scene header**, and where MM3D
stores the per-slot `zFar`/`fogNear`/`fogFar`/`ambient`/`fogColor` record.

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

**MM NOW BOOTS HEADLESS TO GAMEPLAY.** It did not, and the reason was not MM's logic. Two independent
defects stood between `tools/mm_game.py start` and a running game, and both are fixed at the cause:

1. **A static `std::thread` made every `exit()` from a game core abort the process.**
   `Shipwright/libultraship/src/ship/controller/scripted/ScriptedInputFifo.cpp` keeps the scripted-input
   poller's `std::thread` in a namespace-scope global, and its only join is `Ship_ScriptedInputFifo_Stop()`,
   which `Context::~Context` calls. MM's `OTRGlobals::RunExtract` calls `exit()` from its boot-time
   extraction prompt -- long before any `Context` exists -- so static destruction reached `~gThread` with the
   thread still joinable and `std::terminate` killed the process ("terminate called without an active
   exception", SIGABRT). The coredump names the chain exactly: `exit` <- `RunExtract` <- `InitOTR` <-
   `Zelda3D_CoreRun`, then `std::thread::~thread` <- `__cxa_finalize` <- `__do_global_dtors_aux` <- `_dl_call_fini`.
   The fix registers `std::atexit(&Ship_ScriptedInputFifo_Stop)` when the poller starts, and the ordering that
   makes it work is not incidental: `_dl_fini` enters the exit-handler list when the library loads, i.e. before
   `main`, so a handler registered during `main` runs EARLIER in the LIFO walk than the static destructor that
   would terminate. `Shipwright/libultraship/tests/scripted_input_fifo_lifetime_tests.cpp` is the red-to-green
   proof: a forked child starts the poller and leaves via `exit()`; without the fix it dies on signal 6, with it
   the child exits 0. The child is forked BEFORE the thread starts, because forking a process that already owns
   the thread would leave the child holding a handle to a thread that did not survive the fork.

2. **MM asked a different question about its own archive than the one that loads it.** The boot-time extraction
   prompt tested `GetAppDirectoryPath(appShortName) + "/mm.o2r"`, which only ever looks in the user config dir,
   while `OTRGlobals::Initialize` loads the archive through `Ship::Context::LocateFileAcrossAppDirs`, which also
   searches the core's asset dir and the cwd. So on any developer build tree -- which already holds a valid 36 MB
   `mm.o2r` -- the check answered "absent" while the loader would have found it, and MM re-ran the whole
   660-asset ZAPD extraction on EVERY launch. Under software rendering that never finished, so the manager's 40 s
   readiness wait expired and MM was killed before reaching gameplay. This was only reachable because
   `RomSelectionStore` resolves `MmN64` from `ZELDA3D_MM_ROM`, which `.env` sets, so `args` was non-empty and the
   prompt took the "a ROM was supplied" branch rather than the file-check branch. Both sites now use the shared
   locator (one rule, one owner), and `Z3DBoot`'s new `Z3D_ReextractSuppressed` gate answers the remaining "Extract
   again?" popup for headless runs, which have nobody to click it. It is opt-in because skipping extraction is the
   wrong default for someone who just pointed the port at a different ROM.

Diagnosing (2) needed `strace`, which is **not installed on this host** -- recorded rather than worked around. The
substitute that actually settled it was reading `/proc/<pid>/cwd` and `/proc/<pid>/fd` of the live process: cwd was
`/tmp/extractor-*` and the only open archive was `2ship.o2r`, which together proved `CallZapd` had run and `mm.o2r`
had never been opened. A diagnostic that reports only the *fed* value cannot distinguish a correct window from a
same-shaped wrong one, so `Mm3d_SceneFogSlot` was added as the independent table read and the `fog` REPL command
prints the residual across all three distances -- and, at a non-zero blend weight, reports that no slot matches
instead of implying agreement.

**AND THE SUBSTITUTION IS NOW VERIFIED AT RUNTIME, NOT JUST AT THE CODE LEVEL.** S005 previously recorded
that MM3D's records are "SUBMITTED" at `2ship/2s2h/z_scene_2SH.cpp:281`. That was a statement about the
code path. The `fogcolour` command now settles it by POINTER IDENTITY: it compares
`play->envCtx.lightSettingsList` against `Mm3d_SceneEnvList(sceneId)` and reports which list MM is
actually blending. In `z2_clocktower` it reports `list=SUBSTITUTED`, so MM really is blending the
recovered 3DS records. That check exists because the alternative reading -- that the window simply
matched because the 0x20 table is present -- would look identical from the window alone: the window is
read from `kMm3dSceneLighting` directly and never passes through the substituted 0x16 list.

**The fog COLOUR: the standing "not derivable from the table" claim is REFUTED, and an earlier
measurement of mine was wrong twice over.** Both corrections matter more than the number, so they are
recorded in full.

*Wrong #1 -- the "additive term" story.* The claim rested on MM's blend being ADDITIVE
(`list[slot] + spA4[...]`, `z_kankyo.c:1397-1454`), which puts the result outside the convex hull of the
table. Reading the source refutes the part that mattered: `spA4` is `__osMemset` to 0 at
`z_kankyo.c:1325` and `func_800F6CEC` writes it ONLY when the index is in [4,8) or it is raining
(`z_kankyo.c:1263,1279`). MM's live indices in `z2_clocktower` are `1,0,1,0` -- all below 4 -- so **the
additive term is identically zero this frame**. Any residual attributed to it was not that.

*Wrong #2 -- comparing across two producers.* MM's `Environment_UpdateLights` has THREE writers of
`envCtx->lightSettings` and they consume different things: the time-based branch blends four slots by
`skyboxTime` (`:1405-1454`), a plain branch COPIES `lightSettingsList[envCtx->lightSetting]` when
`!lightBlendEnabled` (`:1484-1492`), and a third LERPs `prevLightSetting` against `lightSetting`
(`:1520-1535`). A first version of the `fogcolour` diagnostic compared the live colour against a
two-LERP over the CAPTURED schedule's four indices and reported a 239/255 "residual". The frame was
produced by neither of those paths, and the two data paths are allowed to disagree: the fog WINDOW is
read from the 0x20 table through the captured schedule, while the COLOUR comes through the substituted
0x16 list and whichever branch ran. A plausible number from a cross-branch comparison is the exact
failure mode this project keeps hitting, and it happened here.

*What is actually true, measured.* The colour is the table's, and provably so: at one moment the live
colour was `(105,195,255)` and `z2_clocktower` slot 0's `fogCol` is `{105,195,255}` -- bit for bit. The
substitution is `list=SUBSTITUTED 3DS records`, 29 slots. So the colour is derivable from the table, and
the host does not need a new blend: it can read the colour MM already computed from the 3DS data.

*The drift is MM's own time-of-day blend, and tracking it down found a real defect in the WINDOW feed.*
The colour was not drifting for a mysterious reason: sampling `fogcolour` alongside the state that
governs it showed `gSaveContext.skyboxTime` advancing 16384 -> 16789 while every slot index stayed put,
and the values are arithmetically consistent with a two-LERP between `z2_clocktower` slots 0 and 1 at
w ~ 0.075 (slot 0 fogCol `(105,195,255)`, slot 1 `(110,22,16)`; red moves +5w, which rounds to 0, so
"red pinned" was the rounding, not a pin). So the colour is ordinary MM behaviour on 3DS data.

**But the same reading exposed that MM's PICA fog WINDOW was lerping each pair BACKWARDS.** The
capture was `Zelda3D_EnvBlendCapture(sp95, sp97, sp94, sp96, ...)`, while the shared rule is
`Lerp(Lerp(idx[0], idx[1], wTime), Lerp(idx[2], idx[3], wTime), wConfig)` -- so the weight-0 slot must be
passed FIRST. MM's own code puts it second: `Environment_LerpColor(to, from, w)` returns
`from + (to - from) * w` and `S16_LERP(a, b, w)` is `b + (a - b) * w`, so with
`LerpColor(list[sp95], list[sp97], w)` the weight-0 slot is `sp97`. OoT gets this right -- it captures
`(TIME_ENTRY_1F.unk_04, .unk_05)` directly above its own `LERP(list[unk_04], list[unk_05], sp8C)`.

Measured consequence in `z2_clocktower`, before the fix: the window reported `near=926` (slot 1) at
`wTime=0` while MM's own `envCtx->lightSettings.fogNear` was `456` (slot 0) -- a **470-unit error at
rest**, growing as `skyboxTime` advanced, because the whole pair was interpolated in the wrong
direction. Both orders are valid lerps, so the window still looked entirely ordinary: right shape,
plausible ramp, wrong end. Fixed to `Zelda3D_EnvBlendCapture(sp97, sp95, sp96, sp94, ...)` -- a
one-line change; `z_kankyo.c` is vendored decomp pinned at 3728 lines and `2ship/src/` is excluded from
the clang gate as generated decomp, so the ordering contract is documented in the shared header
(`zelda3d_env_blend.h`) instead of in a comment the seam cannot afford.

The `fog` command now proves the fix against the game's OWN arithmetic: it compares the fed window with
`envCtx->lightSettings.fogNear`, which MM computed for itself from the same list and weights earlier in
the same function. After the fix the two agree at every sampled weight (d = 0.00, 0.98, 0.15) against
**470 before**. The residual is MM's own quantisation -- it stores `fogNear` packed as
`blendRateAndFogNear & 0x3FF` and blends with the integer `LERPIMP_ALT`, while the 0x20 record keeps the
exact f32 -- so the tolerance is 1.0, and a tighter one would report that as a failure every frame.
`zFar` legitimately disagrees (`MM zFar=32767` vs `3DS zFar=40000`): the 0x16 projection clamps zFar to
s16, which is why 130 slots are recorded as clamped.

*This is the strongest check in the MM fog campaign precisely because it needs no oracle*: it compares
the port against the N64 game's own computation on the same frame, so it falsifies a wrong blend
without a pixel comparison.

**Warping the host then exposed a second, larger defect: in override scenes the window was built from a
STALE schedule.** MM could not previously be warped at all, so this was invisible until now. The capture
sits INSIDE MM's time-based branch, and that branch is guarded by
`lightMode == LIGHT_MODE_TIME && lightSettingOverride == LIGHT_SETTING_OVERRIDE_NONE`
(`z_kankyo.c:1339`). In any scene where the game sets a light-setting override -- or does not use
time-based lights -- the branch never runs, so `gZelda3dEnvBlend` silently kept its last value.

Measured in `z2_lost_woods`, where the override is set: the window read `near=229.5`, which is exactly
`lerp(160, 632, 0.147)` of the recovered slots -- i.e. exactly the stale capture -- while MM's own
`lightSettings` held slot 1 outright (`fogNear=632`, `fogColor=(28,20,0)`). **The two numbers disagreed
by 402 and both were internally consistent, which is exactly why nothing looked wrong.** A fog window
is four plausible numbers, so a stale one is indistinguishable from a live one by eye.

Fixed by `Mm3d_CaptureEnvBlendForNonTimePath`, called from `Environment_UpdateLights` after the
light-settings block and before the `lightBlendEnabled = true` that follows it (that line would erase the
distinction). It reproduces whichever rule actually ran, in the shared schedule's own vocabulary:
blending maps to `Lerp(Lerp(prev, light, w), Lerp(prev, light, w), 0)`, and the plain copy to
`Lerp(light, light, 0)`. `FULL_CONTROL` is left alone, because there something outside `z_kankyo` owns
`lightSettings` and the context says nothing about which slots produced it.

Verified across three scenes, each matching its recovered 3DS record with residual 0.00 AND agreeing with
MM's own blend: `z2_lost_woods` (override path, d=0.00, was 402), scene 99 (time-based, d=0.00), and
`z2_clocktower` (time-based, d <= 1.0, MM's own integer quantisation). The colour likewise: Lost Woods'
`live=(28,20,0)` reaches the renderer as `uFog=(0.110,0.078,0.000)` with the two-LERP predicting it to
within 0.

Also recorded, because it cost a crash and is not a defect: warping to `z2_lost_woods` via entrance
`0xC40B` crashes MM in `Actor_InitContext` after the room load fails (`Room: 127`). Spawns `0x00`/`0x03`
of the same scene load fine, so it is a bad spawn argument reaching a room the entrance cannot resolve,
not a lighting or warp-path fault.

**The host now renders MM's Lost Woods, and the window is verified in both of that scene's animated
states.** `warp 0xC400` plus `mm_game.py shot` produces real scene content -- hollow log, trunks,
mossy ground, ferns -- and the live window tracks MM's own water-lights override between two recovered
records, matching each **exactly** (residual 0.00) and agreeing with MM's own blend (d=0.00) in both:
`near=160.0` (slot 0) and `near=632.0` (slot 1), both `far=13200.0 zFar=16000.0`, colour slot 1's
`(28,20,0)` predicted to within 0. So the window fix holds across an animated transition, not just a
static frame.

**The coordinate-matched MM fog A/B is BLOCKED, and the reason is the skip policy rather than a
missing tool.** The oracle's recorded `z2_lost_woods` LUT is for slot 2 (`40, 12800, 12800`); the host
sits at slot 0 or 1 because MM's own scene actors own `lightSettingOverride` in that room. The `fogslot`
command drives MM's public `Environment_EnableUnderwaterLights` / `Environment_DisableUnderwaterLights`
-- the game's own entry points, not a field write -- and **both report `REFUSED`**, because each is
guarded on the override currently being `NONE` (`z_kankyo.c:1162`) and in this scene it never is. The
only way to reach slot 2 from here would be to write `envCtx.lightSettingOverride` directly, which is
exactly the "write a phase, timer, or scene pointer" the project's skip rule forbids. So the remaining
routes are to reach the state by playing (the oracle's own route) or to recover what triggers the
water-lights actor -- and until one of those happens, the transitive validation stands as it is: the
host's fed window equals MM's own blend, and the recovered record's window predicts MM3D's authored LUT
to 0.00466 against a 0.02117 control.

That `fogslot` reports `REFUSED` rather than `ok` matters more than it looks. Its first version printed
`ok` on a call that provably changed nothing, which is the failure mode this project keeps meeting: a
diagnostic that reports the call returned instead of what the call did, so a later measurement looks
like a failed fix when nothing was ever attempted. The 1-in-60 random-colour test is withdrawn as support for the
strong claim: it refuted interpolation of the table, which was never in dispute.

**WITHDRAWN: "MM3D's fog COLOUR is closed on the host side" was MY OVERREACH, and it is wrong.**
`Mm3d_UpdateFogWindow` does now feed `gZelda3dFogColor` from `envCtx->lightSettings.fogColor` -- the
value MM itself blended from the substituted records -- in the same pattern OoT's `Zelda3D_UpdateFog`
uses, and that feed is verified live (`live=(105,195,255)` in `z2_clocktower` reaches the renderer as
`uFog=(0.412,0.765,1.000)`; `live=(90,133,180)` in `z2_lost_woods` as `uFog=(0.353,0.522,0.706)`). What
that feed is NOT is MM3D's fog colour, and calling it "closed" asserted a parity that does not exist.

The reason is in the recovered record's own shape, and the project had already measured it: the 3DS
record's colour block is a **byte-for-byte copy of the N64 `EnvLightSettings`**, so `fogCol` is MM's N64
fog colour carried inside the 3DS file -- it is not MM3D's authored PICA fog colour. MM3D's live PICA
fog colour for the Lost Woods frame is `(40,140,220)`, and the recovered `z2_lost_woods` `fogCol` values
are `(90,133,180) (28,20,0) (0,0,30) (5,55,75) (130,180,180) (28,20,0) (0,0,30) (114,115,101)` -- the
project already showed `(40,140,220)` is not a table value and not a convex combination of any four of
them (only 1 of 60 random colours lands in such a hull).

**Measured this turn, host against the recorded oracle value in the same scene:**
`host (90,133,180)` vs `oracle (40,140,220)` -- per-channel `(50, 7, 40)`, **mean-abs 32.3 of 255**. So
the two disagree by a third of the range, and no blend of the recovered table can close it because the
quantity is not in the table. One more measured wrinkle: the host's colour is TRANSIENT in this scene,
because MM's water-lights override toggles -- two runs gave slot 1's `(28,20,0)` and slot 0's
`(90,133,180)`. A single-sample colour A/B would not even be stable.

**And the cheap route to recovering it is now closed, with evidence.** The obvious question -- is the
authored colour in the scene data we can already read? -- is answered **no**.
`tools/mm3d_fogcolour_hunt.py` finds `(40,140,220)` twice in `z2_lost_woods`'s inflated ZSI, `0x20`
apart, and the first version of that tool called it a real candidate. It was a coincidence: the hits
land at **+12008 and +12040 from a known env `fogCol`, both `8 (mod 0x20)`**, their containing data is
referenced nowhere in the file, its float fields are garbage, and the plausible-colour run around them
degenerates into noise after four entries. The never-reported-triple control came back clean and still
missed it, which is the lesson: a control is only as strong as the failure it can catch. Alignment
against a known field is the test that discriminates, and it is now pinned both ways in
`tools/test_mm3d_fogcolour_hunt.py` (6 tests, no ROM required).

So the honest state is: the feed is real plumbing and makes the divergence measurable, the 1-in-60
"not derivable" test stands for the colour exactly where it always did, and the colour is in neither
the env record nor the scene ZSI -- which makes the `mm3d-decomp` recovery mandatory rather than
optional. **MM3D's PICA fog colour is a
separately authored quantity that the recovered scene record does not contain**, and recovering it is a
`mm3d-decomp` RE step -- the PICA fog-colour producer, not more interpolation. The window is unaffected:
`fogNear`/`fogFar`/`zFar` are genuinely 3DS fields, and that half remains verified.

Gap: MM3D coverage is substantially incomplete and must be established independently from OoT results.
The fog WINDOW is observed, cross-checked against MM's own blend on three scenes and fed; the
scene-lighting substitution is verified by pointer identity. The fog COLOUR is **not** closed -- it is
present, fed, and measured to diverge from MM3D by mean-abs 32.3 of 255, because MM3D's PICA fog colour
is authored separately from the N64 `fogColor` the 3DS record carries. And **no MM frame has been
compared against the oracle**, so `lighting.pica-fog` stays open for MM until an A/B exists.

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
