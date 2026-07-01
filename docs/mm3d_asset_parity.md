# MM3D asset-core parity — probe findings (2026-06-30)

Ground truth from `tools/cmb3d_probe` run against the real decrypted 3DS ROMs (local, uncommitted).
This is the work-list for bringing `src/cmb3d/` (written + verified against OoT3D) to MM3D
parity. **Re-run the probe after each fix** — it is the parity oracle.

## The headline: MM3D packages assets completely differently from OoT3D

| | OoT3D | MM3D |
|---|---|---|
| files in RomFS | 1944 | 1851 |
| archive format | **ZAR** (`.zar` ×461) | **GAR** (`.gar` ×160, plus `.gar.lzs`) |
| compression | none | **LzS** (per-file, by magic) |
| CMBs loaded by probe | **1387 / 1387 (100%)** | **1477 / 1477 (100%), 0 bad floats** |
| scene info | ZSI ×? | ZSI ×424 (some LzS-wrapped) |
| textures | CTXB | CTXB ×445 (same) |

OoT3D is fully green (1387 CMBs, all v6, all with skinning, 0 failures) — the migrated core
is solid for OoT3D. MM3D loads **nothing** yet because two parsers are missing.

## Gap 1 — LzS decompression (magic `LzS\x01`)  [foundational]

MM3D LZ-compresses many files. Compression is detected by **magic, not extension**: of 4
sampled `.zsi`, two begin `4c 7a 53 01` ("LzS\x01", compressed) and two begin `5a 53 49 09`
("ZSI\tShUn", raw). So the loader must sniff the 4-byte magic and inflate transparently
before handing bytes to GAR/ZSI/CMB.

Derived 16-byte header (little-endian), confirmed across two files:

| off | size | field | z2_zolashop | yousei_izumi |
|---|---|---|---|---|
| 0 | 4 | magic `"LzS\x01"` | 4c 7a 53 01 | 4c 7a 53 01 |
| 4 | 4 | type/hash (unused) | 01 00 a6 32 | 01 00 48 4b |
| 8 | 4 | **decompressed size** | 0x11bc = 4540 | 0x27e4c = 163404 |
| 12 | 4 | **compressed size** | 0x8b3 = 2227 | 0x102b9 = 66233 |

`16 + compressed_size == file_size` holds exactly (2243, 66249).

**RESOLVED + VERIFIED** (`src/cmb3d/lzs.*`). It is classic **Okumura ring-buffer LZSS**:
ring size N=4096, max match F=18, ring initialized to **zero**, write cursor `r` starts at
`N-F = 4078`. Token stream: flag byte, 8 bits LSB-first; `bit==1` → one literal; `bit==0` →
two bytes `b0,b1` with `pos = b0 | ((b1 & 0xF0) << 4)` (ring index, wraps) and
`len = (b1 & 0x0F) + 3`, copying `len` bytes out of the ring (each also written back at `r`).
Derived by tracing the first match (`b0=eb b1=f0` → pos 4075, len 3) against the known GAR
header bytes `20 00 00 00` at output offset 12. `tools/cmb3d_probe --lzs` decompresses **all
400** LzS files in the MM3D ROM with output length == declared size; inflated magics are
GAR×108, ZSI×182, ctxb×110 — i.e. compression wraps archives, scenes, and textures alike.

## Gap 2 — GAR archive parser (magic `GAR\x02`)  [RESOLVED + VERIFIED]

**RESOLVED** (`src/cmb3d/zar.cpp`). GAR is the same container as ZAR with one difference:
the per-file meta entry is **12 bytes** `{size, shortNameOff, fullNameOff}` vs ZAR's 8 bytes
`{size, nameOff}`. The full name (with the `.cmb`/`.ctxb` extension) is at meta+8. Header,
type table and data table are byte-identical. Archive detection is by **magic after
LzS-inflate**, not extension (`*.gar.lzs` files are often stored raw `GAR\x02`). The probe
opens **623 archives, 0 failures** in MM3D. (Original reversing notes below.)

