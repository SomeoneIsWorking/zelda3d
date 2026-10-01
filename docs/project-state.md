# Project state

Current focus: S003 (renderer / material / lighting parity on both titles). The next grounded RE
dependency for it is in `docs/re-frontier.md` (`tools/re_frontier.py next`).

## Comparison baseline

The unmodified Nintendo 3DS releases of *Ocarina of Time 3D* and *Majora's Mask 3D*, running on
original hardware or through Azahar. User-visible deltas versus that baseline:

| ID | Delta | State | Evidence or gap |
| --- | --- | --- | --- |
| B1 | 3DS assets presented by a native PC engine instead of an emulator | verified | both cores run through `./run.sh`; `soh_core`/`mm_core` loaded by `zelda3d_app` |
| B2 | One chooser and one process run both games | verified | `--probe-cores` loads both cores and checks symbol isolation |
| B3 | First-run ROM selection with no terminal | verified | SDL setup owner + `RomSelectionStore` under OS config |
| B4 | 3DS PICA200 material/texture/sampler semantics | partial | texture formats and sampler filter/wrap verified for both titles |
| B5 | 3DS scene lighting and PICA fog | partial | OoT closed; MM fog WINDOW recovered and fed, fog COLOUR still the N64 one |
| B6 | 3DS actor animation, facial and camera behavior | partial | grounded replacements ship per actor; remainder falls back to N64 |
| B7 | Controls | verified | PC-native scheme with live HUD glyph rebinding (`docs/lus_input_architecture.md`) |
| B8 | Loading and transition screens | verified | none; every core loads straight into its own title/gameplay |
| B9 | Platforms | partial | Linux delivered; Windows/macOS seams only; Android missing (S011) |

## Capability inventory

| ID | Capability | State | One-line evidence or gap |
| --- | --- | --- | --- |
| S001 | One launcher provisions, validates, builds and chooses between the OoT and MM cores | verified | `./run.sh` builds both cores and opens the chooser with no player ROMs at build time |
| S002 | 3DS containers, models, animations, scenes, collision, cameras, lighting, face data reach both engines | partial | CMB/CSAB/ZAR/ZSI/CMAB/faceb readers live in `Shipwright/cmb3d/asset`; format coverage incomplete across both retail games |
| S003 | PC renderer reproduces the reached PICA200 material, texture, lighting, fog, transparency semantics | partial | texture formats and sampler filter/wrap verified both games; OoT fog closed, MM fog window fed; per-light fragment transport and `config0` bit 17 (`shadow_secondary`) still open |
| S004 | OoT3D actor animation, facial, camera and game-specific behavior replaces N64 behavior where grounded | partial | grounded replacements in `Shipwright/soh/src/zelda3d/behaviors`; unported seams fall back to N64 |
| S005 | MM3D actor animation, presentation and game-specific behavior replaces N64 behavior where grounded | partial | scene lighting substituted at `Scene_CommandEnvLightSettings`, fog window fed via the shared `Zelda3D_EnvBlendWindow`; fog colour producer still unfound (computed at runtime, absent from the MM3D image) |
| S006 | Embedded Azahar oracle and comparison tooling can compare the port with independent 3DS execution | partial | title oracle captures and caches work today; gameplay observation blocked on one missing savestate (issue 0023) |
| S007 | The AppImage accepts four direct ROMs or bounded ZIPs and persists validated choices without shipping game content | partial | container integrity validated; the final artifact and packaged first-run release gate are still open (issue 0024) |
| S008 | Linux CI builds the complete app and both cores and executes asset-free native contracts | partial | hosted job builds pinned SDL3, runs CTest and `--probe-cores`, then the Clang verifier; package install and real-title gameplay unverified |
| S009 | macOS arm64 CI | partial | seam job only; the launcher hardcodes ELF `.so` names and Linux `/proc/self/exe` |
| S010 | Windows x86_64 CI | partial | seam job only; the launcher uses `dlfcn.h`/`dlopen`/`realpath` unconditionally and MM applies GNU flags unguarded |
| S011 | Android delivery | missing | no Android target, package or runtime integration exists |
