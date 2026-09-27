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
and effect families whose parity is partial. The title-scoped oracle half of the host-vs-oracle
comparison now works, but the **host** half faults on the title wordmark's unified pipeline under the
software Vulkan driver this host falls back to, so no host title image exists to compare against (see
[issue #25](issues/0025-title-wordmark-unified-pipeline-crash.md)). That crash gates closure of the
`cmb-unlit-primary`, `cmb-lit-primary-alpha` and `cmb-fragment-lighting` families, whose remaining gap
is exactly "ported and close-tested on retail data, no oracle comparison made". Coordinator mapping methods are now recovered from the retail shader
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
What remains is confirming `uInvView`'s relationship to the view matrix on a draw whose model-view
rows are not identity, coordinator 1's per-vertex component step, and one gameplay-only measurement
for every method-4 material.

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

Gap: the current title-driving recipe still does not reach the current-contract gameplay PlayState or
produce a fresh gameplay checkpoint, so no new gameplay oracle observation is yet valid. The cold
title probe reaches the recovered transition request, but the file-select handoff has no save slots
and the required system-title app is absent from the harness save directory. The software renderer
remains too slow for this scenario on the current host. See [issue #23](issues/0023-embedded-oot3d-oracle-cannot-reach-its-boot-hand.md).
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
