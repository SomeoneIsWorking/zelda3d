# 3DS hi-res texture replacement — status & plan

**Goal:** when Zelda3D draws OoT3D/MM3D content, substitute higher-resolution textures from a
Citra/Azahar-style pack (filenames keyed by Citra's legacy CityHash64) instead of the ROM's
CTXB textures.

## What already exists (verified 2026-07-01, soh3d source map)

The **CMB model** hi-res path is **fully implemented and live** in soh3d — the "never
implemented" report was stale for models. End to end:

- Pack: `<engine>/textures/` — Henriko Magnifico's 4K OoT3D pack, ~2147 PNGs under the PAL
  title id `0004000000033500`, Citra dump naming `tex1_<W>x<H>_<16hexHASH>_<fmt>_mip0.png`,
  `pack.json` (`use_new_hash:false` → legacy hash).
- Code (mirrored in this repo's `src/cmb3d/`):
  - `asset/texpack.cpp` — `TexPackLookup(hash,w,h,rgba)`; pack root via `SOH3D_TEXPACK` / `./textures`
    / next to the ROM.
  - `asset/cityhash.cpp` — `CityHash64` (Citra's v1.1 variant).
  - `asset/pica_texture.cpp:178` — `PicaLegacyHashBytes` rebuilds the exact bytes Citra hashes.
  - Wired at `soh3d_model.cpp:258-264` (compute hash → lookup → replacement RGBA+dims), uploaded
    at `libultraship/src/fast/soh3d_sdl3gpu.cpp:299` (`SoH3DRenderer::uploadTexture`). UVs are
    normalized so a 4K replacement is a drop-in.

So for models: **no code gap** — point at the pack (`SOH3D_TEXPACK`) and it works.

## The real gap (soh3d KANBAN #17, To Do)

**World/scene textures** rendered through the N64 Fast3D opcode path have **no** texpack hook.

- Code: `libultraship/src/fast/backends/gfx_sdl3gpu.cpp:1512` `GfxRenderingAPISdl3Gpu::UploadTexture`
  receives already-decoded N64/CI texels — the original CTXB/Citra hash is no longer in hand, so
  the legacy hash can't be recomputed at this point. The hash must be computed at the scene-CTXB
  decode site and threaded down (or a separate keying scheme used), mirroring `appendTextures`.
- Assets: needs the actual hi-res *scene* image assets (or a procedural upscaler) — likely
  user-provided. The Henriko pack is character/creature/UI-heavy; scene coverage varies.

## Plan

1. **Models, both games:** confirm the existing model path lights up for OoT3D (point
   `SOH3D_TEXPACK` at `soh3d/textures`) and that the **MM3D** model path uses the same
   `texpack`/`cityhash`/`pica` units in `src/cmb3d` (they are game-neutral). *Verify:* hash hits
   on real MM3D textures via a probe over `src/cmb3d`.
2. **Scene path (#17):** compute the legacy hash where scene CTXB is decoded, `TexPackLookup`,
   and pass replacement RGBA+dims into `gfx_sdl3gpu.cpp:1512`. *Verify:* a known hi-res scene
   texture appears in-world.
3. **Assets:** decide pack sourcing per game (bundle/point-at for OoT3D; MM3D pack or upscaler).

This work lives mostly in soh3d's renderer + the shared `src/cmb3d` core; it composes with the
MM integration (`docs/MM_INTEGRATION.md`) since both games share that renderer.
