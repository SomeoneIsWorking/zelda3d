# 0031 — the sphere-mapping normal is world-relative on the host; retail's is camera-relative

**Verdict: the host's CameraSphereEnvMap (mapping method 3) arm transforms the normal into the wrong
space, on every draw except the one title overlay that overrides it. `mat3(uMV)` is the MODEL matrix;
retail's `r14` is `uModelView`-transformed, and the camera is inside `uModelView`, not inside
`uProjection`. This is `eb929ee6`'s recorded open question, now answered: it IS wrong, and the reason
verification never saw it is that the only oracle evidence for the arm is a capture whose camera
rotation is the identity.**

Row: `render.cmb-texcoord-mapping`. RE doc: `oot3d-decomp/docs/cmb_texcoord_mapping.md` §1, §3, §4.

## 1. What retail computes (words, not names)

Disassembled from `scratch/raw/CmbVShader.shbin` with `oot3d-decomp/tools/shbin_disasm.py` (exists; the
row's other measurement tools do not — see §6):

```
14     ifu b2 (IsSkinning) -> skinning 15..43 | else 44..50 -> 51
44-49  dp3 r11 = c20/c21/c22 . v1 ; dp4 r8 = c20/c21/c22 . v0   ; NON-SKINNED: uMatrixPallet row 0
52-54  rsq/mul r11 = normalize(r11)        ; unit object-space normal
55-58  dp4 r15.x/y/z = c4/c5/c6 . r8, r15.w = 1   ; r15 = uModelView . object POSITION
59-61  dp3 r14.x/y/z  = c4/c5/c6 . r11        ; r14 = uModelView . unit object NORMAL  <-- the normal
116-119 dp4 o0.x/y/z/w = c0/c1/c2/c3 . r15    ; POSITION = uProjection . uModelView . objectPos
```

(The skinned arm reaches the same `r8`/`r11` through `sub@266`, which applies `c20..c22`-style
palette rows per bone — words 267-274 — so the normal's two matrices are the same in both arms.)

`sub@295`, the CameraSphereEnvMap source, called by all three coordinators' method-3 arms
(word 131 = coordinator 0, 153 = coordinator 1, 188 = coordinator 2):

```
295  mov  r1.xy = c94.z = (0.5, 0.5);  r1.zw = 0
297  mad  r10.xy = r14.xy * 0.5 + 0.5
298  mov  r10.zw = (1, 1)
```

so the retail sphere coordinate is `uv = 0.5 * (uModelView · n_object).xy + 0.5`, fed through
`TexMtx_k` rows 0/1 (words 132-133, 154-155, 189-190). **The normal's absolute direction matters
here**, because the mapping is a function of the normal itself, not of a dot product with something
else.

`uModelView` (c4..c6) is not the model matrix. Two independent reasons, both measured:

**(a) `uProjection` carries no camera transform.** On title cs1093 draw 77 (oracle frame 2010) I
replayed the cached command list (`scratch/oracle_cache/a647213f0ded0038_.../artifacts/title-pica-command-list-data_4b0405b3adbf.bin`,
cursor 1432) through Azahar's own `ShaderSetup::WriteUniformFloatReg` protocol
(`Azahar/src/video_core/pica/shader_setup.cpp:33`, `packed_attribute.h:25`). Azahar's per-draw log
(`pica_core.cpp:379-410`) prints 28 vec4 uniforms; **24 reproduce exactly** — `proj0..3`,
`modelView0..3`, `texSlotMap`, `texMtx0_0..2`, `texMtx1_0..2`, `texMappingMethod`, `matDif`, `matAmb`,
`vtxScl0`, `dir0/1/2`, `dif0`, `amb0` — and the remaining 4 (`dif1`, `amb1`, `dif2`, `amb2`) are never
written before that cursor in the list at all, so Azahar's printed zeros are their untouched initial
value and all 28 agree. The decode is sound, and it reads

```
c0 = (0, 2.390579, 0, 0)      c2 = (0, 0, 1.000219, 7.001532)
c1 = (-1.434347, 0, 0, 0)     c3 = (0, 0, -1, 0)
```

which is a **plain perspective projection**: `sx/sy = 0.600000` = 240/400 to float24 precision (the
3DS top screen), and `(A,B)` give `near = 3.5004`, `far = 31970` with `w = -z`. The x/y swap is the
3DS screen-axis convention, not a rotation: every entry is `(x,y,0,0)`-shaped except the depth pair,
there is no translation term anywhere, and `c0..c3` are consumed by nothing except words 116-119 and
235-238 (the two bodies' identical POSITION lines) — there is no second projection site in the shader.
The same replay also reads the object's own matrix on that draw, `c20..c22 = (1,0,0,0), (0,1,0,0),
(0,0,1,-34)` — identity rotation, the wordmark's local `(0,0,-34)` offset — so **both** per-object
matrices retail feeds the normal have identity rotation there, and the third (the camera) is identity
too. That is the coincidence §4 is about, and it is why the arm looked right.

**(b) The shader's own `uInvView` needs a view-space input.** `sub@300`'s caller uses
`s = dp3(uInvView, viewNormal)` and `p = uInvView · viewPos` (words 156-163), and the mapping-4 arm
then treats `p` as "the placed position, camera removed". Those two uses — a rotation applied to a
normal, and an inverse applied to a point — only mean anything if `r15`/`r14` are camera-relative.
The game pushes `uInvView` per frame (the title list writes it in six separate blocks), which is a
camera state, not a model state.

So the camera transform reaches `r14`/`r15` through `uModelView` (either composed `view·model`, or
baked into the submitted positions — both put `r14` in a camera-relative frame), and `uProjection`
supplies only perspective. The POSITION chain has exactly one path, so there is nowhere else for it
to be.

## 2. What the host computes

`Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_shaders.cpp:188-194`:

```glsl
vWorld = (ubo.uMV * vec4(sp, 1.0)).xyz;
// Sphere-map normal space is the CmbVShader's c4-c6 uModelView transform. For ordinary scene
// draws host uMV carries the same transform. ...
vec3 ns = (ubo.uSphNrm0.w > 0.5) ? ... : (mat3(ubo.uMV) * nM);
```

The comment's claim is false, and the camera's location is not a matter of interpretation:

| host uniform | source | what it is |
| --- | --- | --- |
| `uMV` | `modelview_matrix_stack[top]` — `interpreter_zelda3d_commands.cpp:99`, filled at `zelda3d_sdl3gpu_pass.cpp:493` | **MODEL only.** The stack base is identity; the Zelda3D draw sites push the actor's world matrix with `G_MTX_MODELVIEW\|G_MTX_LOAD`. |
| `uMP` | `mRsp->MP_matrix` — `interpreter_zelda3d_commands.cpp:124` | `MP_matrix = M_stack_top · P_matrix` (`interpreter_rsp.cpp:145`), and `P_matrix` is the **camera**: `guPerspective` then `guLookAt(eye, at, up)` (`soh/src/code/z_player_lib.c:2186-2193`). |

The same fact is stated independently in the other backend's own comment
(`backends/unified_shader.cpp:209-211`: "the camera is folded into uMvp"), and `vWorld` is *used* as a
world position by the ProjectionMap arm that just landed (`worldN` at line 242), which is only correct
because `uMV` really is the model matrix.

So the host's sphere coordinate is `0.5 * (M · n).xy + 0.5` and retail's is
`0.5 * (uModelView · Palette · n).xy + 0.5` — retail's per-object transform is the *pair*
(`uMatrixPallet` row 0, then `uModelView`), the host's is the single `M` that stands in for both, and
the camera is inside one of retail's two and inside neither of the host's. **They differ by the camera
rotation.** The three sphere branches
(coordinator 0 at line 198, coordinator 1 at 215-218, coordinator 2 at 257-261) all use `ns`, and
`backends/unified_shader.cpp:216-218` is the same expression in the second backend, so both need the
same correction.

**Why nothing else broke.** `vNrmView` (line 156) feeds only lighting, and
`zelda3d_sdl3gpu_pass.cpp:524-539` transforms the light direction by the *same* `mat3(uMV)`, so every
`dot(N, -L)` is invariant under the choice of frame. The sphere map is the one consumer of the
normal's *absolute* direction, which is why this defect is isolated to it. (Retail's own lighting is
self-consistent in its own frame: words 79-81 are `dp3(-LightDir_i, r14)`.)

## 3. Magnitude, direction, and visibility

Write `N` for the world normal. For a camera yawed by `θ` about world up, `Δuv = 0.5·[(V·N).xy − N.xy]`
gives `Δu = 0.5·[(cos θ − 1)·N.x − sin θ·N.z]`, `Δv = 0`; pitch `φ` lands in `Δv` the same way. So
**yaw error goes into u, pitch error into v**, and both scale with `sin` of the camera angle:

| camera angle from identity | `\|Δuv\|` for a normal facing the camera | 64×64 map | 128×128 map |
| --- | --- | --- | --- |
| 10° | 0.087 | 5.6 texels | 11 texels |
| 30° | 0.250 | 16 texels | 32 texels |
| 45° | 0.354 | 22 texels | 45 texels |
| 90° | 0.500 | 32 texels | 64 texels |

It is **neither a diffuse nor a specular lighting error** — it is the *environment-map lookup
direction*. Retail samples the map in a frame that turns with the camera, so a reflection stays glued
to the viewer (what an env map must do); the host samples it in a world-anchored frame, so the
reflection stays glued to the world while the camera moves around it. On a smooth gradient map it
reads as the highlight sitting in the wrong place; on a small tiled map as the wrong 16-64 texels.
It is a **static** error, not a temporal one: a single gameplay screenshot at a rotated camera shows
it, so this is cheap to confirm and cheap to falsify. At zero camera rotation it is exactly zero,
which is the whole reason it survived (§4).

OoT's camera yaw is player-controlled and unbounded, and the pitch is non-zero whenever the player
tilts the camera — which is most of a fight or a cutscene. So in gameplay this is not a subtle bias: it
is a systematic quarter-of-the-map shift at moderate yaw.

## 4. Why verification missed it — the specific capture

The only oracle evidence for this arm is the title wordmark overlay.

* **Retail side:** `title_logo_us.cmb` (model 2015), draws 75-93 of the cached cs1093 frame. I decoded
  `modelView0..2` from Azahar's own `vsuni_log` for **all 24 draws (n=75..98)**: rows 0-2 are the
  identity on every one, row 3 is `(0, 2.39058, 0)`. And by §1(a) the camera rotation on those draws is
  identity too. World normal == view normal there, to the bit.
* **Host side:** those draws never even execute the fallback. `title_logo.cpp:544-545` calls
  `Zelda3D_GL_SetSphereMapNormalMatrix(modelId, IDENTITY)` — a hard-coded literal, because the host's
  own overlay placement is a `RotateX(180)` composition that the game's `uModelView` is not. That
  override has exactly **one call site in the whole tree**, so the `mat3(uMV)` branch that every other
  draw in both games takes has **zero** oracle-verified draws.
* **The comment that encodes the mistake** (`zelda3d_sdl3gpu_shaders.cpp:189-191`, plus the same claim in
  `zelda3d_sdl3gpu.h:41-43` and `zelda3d_sg_ubo.h:95-101`) reads "host uMV carries the same transform",
  and its stated justification — "Native composition may differ, so an exact oracle-derived matrix
  can be transported independently (title wordmark: identity)" — is only ever *checked* on the one
  frame where the two frames coincide.
* **Gameplay has no uniform evidence at all.** I replayed the cached gameplay command list
  (Gravekeeper's Hut, entrance 781, draw 4, 17412 words): it writes **66 distinct uniform indices and
  not one of `c0..c7` or `c76..c79`**. So there is no gameplay frame in the cache whose `uModelView`
  anyone has read — the blind spot is total, not partial.

The generalisable lesson, because it will hide the next one too: **the one capture used to verify a
matrix-identity claim had that identity in it.** A verified identity is not evidence that the
substitution is valid; it is evidence that the substitution was untested.

## 5. What would settle it (no instrumentation required)

`vsuni_log` already prints `modelView0..3` per draw (Azahar `pica_core.cpp:401`), and
`uProjection` needs no log at all — `tools/pica_command_list.py` replays it offline once a command list
is captured. So this needs **one capture, not a probe**:

1. Boot the embedded oracle to a **gameplay** scene (`tools/harness_cli.py` `gameplay` + `warp`, which
   needs a loaded save — [issue #23](0023-embedded-oot3d-oracle-cannot-reach-its-boot-hand.md)) at a
   moment when the camera yawed well away from the game's world axes (>30°), and capture
   `vsuni_log` plus the command list (`tools/title_oracle_probe.py`'s capture path works off the title;
   the same harness REPL drives it in gameplay — see `tools/soh3d_harness.py` / `harness_cli.py`).
2. Pick a draw whose bound material declares coordinator mapping 3 (the host already prints
   `coord0/coord1/coord2=` per draw, and `sgdump` prints `coordMap`), so the draw is known to be
   sphere-mapped rather than inferred from a draw index.
3. Compare, **for that same draw**, the retail `uModelView` rows 0-2 against (a) the host's `uMV`
   rotation for the same object and (b) the N64 camera rotation (readable host-side from the actor's
   world matrix and `soh_camera_state`, no new probe).

What it distinguishes, exactly:

* `uModelView.rows ≈ camera · model` (non-identity where the model's own rotation and the camera's
  differ) → **confirmed**: the host is dropping the camera rotation from the sphere normal. The fix
  belongs in the sphere normal only; `uMV` must stay world-space, because `vWorld` (the ProjectionMap
  `p`) and the lighting's `N`/`L` pair both depend on that today.
* `uModelView.rows ≈ model` with the camera elsewhere → **refuted**, and the row's `uInvView` reading
  is then what needs re-deriving (see §6). The host would be right and only the comment is wrong.

Either outcome is cheap. What is *not* cheap is pretending the title capture decided it.

## 6. Two corrections to this row's own evidence, found while re-deriving it

The row (and `oot3d-decomp/docs/cmb_texcoord_mapping.md` §11) leans on `uInvView` measurements I could
not reproduce. My replay is validated against all 28 vec4 uniforms Azahar logs for that draw (§1a), so
these are findings, not doubts:

1. **"uInvView is a full rigid matrix on the wordmark draw (draw 77)" is not reproducible at draw
   77's cursor.** `c76..c79` read the IDENTITY there, and for every draw in that command list
   (n=75..98 all sit in the uniform block that begins at word 362, which writes `c76 = (1,0,0,0)`).
   The rigid values the row quotes **do** exist in the list — at words 122-135 and 262-275 — but no
   logged draw cursor falls inside those blocks. So "uInvView is written per draw, interleaved" is
   right, and "on the wordmark draw it is a full rigid matrix" is not.
2. **"the product uInvView · uModelView is a rigid transform with translation (-2314.03, -1049.86,
   -7299.79), an ordinary object placement" is the translation of `inverse(uInvView)`, not of the
   product.** With the measured `t_U = (-602.811, 86.432, 7705.71)` and `t_M = (0, 2.39058, 0)`, the
   product's translation is `R_U·t_M + t_U = (-602.93, 88.80, 7706.00)`; the quoted triple is
   `−Aᵀ·t_U = (-2313.7, -1052.4, -7300.1)`, i.e. the origin expressed in `inverse(uInvView)`. Since
   `inverse(uInvView)` being rigid says nothing about `uModelView`, the row's conclusion "so on this
   draw uModelView = view · model and uInvView = inverse(view)" **does not follow from it**.

Neither correction weakens this finding — §1(a) establishes the camera's location in `uModelView`
without `uInvView` at all, which is why it is stated that way. But §11's `uInvView` paragraph should be
marked as un-reproduced, and any future work that leans on it should re-derive it first.

## 7. Reach, and a caveat about the numbers

From `oot3d-decomp/docs/cmb_texcoord_mapping.md` §5, counting only texture units a combiner actually
samples (so: units that can reach a pixel): **OoT3D 518 method-3 declarations** (coordinator 0: 31,
coordinator 1: 466, coordinator 2: 21) and **MM3D 1270** (tex0 44, tex1 1121, tex2 105). Every method-3
material on both games except the title wordmark's overlay takes the wrong-frame branch.

Caveat worth recording: those figures came from `tools/tev_corpus_survey.py`, and `tools/cmb_corpus.py`
beside it. **Both were deleted in `1607162c` and neither resolves anywhere in the tree** (as do
`tools/pica_shader_uniforms.py` and `tools/test_pica_shader_uniforms.py`, which §10 names as the
uniform-replay validator). What survives and is sufficient for this finding: `tools/pica_command_list.py`
and `oot3d-decomp/tools/shbin_disasm.py`. Re-authoring the corpus survey is a separate chore; the
reach numbers here are quoted, not re-measured.

**Runtime reach is still unmeasured** — how many of those 518/1270 declarations are on screen in a
given gameplay frame is not known. The only cached host log with a `coord1=3` row at all is the title
one (`scratch/logs/title_cs1093_drawlist.log`, 9 of 102 draws, models 2010/2015 = wordmark decoration
geometry), so no cached host log demonstrates a gameplay draw taking the branch either.

## 8. Adjacent, and deliberately not claimed

The retail sphere arm multiplies its `(u, v, 1, 1)` source by `TexMtx_k` rows 0/1 (a `dp4` over four
components), while the host applies a 2D affine `(u - trans) · scale` from the CMB coordinator
(`uTex0Xf`/`uTex1Xf`/`uTex2Xf`). These agree for a diagonal TexMtx with a constant term, which is what
the one measured capture has (`texMtx1` = `(1,0,0,0),(0,1,0,0),(0,0,1,0)`), and what §11 measured for
the *method-4* population. Whether the sphere population's TexMtx rows are equally diagonal is **not
measured** and is not asserted here. Same caveat for `o5 QUATERNION` (words 63-75), which is a second
consumer of `r14`'s absolute direction: the host emits no `o5` at all, so it cannot be a pixel
difference unless OoT3D's fragment program reads the tangent attribute (unverified).
