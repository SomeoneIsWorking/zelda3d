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
| S003 | The PC renderer reproduces the reached PICA200 material, texture, lighting, fog, and transparency semantics | partial | S002 | G001, G002 |
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
content is composed correctly (lockup width ~445px host vs ~415px oracle of 800). The glow hue is white where the oracle's
is orange, and it is **unattributed**: it was first blamed on the unified path's placeholder combiner,
but a generated-source test refutes that — `kDualTex`/`kDualTexFog` never call `evalCycle`, they take
a branch implementing the three byte-classified PICA dual-texture shapes on `uSheen.y`, so the
wordmark is on the same proven shape mechanism as the native path. Every 3DS draw on this frame is
therefore accounted for on a proven mechanism, which puts the delta in a stage shared by all of them;
the PICA distance fog the unified path documents as not applied, and the dawn-layer stack, are the
candidates and neither is claimed. The tool's `content` metric moved
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
MM3D has 2,283 of 2,968 (76.9%)**, plus 503 vs 69 for `FRAGMENT_SECONDARY`. The host maps those to the
vertex-lit primary and to black, so this is a rounding error on OoT3D and the **dominant lighting
mechanism of MM3D being essentially unimplemented** — which contradicts any reading of S003/S005 as
symmetric. **But the two populations are not the same shape, and the tool prints that on every run:**
OoT3D reaches 1,387 archive members plus 610 inline `.zsi` scene CMBs, while MM3D's iterator reaches
1,448 actor members and **zero scene files** (MM3D ships no `.zsi` equivalent under `/actors/`). So
76.9% is an **actor-material** figure and MM3D's scene/environment materials are unmeasured — extending
`iter_mm3d_cmbs` to MM3D's scene container is a named gap, and the same limit applies to the other
"MM3D's corpus" figures in the frontier. It is also cheap to fix and rankable: **1,069 MM3D materials (36% of the corpus) use only
`MODULATE[C(FRAG_PRIMARY),C(TEX0)]`-family chains**, so one correct `FRAG_PRIMARY` is worth more on the
MM side than every other open graphics row combined. The OoT3D figures reconcile exactly with the
existing row (202 consumers, `source_without_flag=5`, 202−5=197). Whether the OoT3D-recovered `+0` flag and `+0xA0..+0xB3`
colour block are even valid for MM3D was tested with a control against each argument, and they disagree:
the "authored colours are spiky" argument is **refuted by its own control** (the material record is mostly
constant, so other offsets hold a median of only 4–8 distinct values, and MM3D's `specular0` is more varied
than that), while the descriptor probe-word argument is **validated by its control** — the u32 `0x62C884C0` at material
`+0xDC` is the most common word at exactly one of 84 sampled offsets in both games (a composite
of two typed fields, not a named enum). So the `+0xCC` descriptor layout is
strongly corroborated and the colour block plausible by adjacency, but neither is the binary read, and
confirming them in `mm3d-decomp` is the named next RE step. The tool prints both verdicts on every run so
the disagreement stays visible. Getting there also required
fixing MM3D's silent-zero class in this very tool (the material chunk pointer at `0x28` is `qtrs` for
version ≥ 7, so the entire MM3D survey had been returning nothing), now routed through the single
layout owner and pinned by a test verified to fail on the old read.

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
