# Wordmark sphere-map normal transform (corrected 2026-08-30)

This journal replaces the false 2026-07-14 conclusion that the title wordmark's
CameraSphereEnvMap coordinators consume the live cutscene-camera view basis. Commit `a20566da`
implemented that inference and improved two screenshots, but no captured CmbVShader c4-c6 state
supported it. The improvement was correlation, not ground truth.

## Binary and GPU ground truth

`/CmbVShader.shbin` names c4-c7 `uModelView`. Words 59-61 transform the post-skinning normal by
c4-c6. The mapping-method-3 helper at words 295-296 computes:

```
sphereSource = 0.5 * transformedNormal.xy + 0.5
uv = TexMtx * sphereSource
```

The cache-backed cs1093 / Azahar-frame-2010 uniform capture records every wordmark draw 75-87 with
c4-c6 exact identity, coordinator-0 texture matrix exact identity, and mapping method 3. The title
camera basis is absent from this shader input. The decompiled title placement's identity rotation
is consistent with the live upload.

The host title is composed through a native orthographic placement matrix containing a compensating
RotateX(180), so blindly using host `mat3(uMV)` is not equivalent either. The renderer therefore
carries a narrow `SphereMapNormalMatrix` channel: an exact copy of the target CmbVShader c4-c6
normal transform, independent of host placement. The title owner supplies identity.

## Coordinator transform and texture-space Y

The 2026-07-14 work correctly established two independent facts that remain live:

- sphere source coordinates still pass through their coordinator's own texture matrix; and
- the uploaded host texture orientation requires the same final `1-v` conversion as ordinary UV
  coordinates.

Those facts were retained. Only the source normal matrix changed from the inferred camera basis to
the captured CmbVShader c4-c6 identity.

## Selected-fragment verification

At cs1093, host draw88 corresponds to oracle draw86. With identical PRIMARY and authored TEV
already proven, 412 clean selected host pixels changed:

```
inferred camera basis: TEX0 = (148,28,16)
captured c4-c6 identity: TEX0 = (209,42,58)
```

The decoded RGB565 center 2x2 average is `(208,42,57.25)`, so the SDL linear-filter result is the
expected hardware sample. Azahar's software rasterizer returns `(206,40,49)` because its source
explicitly leaves min/mag filtering as a TODO and truncates `(0.5,0.5)` to texel `(64,63)`.
Instrument I043 is therefore distrusted for filtered sample values; no half-texel imitation was
added.

The full derivation, immutable artifact key, and cache/checkpoint workflow are recorded in
`2026-08-30-title-unified-cmb-state-contract.md` and `oot3d-decomp/docs/title_logo_actor.md` §6.7.
