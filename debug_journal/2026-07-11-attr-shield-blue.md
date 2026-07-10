# Shield-face blue-excess attribution + occlusion re-measure (measurement only, 2026-07-11)

Task: attribute the shield-face "blue-hot" residual (`debug_journal/2026-07-10-shield-scale-measure.md`
§1: loose blue-hue mask, B channel +8..15% hot vs oracle, R/G reportedly within ±5%) to (a) texture
decode, (b) a const/tint register, or (c) vertex colors — and re-measure the shield occlusion
footprint on the current build. No code changed; tree clean except this journal.

Build: current `Shipwright/build-cmake/soh/soh.elf` (2026-07-11 00:34, includes the fog port
`19081f9a` and the dual-tex/sheen fixes). Model id `2014` is the ACTUALLY-DRAWN `title_logo_us`
copy (a stray unused registration also exists at id `2013` from a generic skinned-actor autoload
path — never uploaded/drawn; don't target it). Shield materials = mat6/7/8/9 (all `tex0=4` =
`i_ctex10a`, format `RGB565` **not** ETC1A4 — `etc1=False`, `gl_format=0x83636754`).

## (a) Texture decode — RULED OUT, byte-exact

`i_ctex10a` is RGB565 (5:6:5), not ETC1A4 — the `+0xB4` const-color off-by-one bug class (which
was ETC1A4/const-palette specific) doesn't apply to this texture at all. Both decoders use
identical bit-expansion formulas at identical bit positions (R/B: `e5(n)=(n<<3)|(n>>2)` on bits
15-11/4-0; G: `e6(n)=(n<<2)|(n>>4)` on bits 10-5) — `tools/pica_texture.py::_decode` vs
`Shipwright/cmb3d/asset/pica_texture.cpp` (`GF_RGB565` case, line ~125). Verified two ways:

1. Independent Python decode of the ROM bytes (`tools/pica_texture.py decode_cmb_texture`):
   mean RGBA = **(77.01, 78.43, 107.13, 255)**.
