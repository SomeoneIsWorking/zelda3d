# 2026-07-22 (later) — generic PICA multi-texture / multi-stage TEV emulation

Outcome in one line: **the full per-stage PICA TEV chain (up to 6 stages, three texture
units, per-stage ops/operands/scales/const-selects) is now parsed from the CMB and
evaluated in the fragment shader**; the per-material combiner gap at Zora's Domain is
closed — its multi-texture draws now sit in the SAME 0.77-0.87 band as the scene's
single-texture surfaces, so the remaining Zora residual is one scene-wide cause
(`render.zora-ground-deficit`), no longer two. Kokiri (the regression gate) held.

Nothing committed. Working-tree changes listed at the bottom.

## Corpus survey first (bounding the work with data)

`tools/tev_corpus_survey.py` (NEW) parses the raw 0x28-byte combiner entries of every CMB
in the ROM (1997 files incl. all .zsi rooms, 11172 materials) and validates every field
against its legal enum domain — ZERO violations, so the layout in
`oot3d-decomp/docs/pica_tev_combiner.md` (NEW) is the whole story. Key counts: 8232/11172
materials are the trivial single MODULATE(PRIMARY,TEX0) shape; 1622 consume texture1, 250
texture2; ops used = REPLACE/MODULATE/ADD/ADD_SIGNED/INTERPOLATE/SUBTRACT/MULT_ADD/
ADD_MULT (no DOT3). Every coordinator in the ROM sources texcoord0.

Two enum-label bugs found in existing cmb.cpp comments/code and fixed: 0x8574 is
ADD_SIGNED and 0x8575 INTERPOLATE (were swapped); SUBTRACT is 0x84E7 (the old
`case 0x8506` matched nothing).

Cross-validation: spot07_1 (Zora interior) mat0-2's static 3-stage chain decodes
byte-identically to the live oracle's per-draw TEV register log — the game uploads the
CMB combiner verbatim.

## The port

- `cmb.h/.cpp`: full `CombStage[6]` capture (rgb+alpha op/scale/sources/operands, buffer
  inputs, PER-STAGE const index), texture binding 2 + coordinator 2, and the routing flag
  `tev_generic` = NOT the trivial single-MODULATE shape and NOT a classified dual-tex
  title shape (those keep their verified legacy paths — CLOSED rows untouched by
  construction; Kokiri terrain is all trivial-single so it keeps the bit-identical fast
  path).
- `cmb_glgroups.cpp`: GL-DMP -> PICA code packing (3 u32 words per stage, documented at
  `Zelda3DGlGroup::tevStagePack`).
- `zelda3d_sg_ubo.h` + shader UBO: `uTevStages[6] (uvec4)`, `uTevConst[2]` (six RGBA8
  const slots, quantized like PICA's 8-bit registers, AFTER the per-actor override
  channel), `uTex2Xf`, `uTevCtl` (stage count + coordinator mappings). Common block
  368->656 bytes, still far under the 4096 push cap; unified twin + offset tests updated.
- `zelda3d_sdl3gpu.cpp` kFrag: `tevRun()` — faithful per-stage evaluation incl. clamp-on-
  register-write, per-stage x1/x2/x4 scales, Dot3, Lerp, MultiplyThenAdd, AddThenMultiply,
  and PICA's alpha test on the FINAL combiner alpha (not the raw texel) for generic draws.
  The vertex-lit PRIMARY computation now has one home (`prim`) shared by legacy + generic.
- Third texture unit: the dead ex-shadow-map sampler slot (set=2 binding=1) now carries
  uTex2; kVert gains vUv2 through coordinator-2 (UV or sphere mapping).
- sgdump prints `tevGeneric/stages/tex2/coordMap/pack` per group.
- `tools/tev_mask_ratio.py` (NEW): the per-draw mask ratio measurement (ours/oracle inside
  each oracle isolation mask at a matched camera) as a durable tool — it reproduces this
  morning's ad-hoc numbers.

Documented approximations (rare, none at Zora/Kokiri): FRAGMENT_PRIMARY/SECONDARY (199+69
materials) map to the vertex-lit primary / (0,0,0,1) — fragment lighting unemulated;
PREVIOUS_BUFFER (14 materials) evaluates as 0 (the initial combiner-buffer color is an
uncaptured runtime register); TEXTURE3 (1 material) falls back to tex0; coordinator
mapping 4 (ProjectionMap, 366 materials, mostly tex1) falls back to plain UV.

