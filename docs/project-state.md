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
| S003 | The PC renderer reproduces the reached PICA200 material, texture, lighting, fog, and transparency semantics | partial (texture formats **and** sampler filtering/wrapping: **verified for both games**; material/lighting/fog still partial) | S002 | G001, G002 |
| S004 | OoT3D actor animation, facial, camera, and game-specific behavior replaces N64 behavior where grounded | partial | S002, S003 | G001 |
| S005 | MM3D actor animation, presentation, and game-specific behavior replaces N64 behavior where grounded | partial | S002, S003 | G002 |
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
