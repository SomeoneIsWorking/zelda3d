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
| S006 | An embedded Azahar oracle and parity tooling can compare the port with independent 3DS execution | verified | S001 | G001, G002 |
| S007 | The AppImage accepts four direct ROMs or bounded ZIPs and persists validated choices without shipping game content | partial | S001 | G003 |

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
and effect families whose parity is partial.

### S004 — OoT3D behavior coverage

Grounded actor modules replace selected animation, face, camera, movement, and draw behavior in the OoT
engine.

Gap: complete actor and game-flow coverage is not reached; behavior remains a per-system RE frontier.

### S005 — MM3D behavior coverage

The MM core shares the 3DS asset/renderer layer and has title-specific animation and behavior adapters.

Gap: MM3D coverage is substantially incomplete and must be established independently from OoT results.

### S006 — Independent oracle comparisons

Evidence: the repository embeds Azahar, exposes state and rendering probes, records closed parity cases,
and carries positive/negative controls for its trusted comparison instruments. This verifies the harness,
not parity for unmeasured content.

### S007 — Packaged player setup

The documented AppImage first-run flow validates direct ROMs or one bounded nested ZIP per selection,
persists choices under user configuration, and excludes ROM-derived archives from the package.

Gap: the final artifact and first-run release gate remains open, so source-path behavior does not yet
verify the packaged release end to end.
