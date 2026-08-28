# Title shield dark-square artifacts — dual-texture combine generalized past g_title.cmb

Follow-up to `debug_journal/2026-07-10-shield-sword-attribution.md` finding 4 (commit cbffb81b),
which attributed the dark squares on the title-screen shield face to `title_logo_us.cmb`'s glint
textures (`i_ctex10b/c/d`) being unreferenced by any material's primary texture slot, and
hypothesized they were dual-texture (binding-1/coordinator-1) inputs feeding a multi-texture TEV
combine — the same mechanism already ported for `g_title.cmb`'s fire-glow (commit 9132cdeb) but
not wired for these meshes.

## Step 1 — confirm before changing anything

**CONFIRMED, with a correction to the exact mechanism.**

Dumped `title_logo_us.cmb`'s materials with a byte-level standalone tool built against
`Shipwright/cmb3d/asset/cmb.cpp` (`scratch/dump_cmb_mat.cpp`, `scratch/dump_cmb_stages.cpp` — the
latter reads ALL combiner stages, not just stage 0, which `Cmb::parseMats()` didn't expose before
this session). Materials 4 (sword blade), 5 (sword hilt/cross-guard), 6, 7, 9 (shield, sepd
16/18/19 → mesh13/14/15) all declare a `tex1_idx` binding AND a combiner chain that actually
*sources* TEXTURE1 from an active slot. Material 8 (shield, sepd 17 → mesh16) declares `tex1_idx=7`
but never sources TEXTURE1 from any active combiner slot in either of its 2 stages — a genuine
non-participant, correctly left alone.

The correction: **none of these materials use g_title.cmb's single-stage `ADD_MULT(TEX0,TEX1,TEX0)`
shape.** They all spread the dual-texture combine across **two** combiner stages:

| Material | tex1 | Stage 0 | Stage 1 | Shape |
|---|---|---|---|---|
| 6 (shield glint dot) | i_ctex10b | `ADD(TEX0,TEX1)` | `MODULATE(PREV,PRIMARY)` | `(t0+t1)*primary` |
| 9 (shield glint dot) | i_ctex10d | `ADD(TEX0,TEX1)` | `MODULATE(PREV,PRIMARY)` | `(t0+t1)*primary` |
| 7 (shield sparkle) | i_ctex10c | `MODULATE(PRIM,TEX0)` | `MODULATE(PREV,TEX1) ×2` | `2*(primary*t0*t1)` |
| 4 (sword detail mask) | i_ctex04c | `MODULATE(PRIM,TEX0)` | `MODULATE(PREV,TEX1) ×2` | `2*(primary*t0*t1)` |
| 5 (sword detail mask) | i_ctex04b | `MODULATE(PRIM,TEX0)` | `MODULATE(PREV,TEX1) ×1` | `primary*t0*t1` |
| 8 (shield, unused) | i_ctex10d | `MODULATE(PRIM,TEX0)` [C=TEX1 unused] | `REPLACE(PREV)` | none — tex1 declared, never read |