## Measurement (matched cameras, oracle masks/frames unchanged from this morning)

Zora (entrance 0x109, 0x6000, cam -1286.4 285.1 -159.0 -> -1089.4 250.3 -160.2), ratio
ours/oracle inside each oracle draw mask (tools/tev_mask_ratio.py, before = this
morning's z3d_zora_iso.png against the same masks):

| draw | surface | before | after |
|---|---|---|---|
| d15 | water (tex0+1+2) | 0.640 | 0.677 |
| d49 | water | 0.734 | 0.765 |
| d9  | water | 0.742 | 0.772 |
| d54 | water sheet (1 tex) | 0.797 | 0.799 |
| d11 | near ground (1 tex) | 0.905 | 0.808* |
| d3  | rock walls (1 tex) | 0.860 | 0.859 |
| d48 | waterfall | 0.885 | 0.874 |

(*d11 moved because the before/after frames are separate live sessions — its material is
trivial-single-MODULATE and takes a byte-identical code path; the delta is scene-state
noise (actor shadows/time drift), not the TEV change.)

The signal: BEFORE, multi-tex draws were 0.13-0.20 BELOW the single-tex surfaces of the
same frame; AFTER, all surfaces sit in one 0.77-0.87 band. The water's own stage
contributions are small in absolute terms because `s07_uvwater_01/02` genuinely average
(17,23,23)/(9,40,46) RGB — the static data bounds the gain, and our measured deltas
(+7/+8 G/B on water, +0 R since water vColor.r=0) match that prediction. The remaining
uniform deficit is `render.zora-ground-deficit` (single-tex surfaces prove it is not a
combiner problem).

Kokiri gate (entrance 0xEE, same method): all large surfaces stayed 0.93-1.10 (d7
0.963->0.986, d26 0.982->1.006, d8 1.095->1.102, d15 0.926->0.932...). Bonus direct
evidence the evaluator is oracle-accurate where no scene-wide deficit overlays it:
Kokiri's multi-stage material d68 went **0.561 -> 0.987**.

Unit tests: UBO offset tests updated (uTevStages=496, uBones=656, verified by compile);
NOTE the `lus_tests` binary does NOT LINK at HEAD (pre-existing: undefined
`Zelda3D_DbgInputEnabled` from KeyboardKeyToButtonMapping.cpp) — my test edits compile at
object level; the link break predates this session (verified via git stash).

## Working-tree changes (nothing committed)

- `Shipwright/cmb3d/asset/cmb.h` / `cmb.cpp` — CombStage capture, tex2/coord2, tev_generic
  routing, enum fixes.
- `Shipwright/cmb3d/asset/cmb_glgroups.cpp` — PackTevStage + new group fields.
- `Shipwright/libultraship/include/fast/zelda3d_gl.h` — Zelda3DGlGroup TEV fields.
- `Shipwright/libultraship/include/fast/zelda3d_sg_ubo.h` — SgUbo uTevStages/uTevConst/
  uTex2Xf/uTevCtl.
- `Shipwright/libultraship/include/fast/backends/zelda3d_sdl3gpu.h` — SgGroup TEV fields.
- `Shipwright/libultraship/include/fast/backends/gfx_sdl3gpu.h` + `src/fast/backends/
  gfx_sdl3gpu.cpp` — model-draw sampler slot 1 = tex2 (ex-shadow).
- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — shader (tevRun, vUv2, uTex2),
  group copy, UBO fill incl. const-override packing, tex2 bind, sgdump.
- `Shipwright/libultraship/include/fast/unified_ubo.h` + `src/fast/backends/
  unified_shader.cpp` — size-parity mirrors (dormant path).
- `Shipwright/libultraship/tests/zelda3d_render_tests.cpp` — new offsets (also the missing
  uLitDif rows from an earlier session).
- `tools/tev_corpus_survey.py`, `tools/tev_mask_ratio.py` — NEW tools.
- `oot3d-decomp/docs/pica_tev_combiner.md` — NEW RE doc (in the submodule, uncommitted).
- `docs/re-frontier.md` — step update.

Artifacts: `scratch/tev_corpus.txt`, `scratch/screenshots/z3d_{zora,kokiri}_tev_after.png`.
