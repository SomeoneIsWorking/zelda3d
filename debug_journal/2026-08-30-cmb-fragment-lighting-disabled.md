# 2026-08-30 — CMB fragment-lighting disabled branch and corpus frontier

## Root cause

The generic TEV evaluator conflated PICA vertex `PRIMARY` with fixed-function
`FRAGMENT_PRIMARY`, and represented `FRAGMENT_SECONDARY` as opaque black. PICA keeps all three
sources distinct. Its fragment colors initialize to zero and are only replaced by the Lighting
Unit when the material enables fixed-function fragment lighting.

Five retail materials deliberately consume a fragment source while leaving that unit disabled.
For those draws, substituting vertex color is not an approximation of lighting; it is the wrong
branch. Dark Link material 0 is the close-test: `IsFragmentLighting=0`, while TEV stage 0 consumes
`FRAGMENT_PRIMARY`.

## RE and offline cache

- `oot3d-decomp/docs/fragment_lighting.md` records OoT3D `FUN_003fa5d0`: material byte `+0x00`
  gates the exact three-slot fixed-function setup, whose inputs are material emission, ambient,
  diffuse, specular 0, and specular 1 at `+0xA0..+0xB3`.
- Azahar's oracle implementation initializes both fragment outputs to `(0,0,0,0)` and calls
  `ComputeFragmentsColors` only when the PICA lighting-disable register is clear.
- `tools/cmb_fragment_lighting_survey.py` joins material flags/colors with active TEV slots over
  the user ROM without starting the oracle. The reusable detailed cache is
  `scratch/cmb_fragment_lighting_corpus.txt`.

The cache reports 1,997 CMBs / 11,172 materials, 205 fragment-light-enabled materials, 197 enabled
`FRAGMENT_PRIMARY` consumers, 69 enabled `FRAGMENT_SECONDARY` consumers, five disabled consumers,
eight enabled-but-unconsumed results, and zero failures.

## Port

- `fragmentLighting` now travels through `Zelda3DGlGroup`, native `SgGroup`, and shared UBO
  `uPrimaryCtl.y` instead of being parsed and discarded.
- Shared `tevRun` receives vertex primary, fragment primary, and fragment secondary as separate
  values in both native and unified shaders.
- Disabled materials receive the exact zero/zero PICA fragment colors. Enabled materials retain
  the existing primary approximation and zero secondary while their configuration/LUT path remains
  explicitly RE-partial; this milestone does not claim that enabled lighting is ported.

## Verification

- Offline instrument falsifiers: 3/3 pass.
- Retail corpus: `files=1997 materials=11172 fragment_enabled=205`
  `enabled_primary_consumers=197 enabled_secondary_consumers=69 source_without_flag=5`
  `flag_without_source=8 parse_failures=0`.
- Focused Clang C++ gate: 5/5 pass, including the real-ROM Dark Link parser/group/packed-TEV
  close-test and native/unified shader-source contracts.
- Full ROM-enabled `lus_tests`: 480/480 pass.
- Full Clang `zelda3d_app` build: pass.
- All touched C/C++ files pass `clang-format --dry-run --Werror`; all seven touched translation
  units pass the repository Clang-Tidy configuration with warnings treated as errors.
- `tools/re_frontier.py check`, `tools/codemap.py check`, `tools/info.py check --no-stale`, and the
  scoped `git diff --check` pass. The composed Clang verifier reaches its global structure gate and
  is blocked by eleven unrelated pre-existing SoH enhancement files above their frozen limits; no
  touched fragment-lighting file is listed.

No oracle process was started. User-visible parity is not claimed; the next enabled-path step is a
single cache-owned structured capture of the active PICA config, light records, and selected LUTs,
validated against the static `FUN_003fa5d0` products before shader implementation.
