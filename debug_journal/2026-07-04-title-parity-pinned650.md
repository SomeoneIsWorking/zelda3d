# 2026-07-04 — title-demo parity at pinned cursor=650

## Approach

Per user directive (no eyeballing): pin a specific title-demo cursor on
both engines, verify structural parity (camera basis, cursor), then
close residual divergences by RE + port. Cursor=650 chosen from Shot 0
of Az's demo loop (mostly-static ground-level shot; eye stable at
~(-4072, 58, 5216), 155 frames of stability).

Pin primitive: `force titletime 650` writes Az cursor 0x0054CC3C AND
SoH csCtx.frames = 650.

## Structural verification at cursor=650

`compare firstdiv` after pin:

| Field | Az | SoH | Δ |
|---|---|---|---|
| eye | (-4072.24, 57.81, 5216.14) | (-4071.49, 57.81, 5217.30) | 1.39 |
| forward | (-0.868, 0.195, 0.458) [+0x24] | (at−eye)/‖at−eye‖ = same | **0.0001** ✓ |
| up | (0.212, 0.977, -0.014) | (0.21, 0.98, -0.01) | 0.0006 ✓ |
| fov | 48.803° | 48.80° | ~0 ✓ |
| sceneNum | 0x0051 | 0x0051 | ✓ |
| rider pos | (-6154, 29, 4943) | (-6043, 42, 5007) | **128.6** |

Camera basis is at parity. Rider pos off by 128 units — a smaller
residual than the visual ground defect below, deferred.

### Metric bug fixed en route

`Az_ReadTitleCameraBasis::dir[]` was reading VA `TITLE_CAM_BASIS_VA +
0x0C` which is the RIGHT axis per the JIT-caught view-matrix writer
(FUN_004235B8 @ 0x004235d4). The actual view forward lives at
`TITLE_CAM_BASIS_VA + 0x24`. Prior |Δdir| = 1.4143 was reading right
vs SoH's forward — √2 apart because the two vectors are orthogonal by
construction. Fixed in soh3d commit a136c4e0.

## Bottom-third defect — ground rendering ~5.5× too dark

At pinned cursor=650, structural cam parity, sceneNum parity:

| Region | SoH color (RGB) | Az color (RGB) | SoH lum | Az lum |
|---|---|---|---|---|
| Top ⅓ (sky) | (57, 54, 80) | (44, 43, 77) | 63.7 | 54.7 |
| Bot ⅓ (ground) | (6, 7, 4) | (33, 51, 27) | 5.7 | 37.0 |

Bot ⅓ = Hyrule Field grass on Az (green-brown), SoH renders near-
black. 28.8% of frame has Az>30 lum but SoH<15 lum — the missing
ground.

### Not letterbox

Added REPL `soh_letterbox` reading `ShrinkWindow_GetCurrentVal()`.
At pinned title: **letterbox = 0**. Not the cause.

### Not dayTime

A/B tested removing the `gSaveContext.dayTime = 0x0000` force in
`Zelda3D_ApplyTitleCam`:
- With midnight force: SoH bot ⅓ = R6G7B4 (dim)
- Without midnight force (natural title-cs env): SoH bot ⅓ =
  R6G7B4 (**unchanged**)
Sky brightness varied — top ⅓ went R57G54B80 → R76G80B88 — but
ground stayed dim in both. dayTime affects sky bloom, not the
ground defect. Reverted.

### Is OoT3D CMB the mesh at fault?

A/B tested `ZELDA3D_SCENE=0` (fall back to N64 mesh) vs `=1` (OoT3D
CMB):

| ZELDA3D_SCENE | SoH bot ⅓ RGB | SoH bot ⅓ lum |
|---|---|---|
| 1 (OoT3D CMB, default) | (7, 7, 6) | 7.0 |
| 0 (N64 fallback) | (20, 27, 152) | 39.1 |

**Confirms Zelda3D_DrawRoomGL is the too-dim path.** Fallback N64
mesh renders at proper luminance (though blue-tinted). OoT3D CMB path
is 5.5× dimmer.

## Root cause — shader compound-dim

