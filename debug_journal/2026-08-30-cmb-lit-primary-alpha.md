# 2026-08-30 — CmbVShader lit PRIMARY alpha / HasColor port

## Root cause

The native CMB packer zeroed `uLitDif1.a` and `uLitDif2.a`, while both native and unified
vertex shaders emitted `aColor.a` for every vertex-lit draw. That is not the PICA program.
`CmbVShader.shbin` words 89--110 accumulate `MatDiffuseColor.a * LightDiffuseColor_i.a`
once per enabled light, without `NdotL`, and multiply the completed RGBA value by `aColor`
only when draw uniform `HasColor` is true. For a missing color stream, the old white vertex
default therefore replaced authored material alpha with 1.

## Ground truth and cached observations

- `oot3d-decomp/docs/title_env_lighting.md` section 10.2b records the exact disassembly and
  formula. The source program is the offline ROM artifact `scratch/raw/CmbVShader.shbin`; no
  oracle was started for this change.
- Existing cached oracle logs `scratch/title_ab/actor_light_uniforms.log` and
  `scratch/zora/zora_vsuni.log` show enabled slots 0/1 with diffuse alpha 1 and disabled slot 2
  with diffuse alpha 0. Those caches determine the host two-slot alpha payload directly.
- The expanded offline `tools/cmb_primary_corpus_survey.py` combines material bytes, mesh
  `HasColor`, and authored TEV stages. Its cache at `scratch/cmb_primary_corpus.txt` reports
  1,997 CMBs, 154 unlit candidates, 24 lit/no-color/non-opaque candidates, and zero failures.
  All 24 lit candidates consume `PRIMARY.a`; 20 have c8 alpha below 128 and remain translucent
  after the two enabled alpha-one slots are summed.

## Port

- The native packer preserves authored c8 alpha in both enabled diffuse-product slots.
- Native and unified vertex shaders construct the complete lit RGBA accumulator, conditionally
  multiply it by the color attribute under the real `HasColor` uniform, then saturate at the
  PICA vertex-output boundary.
- The bottled-Poe retail close-test locks c8 alpha 76/255, absent color data, vertex lighting,
  and TEV stage-0 `PRIMARY.a` consumption through parser, draw group, and GL material packing.

## Verification

- Offline instrument falsifiers: 5/5 pass, including positive lit-alpha and negative
  texture-only-alpha controls.
- Retail scan: `files=1997 unlit_candidates=154 lit_alpha_candidates=24 parse_failures=0`.
- Focused C++ gate: 6/6 pass.
- Full ROM-enabled `lus_tests`: 476/476 pass in the Clang build tree
  `scratch/build-cmb-primary-clang`.
- Full `zelda3d_app` build succeeds in that tree, whose CMake cache identifies Clang as the
  C++ compiler.
- `clang-format --dry-run --Werror` passes for every touched first-party C/C++ file, and
  repository-root `clang-tidy --warnings-as-errors=*` passes for all five touched translation
  units.
- The freshly built game reaches `[Zelda3D_SG] resources ready (unified op model)` on the
  headless default renderer with no shader-compilation failure. The generic runtime manager's
  REPL-ready check timed out after 40 seconds while the process remained alive, so this proves
  renderer initialization only, not a complete live gameplay path.
- `tools/re_frontier.py check`, `tools/codemap.py check`, and `tools/info.py check --no-stale`
  pass. The full information check still reports seven older stale claims. The normal Clang
  verifier stops at 12 unrelated legacy structure-ratchet violations before its file checks;
  no touched graphics file is in that list.

Visual/oracle parity is not claimed by this close-test. The next live host gate verifies shader
submission through a representative lit/no-color item model; a later cached-oracle-compatible
image comparison can close that user-visible result without re-booting the oracle.
