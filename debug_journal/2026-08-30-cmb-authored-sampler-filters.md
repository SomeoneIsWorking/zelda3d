# CMB-authored sampler filters close the BossFd2 smoothing discriminator

## Root cause

The SDL3GPU CMB route parsed texture index and wrap state but discarded each binding's
minification and magnification fields. It then selected linear min/mag plus trilinear mipmaps for
every CMB texture. This was not the state authored in the game binary: all active bindings in
`valbasiagnd.cmb`, including material 1's independent TEX0 and TEX1 bindings, are
`0x2601/0x2601` (`GL_LINEAR` with no mip selection).

This explains the observed mechanism without a BossFd2 special case. The texture pack supplies a
high-contrast replacement mip 0 and declares `skip_mipmap`; Azahar generates replacement lower
levels but still obeys the PICA sampler state. Unconditionally sampling those levels on the host
averaged the fire pattern into dark strips. The recovered draw chain
`0020A3B0 -> 0035E240 -> 004C7AB0 -> model vtable 003FED90` and generic material-copy path
`0040C6C8 -> 0030487C` contain no body-wide brightness multiplier.

## Port

- `CmbMaterial` now parses min/mag at binding offsets `+4/+6` for TEX0, TEX1, and TEX2.
- `Zelda3DGlGroup` and the SDL3GPU submission group preserve those values per texture unit.
- `zelda3d_sampler.h` is the one neutral GL-enum-to-filter/mipmap resolver shared by the backend
  and pure regression tests.
- The SDL3GPU sampler cache key now includes min, mag, mip mode, wrap modes, and max LOD.
- The obsolete additive/single-level heuristic is removed; authored CMB state owns the choice.

The ROM-wide falsifier found five real minification enum values across 12,888 active bindings:
7,150 `LINEAR_MIPMAP_NEAREST`, 4,750 `LINEAR`, 973 `LINEAR_MIPMAP_LINEAR`, 13 `NEAREST`, and
2 `NEAREST_MIPMAP_LINEAR`. Treating the field as padding or forcing one sampler cannot reproduce
that corpus.

## Evidence

- Real-ROM focused tests pass for the enum resolver and BossFd2 material-1 propagation.
- The Clang build links `lus_tests`, `soh_core`, `mm_core`, and `zelda3d_app` with the changed path.
- A shipping headless host-only run at entrance `0x305`, actor `(0,-850,0)`, held
  `vba_search`, and camera `(0,-871.249,700)->(0,-971.249,0)` shows the restored high-frequency
  orange fire pattern in `scratch/screenshots/fd2_authored_sampler_front_after.png`.
- The oracle reference remains the previously cached
  `scratch/screenshots/fd2_paired_xy.oracle.png`; this verification did not launch Azahar.
- Exact draw-29 oracle fragments, state, frame, and reduced footprint remain in
  `scratch/oracle_cache/26f57176963dad7a_6510135ae6c38599_p37-345049fb_tp2149-e714eb17be1b/`.

The full renderer suite also caught a stale std140 regression assertion from the earlier selected
fragment-probe work: `uDebug` is present in both `SgUbo` and the shader common block at byte 656,
so `uBones` correctly begins at 672, but the test still expected the pre-probe 656 offset. The
layout was internally consistent; the test now asserts both fields and therefore guards the real
contract.

The controlled host screenshot is visual mechanism evidence, not a parity closure: its shipping
camera FOV/pose is not the exact paired-harness checkpoint. BossFd2 remains open until a cached
oracle control checkpoint can be replayed host-only for a like-for-like image metric and the
natural action sequence is exercised.