`Zelda3D_DrawRoomGL` computes a scene tint via `Zelda3D_SceneTint`:

```c
tint[i] = (ambientColor[i] + 0.5 * (light1Color[i] + light2Color[i])) * 1.0
```

At settled title: `ambient=(40,35,77) light1=(90,100,180)
light2=(60,60,60)` →
`tint = (40+75, 35+80, 77+120) = (115, 115, 197)`. Then
`gSPZelda3DDraw` emits with `tint` in `uTintSkin`.

Fragment shader (`Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`):

```glsl
vec3 shade = ubo.uTintSkin.xyz;                    // = tint/255 ≈ (0.45, 0.45, 0.77)
vec3 rgb = t.rgb * vColor.rgb * shade;             // texture × vertex × tint
if (ubo.uMatConst.a >= 0.5) rgb *= ubo.uMatConst.rgb;
if (ubo.uParams.y < 0.5) {                         // scene / room path
    if (ubo.uAmbient.w > 0.0)
        rgb *= ubo.uAmbient.xyz;                   // ← additional dim (task #16 port)
    rgb = clamp(rgb, 0.0, 1.0) * ubo.uExtra.w;
}
```

`ubo.uAmbient.xyz = gZelda3dAmbient * grp.matAmbient`. With
`gZelda3dWorldLit = 1` (default) and grass `grp.vertexLighting = 1`,
`ubo.uAmbient.w > 0` and the compound-dim fires: `rgb ~= t.rgb *
vColor.rgb * (ambient+lights) * (envAmb * matAmb)` — TWO independent
ambient factors, both dark.

Estimating with plausible values:
- t.rgb ~ 0.5 (grass texture)
- vColor.rgb ~ 0.7 (baked vertex color)
- shade ~ 0.45 (scene tint)
- uAmbient ~ 0.16 (envAmbient scaled × matAmbient)

Product: 0.5 × 0.7 × 0.45 × 0.16 = **0.025 = 6.4/255** — matches
observed R6-7. Fully explains the compound-dim.

## Where the fix landing goes

Az's PICA200 fragment lighting at title does NOT do this double
compound. The OoT3D-authentic model uses just
`saturate(sceneAmb * matAmb + sceneDif * matDif * NdotL) * bakedColor`
(per the shader comment on line 236-240) — one ambient factor, no
scene-tint compound.

The task #16 port added `uAmbient` modulation but kept the pre-existing
`shade = uTintSkin.xyz` multiplication. That's the source of the
double-dim.

Two candidate fixes, both need RE of Az to be sure which is authentic:

1. **Drop `shade` when uAmbient path is active**: for scene/room draws
   (`ubo.uParams.y < 0.5` && `ubo.uAmbient.w > 0`), skip the initial
   `rgb *= shade` — just do `rgb = t.rgb * vColor.rgb * uAmbient.xyz *
   uExtra.w`. That's what the shader comment describes as the
   OoT3D-authentic scene-vertex-lit model.

2. **Fold shade INTO uAmbient**: set `uAmbient.xyz = shade * envAmbient
   * matAmbient` at UBO fill time, then the shader single-multiplies.

## Session tools added

- REPL `soh_env`: daytime + skybox indices + live ambient/fog
- REPL `soh_letterbox`: current ShrinkWindow val
- `Az_ReadTitleCameraBasis` fix: read forward from +0x24 (was +0x0C)

## Session artifacts (scratch/, gitignored)

- `az_title_state.csv` — 400-sample per-frame Az title-state log (eye,
  dir, up, rider, cursor). Two shot cuts observed at cursor 755 and
  2015. Shot 0 characterized as ground-level static (cursor 595..750,
  eye_y=58, dir_y=0.086).
- `pinned650_soh.png`, `pinned650_az.png`, `pinned650_diff.png` — the
  pinned frames + diff map. Kept for the follow-on shader RE arc.

## Deferred

- Rider pos |Δ|=128u
- N64 logo overlay top-right (~40px, minor)
- Sky top-⅓ tint mismatch (once ground fix lands, revisit)
- Az envCtx offset in OoT3D PlayState (3ds side of `compare lighting`
  currently n/a)