MM3D's ZAR successor. Header (little-endian), from 4 sampled files:

| off | size | field | example |
|---|---|---|---|
| 0 | 4 | magic `"GAR\x02"` | 47 41 52 02 |
| 4 | 4 | **total file size** | 0x5e8 = 1512 (= file size) ✓ |
| 8 | 2 | chunk/file-type count? | 0x0004 |
| 10 | 2 | entry count? | 0x0002 / 0x001d / 0x0028 |
| 12 | 4 | offset A | 0x20 |
| 16 | 4 | offset B | 0x84 |
| 20 | 4 | offset C | 0xd0 |
| 24 | 8 | build user string | `"jenkins\0"` |

Layout (file-type table → file table → data) still to be reversed; noclip's `oot3d` GAR
reader is the reference. Many actor archives are `*.gar.lzs` (GAR inside LzS) — but the four
sampled `.gar.lzs` actor files start with raw `GAR\x02`, i.e. stored uncompressed despite
the extension. Confirm: magic-sniff, don't trust the extension.

## Gap 3 — CMB v10 (MM3D, noclip "Majora") parsing  [RESOLVED + VERIFIED]

MM3D CMBs report raw version **0x0A** (noclip maps `0x06→Ocarina, 0x0A→Majora,
0x0C→EverOasis, 0x0F→LuigisMansion`). Code dispatches MM3D on `version >= 7`. Four concrete
layout deltas from v6 had to be fixed (ground truth: noclip `src/OcarinaOfTime3D/cmb.ts`),
each found by the probe's geometry sanity check (NaN/huge position floats):

1. **Header chunk pointers shift +4** — v10 inserts a `qtrs` pointer at 0x28, pushing
   mats/tex/sklm/luts/vatr/vidx/texdata each +4 (header grows 0x44→0x48). `base = 0x2C`.
2. **Bone struct stride 0x28 → 0x2C** — v10 appends a 4-byte field (name hash) per bone.
   Same field offsets, larger stride. Wrong stride → garbage parents/scales → bone matrices
   blow up → ±1e9 verts.
3. **`mshs` mesh entry stride 0x04 → 0x0C** (noclip: Ocarina 0x04, Majora 0x0C, EverOasis
   0x10, LuigisMansion 0x58). Wrong stride mis-maps meshes to SEPDs.
4. **Index byte-base is `prm.first * 2` ALWAYS** (noclip `readPrmChunk`: "offset always in
   shorts even when indexType is byte"). Was `first * isz`; correct for UShort (why OoT3D
   stayed clean) but **half** for UByte-indexed prims → indices land outside the SEPD's
   vertex slice → ±1e9 verts. This was a latent OoT3D bug too.

SEPD attribute table is at 0x24 (same as v6; only LuigisMansion uses 0x3C) and adds
`tangent` after `normal` (already handled). **Probe result: 1477/1477 CMBs ok, 0 bad floats,
position range sane; OoT3D unchanged at 1387/1387, 0 bad (no regression).**

Material chunk (`parseMats`) still uses the v6 layout with stride 0x16C for v7+; colours and
combiner offsets are unverified against v10 but produce no geometry errors. Re-verify once a
renderer is wired up.

## Gap 4 — ZSI Majora env-lighting stride

`zsi.*` notes per-setting stride `0x1C` is "non-Majora"; MM3D's stride differs. Lower
priority (lighting variants), but tracked.

## Order of work

1. ~~**LzS decompressor**~~ — DONE (400/400).
2. ~~**GAR parser**~~ — DONE (623 archives, 0 fail).
3. ~~**CMB v10 geometry**~~ — DONE (1477/1477, 0 bad floats). Material colours/combiner
   still need visual verification once a renderer exists.
4. ZSI Majora env-lighting stride (Gap 4) — still open, low priority.