2. Live SoH GPU-side dump (`ZELDA3D_SG_DUMPTEX=2014`, model 2014's tex index 4): source-texel
   mean AND a full GPU readback of the uploaded texture (catches upload/swizzle corruption, not
   just decode) — both report `srcMeanRGBA=(77,78,107,255) gpuMeanRGBA=(77,78,107,255)`, i.e.
   bit-identical to the independent decode (rounding aside) and self-consistent through upload.

The texture itself is genuinely blue-leaning (B=107 vs R=77/G=78, the art is a blue shield) but
SoH reproduces those exact texel values — **no decode-side channel bug exists**.

## (b) Const/tint register — RULED OUT for the palette; but exposed a REAL related bug (see §3)

`CmbMaterial::mat_constant[6][4]` (the `+0xB4..+0xCB` palette, already fixed for the earlier
off-by-one bug) is **dead for all four shield materials** — parsed as `[(0,0,0,0)]*5 +
[(1,1,1,1)]` for mat6/7/8/9 alike, and none of their combiner stages actually CONSUME `CONSTANT`
as a used source slot (`comb_uses_const=False` by the `slotsUsed` rule in `cmb.cpp::parseMats` —
each material's one `REPLACE(PREVIOUS)` stage that nominally references `CONSTANT` in slots B/C
never reads them, since REPLACE only consumes slot A). So the const-color palette mechanism is
not in play here at all, ruling this candidate out as literally non-participating.

However, chasing "const/tint register" led to a **live comparison of `MatAmbientColor`
(matAmb) between the oracle and the CMB's own baked bytes**, which turned up something real —
see §3.

## (c) Vertex colors — RULED OUT, confirmed live

`vsuni_log` (embedded-Azahar oracle, `az=1000`, `scratch/title_settled.state` → `run 1000`) shows
**every** `title_logo_us` draw at this frame with `hasCol=0` — PICA's `HasColor` bool is off for
these draws, so no per-vertex color attribute is consumed; `vColor` in the shader compound is
whatever the fixed default is, not a baked-in blue tint. Confirms the earlier fireglow-session
finding ("matAmb=(1,1,1,0) hasCol=0") generalizes to the shield draws too.

## 3. What the vsuni_log capture actually found: matAmbient IS genuinely per-material and blue-biased — AND the oracle sums a second, unmodeled light

Ran `vsuni_log` over the full `run 1000` (39387 draw lines) and grepped the wordmark-signature
draws (`dir0=(0.57735,-0.57735,-0.57735,1) dif0=(1,1,1,1) amb0=(0.18,0.18,0.18,1)` — the sweeping
"sheen" light). Near the end of the frame (lines 39361-39377, the shield's own draws, `vLit=1`),
`matAmb` **varies per draw** and matches the CMB's own baked `mat_ambient` bytes **exactly**:

| observed `matAmb` (oracle, live) | CMB `mat_ambient` (parsed from ROM bytes) | material |
|---|---|---|
| `(0.098039,0.098039,0.098039,0)` | (0.098,0.098,0.098) | mat7 |
| `(0.74902,0.74902,0.74902,0)` | (0.749,0.749,0.749) | mat8/mat9 |
| `(0.098039,0.098039,0.14902,0)` | **(0.098,0.098,0.149)** | **mat6 — genuinely blue-biased** |
| `(1,1,1,0)` | n/a (letters, mat10/11 = white) | letters |

So the real 3DS hardware **does** modulate the wordmark's vertex-lit color by each material's own
baked `mat_ambient`/`mat_diffuse` — bit-exact against the CMB file, confirming SoH's CMB parse of
these bytes is correct. The mechanism this rules OUT: it is **not** a wiring/parse bug where SoH
reads the wrong bytes — both engines agree on the raw material constants.

The bug is downstream, in the **wordmark sheen shader compound**
(`Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` ~L294-309). Its derivation comment states
the formula as measured against the LETTER materials only:

```
o1 = matAmb(1)*lightAmb(0.18) + max(0, dot(N,-L)) * matDif(1)*lightDif(1)
   = 0.18 + max(0, dot(N,-L))          // valid ONLY when matAmb == matDif == (1,1,1)
```

and the shader hard-codes exactly that flattened scalar (`shade = clamp(uSheen.x + ndotl, 0, 1)`,
applied identically to R/G/B, `uSheen.x` = the constant 0.18) — `matAmbient`/`matDiffuse` are
**never multiplied in** on this path (the legacy renderer's separate `uMatAmbient` UBO field
exists but belongs to the *unified* renderer, gated off by default — the active title-wordmark
draw path uses only `shade * t.rgb * vColor.rgb`, no per-material ambient/diffuse term at all).
The assumption `matAmb=matDif=(1,1,1)` is **true for the letter materials (mat10/11 measured
`(1,1,1,0)`) but FALSE for every shield/sword material** (mat6 in particular, `(0.098,0.098,
0.149)` — R=G, B is 52% brighter in the material's own baked ambient).

Separately, the same `vsuni_log` lines show a **second light slot is always active and
untouched by SoH's port**: `dir1=(0,0,-1,0) dif1=(0.40784,0.40784,0.27059,1) amb1=(0,0,0,1)` on
every wordmark draw, frame-invariant (not swept like light0/"sheen"). `dif1` is warm-toned
(R=G=0.408, B=0.271 — R/G run 51% brighter than B in this light's own diffuse color). SoH's
`Zelda3D_GL_SetLightDirOverride` plumbing (`zelda3d_gl.cpp`/`.h`) only carries **one** direction/
color pair (`lightDirOv`, feeding `uSheen`/`uLightDir`) — there is no second-light uniform or
shader term anywhere in this draw path. The oracle's real per-vertex color is (schematically,
per material, ignoring clamps):

```
v = matAmb*(0.18,0.18,0.18) + max(0,N·-L0)*matDif*(1,1,1)     [light0, SoH HAS this, minus matAmb/matDif]
  + matAmb*(0,0,0)          + max(0,N·-L1)*matDif*(0.408,0.408,0.271)   [light1, SoH DROPS this entirely]
```

Both omissions push in the SAME direction for the shield (whose own `matDif≈(1,1,1)` per §earlier
CMB dump, so the light1 diffuse term's warm R/G-over-B bias transfers straight through): dropping
light1 removes a warm (R,G > B) additive contribution that the real hardware sums, and dropping
matAmbient scaling replaces mat6's actually-small ambient floor (`0.098*0.18=0.018` R/G,
`0.149*0.18=0.027` B) with a uniformly-large `0.18` floor on all three channels. Both are
numerically real, confirmed against live oracle registers, and independent of the texture/const
findings above (§1-2).

**Named mechanism: the wordmark-sheen port is a single-light, `matAmb=matDif≡1` special case
that only holds for the letter materials — it needs (i) per-material `matAmbient`/`matDiffuse`
multiplied into the shade term (data already parsed correctly in `cmb.cpp`, just never plumbed
into this shader path) and (ii) light-slot 1 (`dir1=(0,0,-1)`, `dif1=(0.408,0.408,0.271)`,
`amb1=0`, static/non-swept) added as a second `max(0,N·-L)*matDif*dif1` term. Neither is present
today; both are measured live from the oracle, not modeled from decomp docs.**

## 4. Re-measured residual on the CURRENT build — the "B-only" framing is STALE, flag for follow-up

Re-ran the exact §1 methodology (loose/strict blue-hue mask, box `x[100,220] y[80,160]`,
`az=1000/soh=1405` — the current camera-exact pairing) on the current build
(`tools/title_ab.py ab 1000 --soh 1405`, then a blue-mask sweep script):

| mask | oracle mean (n) | SoH mean (n) | SoH/Az ratio |
|---|---|---|---|
| loose (2026-07-10, pre-fog-port) | (16.9,40.2,101.1) (1137) | (17.2,40.6,116.1) (1475) | (1.018, 1.010, **1.148**) |
| loose (2026-07-11, current/post-fog) | (17.0,40.6,101.5) (1141) | (19.0,41.3,115.2) (1512) | **(1.118, 1.018, 1.136)** |
| strict (2026-07-10) | (19.0,49.3,120.2) (797) | (19.5,46.6,129.8) (1220) | (1.027, 0.945, **1.080**) |
| strict (2026-07-11, current) | (19.1,49.8,120.6) (801) | (21.6,47.8,129.9) (1233) | **(1.133, 0.960, 1.077)** |

Oracle-side means and counts are essentially unchanged (as expected — the oracle capture is
cached/deterministic and the fog port doesn't touch this scene). **SoH-side R has moved from
+1.8%/+2.7% to +11.8%/+13.3%** while B stayed roughly flat (+13.6%/+14.8% → +13.6%/+7.7%) and G
stayed flat. The clean "B-specific, R/G within ±5%" characterization from 2026-07-10 **no longer
holds on the current build** — R is now comparably or more elevated than B.

Leading explanation (not confirmed further this session, flagged for follow-up): the mask box is
a fixed screen-space rectangle, not a shield-only selector — it also catches background/letter-gap
bleed-through pixels wherever the (known, separately-documented) shield/letter pose offset exposes
different content between the two panes. The FOG PORT (`19081f9a`, landed after the 2026-07-10
measurement) changed the color of exactly that background/terrain content (the dawn-hue fix), so a
shift in what leaks through the mask box, not a shield-material channel bug, is the more likely
cause of the new R-channel elevation. **This needs a shield-only-pixel mask (e.g. gated on actual
draw-call identity via `SOH3D_PIXEL_TEX`/`SOH3D_PIXEL_XY` reading back `tex0` per fragment, not a
loose HSV box) before drawing further conclusions about per-channel bias** — out of scope for this
measurement pass to build.

## 5. Occlusion footprint re-measure — unchanged, delta persists

Same box/masks, pixel COUNTS (footprint size, not color):

| mask | oracle n | SoH n | SoH/Az |
|---|---|---|---|
| loose, 2026-07-10 | 1137 | 1475 | 1.297 |
| loose, 2026-07-11 (current) | 1141 | 1512 | **1.325** |
| strict, 2026-07-10 | 797 | 1220 | 1.531 |
| strict, 2026-07-11 (current) | 801 | 1233 | **1.539** |

**The footprint delta is unchanged to within measurement noise** across the fog-port landing —
SoH's shield-blue silhouette is still ~30-54% larger in pixel count than the oracle's, same
magnitude as before `19081f9a`. Since `3e6186ad` ("overlay-pass depth scope for shield occlusion")
already landed before both measurements and the ratio didn't move, that fix's intra-model depth
scoping did NOT fully close this gap — a residual footprint delta of this size remains.

Attribution (not independently re-derived this session — citing and confirming the still-valid
prior finding from `debug_journal/2026-07-10-shield-sword-attribution.md` §3, since nothing in
this session's investigation touched geometry/pose): the leading candidate remains the
**already-measured ~3° sword/shield pose offset** (oracle ≈29° from vertical, SoH ≈32°) plus the
inherent imprecision of a fixed-threshold HSV mask at a silhouette edge — a few degrees of pose
rotation shifts which pixels straddle the shield's edge into or out of the hue/saturation
thresholds, inflating the naive pixel count without requiring any scale error. This was NOT
re-derived with fresh angle measurements this session (out of scope for the token budget here);
flagged as the standing explanation, not newly confirmed.

## Tools/artifacts (scratch, not committed)

- `scratch/vsuni_shield.log` (39387 lines) — full `vsuni_log` capture of `run 1000`, shield
  signature at lines 39361-39377.
- `scratch/title_ab/shieldattr_1000.{az,soh}.png`, `_sxs.png` — fresh az=1000/soh=1405 capture.
- `scratch/logs/run.71.log` — instance-71 `ZELDA3D_SG_DUMPTEX=2014` boot log with the `[SG_DUMP]`
  texture-mean lines (tex4 = i_ctex10a).
- Inline Python one-liners (this session's transcript) for: CMB material-constant/combiner-stage
  parse (mat6-9), RGB565 mean decode cross-check, blue-hue mask sweep — none saved as standalone
  scripts (small enough to be reproduced from this journal's code blocks; a durable version would
  belong in `tools/cmb.py`/a new `tools/title_shield_mask.py` if this line of investigation
  continues).

## Fix spec for a follow-up session (not implemented — measurement only)

1. `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` wordmark-sheen shader block
   (~L289-309): multiply `shade` by each draw's `matAmbient`/`matDiffuse` (already available as
   `grp.matAmbient[3]`/`grp.matDiffuse[3]`, parsed correctly in `cmb.cpp`, just not plumbed into
   this UBO/shader path) instead of the flattened `matAmb=matDif≡1` assumption.
2. Add a second light-direction/color pair to the wordmark override plumbing
   (`GlModel::hasLightDirOv` in `zelda3d_gl.h`/`.cpp`, `uSheen`/`uLightDir` UBO fields in
   `zelda3d_sdl3gpu.cpp`) for the static `dir1=(0,0,-1) dif1=(0.408,0.408,0.271) amb1=(0,0,0)`
   light slot, confirmed live and frame-invariant — not swept like light0, so it can likely be a
   second constant uniform rather than a per-frame override.
3. Verify with a shield-ONLY pixel selector (gate by draw identity, e.g. via `sgdump`'s per-group
   render state or a `SOH3D_PIXEL_XY`-style readback keyed to `tex0==i_ctex10a`'s physical
   address) rather than a loose screen-space HSV box, since §4 shows the box conflates
   shield-material color with background bleed-through at the shield's silhouette edge.
4. Acceptance: re-run the loose/strict masks above; expect R ratio back to ~1.0-1.05 (matching
   the pre-fog-port baseline, confirming the fog port itself isn't broken) and B ratio measurably
   reduced from the matAmbient/light1 fix specifically (recompute the predicted per-channel shade
   with the real 2-light formula at the shield's actual `ndotl` — not done this session, needs a
   live `N`/`ndotl` readback at the sampled fragment to get an exact predicted number).