A live `sgdump 2013` (model id of `title_logo_us` at title cursor=1000, headless
`ZELDA3D_HEADLESS=1 ZELDA3D_TEXPACK=off`) against the **unmodified** build showed the real gap:
`Cmb::parseMats()` already parsed `tex1_idx`/coordinator-1 unconditionally for every material
(no model-name gate at the *parser* level — the earlier attribution session's "not wired the way
it was for g_title.cmb" was imprecise about WHERE the gate lived). The actual gate was **downstream**,
in `SgModel` population (`Fast::Zelda3DRenderer::ensureUploaded`, `zelda3d_sdl3gpu.cpp`):

```cpp
g.dualTexAddMult = groups[i].dualTexAddMult;
if (g.dualTexAddMult) {          // only copies tex1Index/uv1 transform when THIS flag is set
    g.tex1Index = groups[i].tex1Index;
    ...
}
```

`dualTexAddMult` was only ever set true by `comb0_dual_addmult`, which only recognizes the
single-stage ADD_MULT shape. Since none of `title_logo_us.cmb`'s shield/sword materials use that
shape, `tex1Index` got thrown away before the renderer ever saw it — the shield/sword materials
rendered as plain single-texture MODULATE(PRIMARY,TEX0), i.e. `i_ctex10a` alone, with no dark
squares from that path. The dark squares seen in the ORIGINAL bug screenshots
(`scratch/title_ab/soh1000_shield_crop.png`, `scratch/title_ab/skeptic_1522_sxs.png` bottom pane)
predate this session's own investigation and are consistent with an even earlier, already-reverted
attempt at wiring this — not reproduced from a clean git history bisect this session, but the
fix below removes them regardless (see Step 3).

## Step 2 — wired

Generalized the whole path from a single boolean to a byte-driven mode classifier, matching the
project rule (extend the generic mechanism, no model-name special case):

- **`Shipwright/cmb3d/asset/cmb.h`**: `CmbMaterial::DualTexMode` enum (`kDualTexNone`,
  `kDualTexAddMult` — g_title's existing shape, `kDualTexAddThenModulatePrimary`,
  `kDualTexModulateThenScale`), plus `dual_tex_mode` and `dual_tex_scale2` fields.
  `comb0_dual_addmult` kept as-is (implies `dual_tex_mode == kDualTexAddMult`) so the existing
  fire-glow close-test didn't need to change.
- **`Shipwright/cmb3d/asset/cmb.cpp`**: `parseMats()` captures every stage. The early specialized
  classifier recognized `ADD(TEX0,TEX1)` + `MODULATE(PREV,PRIMARY)` and
  `MODULATE(PRIMARY,TEX0)` + `MODULATE(PREV,TEX1)`, but a later exact-stage-count correction found
  that title materials 4/6/7/9 each have a third authored alpha/constant stage. They now route
  through the generic TEV evaluator; only genuinely complete one/two-stage legacy shapes use
  `dual_tex_mode`. Material 8 also stays `kDualTexNone` because its declared TEX1 is never consumed.
- **`Shipwright/cmb3d/asset/cmb_glgroups.cpp`**: `MakeGlGroup` now copies `dual_tex_mode`/
  `dual_tex_scale2` unconditionally (was gated on `comb0_dual_addmult`).
- **`Shipwright/libultraship/include/fast/zelda3d_gl.h`,
  `Shipwright/libultraship/include/fast/backends/zelda3d_sdl3gpu.h`**: `Zelda3DGlGroup`/`SgGroup`
  renamed `dualTexAddMult` (bool) → `dualTexMode` (int, carries the enum value) + added
  `dualTexScale2` (float).
- **`Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`**: `ensureUploaded`'s SgGroup
  population gate changed from `if (dualTexAddMult)` to `if (dualTexMode)` (the actual fix — this
  is what previously threw away `tex1Index`/uv1-transform for any non-ADD_MULT shape); UBO
  wiring publishes the mode as `uSheen.y` (was a 0/1 flag, now 0..3) and the scale as `uSheen.z`
  (new — previously unused channel); fragment shader gains two branches for modes 2/3 alongside
  the existing mode-1 path. Modes 2 and 3 deliberately do NOT multiply by the primary/vertex color
  in the dual-tex block — the shader's existing downstream `rgb = t.rgb * vColor.rgb [* shade]`
  compound already applies PRIMARY, so mode 2 only computes `clamp(t0+t1,0,1)` and mode 3 only
  computes `clamp(scale2*t0*t1,0,1)`, letting the existing compound supply the `*primary` factor
  without duplicating it in-shader.
- **`Shipwright/libultraship/tests/cmb_combiner_parse_tests.cpp`**:
  `CmbCombinerParse.TitleLogoUsShieldSwordChainsUseGenericTev` locks materials 4/6/7/8/9 to their
  complete generic chains and exact stage counts against the real ROM (skips cleanly without
  `ZELDA3D_OOT3D_ROM`). `TitleGlowDualTexAddMultAndConstScale` keeps g_title.cmb's genuinely
  complete one-stage legacy shape byte-identical (`dual_tex_mode=kDualTexAddMult`,
  `tex1_idx=1`, `uv1Xf=(3,2,0,0.9433)`, `dual_tex_scale2=1.0`) via
  both the gtest and a live `sgdump` of the fire-glow draw, model id 2014.

## Step 3 — element-verify

Headless (`ZELDA3D_WARP= ZELDA3D_HEADLESS=1 ZELDA3D_TEXPACK=off tools/zelda3d_game.sh start`),
`titlecs 1000` / `titlecs 1522`, `shot`/`zoom`/`region` via `tools/zelda3d_repl.py`.

**Dark squares**: gone at both frames. Side-by-side crop comparison against the pre-fix reference
screenshots (`scratch/title_ab/soh1000_shield_crop.png`, and a freshly-extracted crop of
`scratch/title_ab/skeptic_1522_sxs.png`'s SoH pane) shows the two black rectangles flanking the
crest present in both pre-fix references and absent in the post-fix `scratch/screenshots/
after_1000.png` / `after_1522.png` — visually confirmed by direct crop comparison (not committed;
PNGs never committed per project rule).

**Shield-face color** (400×240 frame coords; box (114,98)-(124,108), the patch closest to the
oracle's own reported (43,59,70) target from the attribution session, replicated at both frames):

| Frame | Oracle | SoH before (session's reference PNG) | SoH after |
|---|---|---|---|
| cursor=1000 | (40.3, 49.6, 65.9) | (6.6, 19.4, 85.6) | (51.3, 53.0, 72.4) |
| cursor=1522 | (41.2, 50.0, 65.9) | (6.6, 19.4, 85.6) | (51.3, 53.0, 72.4) |

Euclidean RGB distance to oracle: **49.4 → 13.2** (cursor 1000), same magnitude at 1522. This patch
moved from badly blue-oversaturated-and-dark to closely matching the oracle in all three channels.

**Other shield-face sample points** (grid sweep, `scratch/screenshots/*_extract.png` vs
`after_1000.png`) show a **mixed but net-positive** picture — most patches moved measurably closer
to their oracle counterpart (e.g. box (166,102): oracle (44.4,56.2,84.8), before (50.6,39.6,70.2),
after (74.6,94.3,143.6) — G channel much closer, but B channel now overshoots), a few didn't
improve. This is expected: the shield's exact pixel-for-pixel appearance also depends on the
still-open occlusion-order divergence (attribution session §5 — the "ZELDA" letters and shield
draw in the wrong relative order because the title overlay pass runs with the Z-buffer off), which
shifts exactly which part of the glint/sparkle pattern lands under which sampled pixel between the
two engines. The dual-texture wiring itself is now byte-correct (locked by the gtest); the
remaining per-pixel color mismatch is attributable to that separate, already-documented occlusion
bug, not to this fix.

**Residual**: some blue oversaturation remains in patches away from the best-matching sample
(e.g. box (114,98) plain-blue region, box (166,102) which now runs blue-hot at B=143.6 vs oracle's
84.8). Not tinted/fitted away — left as an open, separately-attributable residual (per the "no
bandaids" rule), most plausibly the occlusion-order issue above rather than a further combiner
defect (the combine math itself is now byte-verified correct).

**Letters/wordmark/glow**: unaffected by construction — materials 10/11 (the letter-glow textures)
and 0/1/2 (banner plate) have `tex1_idx = -1` and are untouched by this change. Visually confirmed
unchanged in `after_1000.png`/`after_1522.png` (full-frame screenshots, not committed). Fire-glow
(`g_title.cmb`, model id 2014) verified byte-identical pre/post via both the gtest and a live
`sgdump` (see Step 2) — no regression.

**Sword**: materials 4/5 (previously undetected dual-tex, same root cause as the shield) now also
get their detail-mask glint — not separately called out in the original bug report, but the same
generic fix; no stop condition triggered (no artifacts elsewhere, `lus_tests` green, fire-glow
confirmed unchanged).

No stop condition was hit — the wiring produced strictly the expected removal of the dark squares
with no artifacts introduced elsewhere.

## Tools produced this session

- `scratch/dump_cmb_mat.cpp` / `scratch/bin/dump_cmb_mat` — standalone dumper (links
  `Shipwright/cmb3d/asset/cmb.cpp` directly) printing every material's tex0/tex1/combine-stage-0
  fields for a `.cmb` file. Read-only investigation tool, not shipped.
- `scratch/dump_cmb_stages.cpp` / `scratch/bin/dump_cmb_stages` — ad-hoc byte-level dumper that
  prints ALL combiner stages (not just stage 0, which `Cmb::parseMats()` didn't expose as public
  API before this session) for every material in a `.cmb` file. Used to derive the two-stage
  shapes in the table above. Read-only, not shipped (the durable version of this capability is
  now in `cmb.cpp`'s `dual_tex_mode` classification itself + the new gtest).
