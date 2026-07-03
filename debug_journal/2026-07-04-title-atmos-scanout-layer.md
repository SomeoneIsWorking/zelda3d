# 2026-07-04 — task #16 title-atmos: source layer pinned to VRAM 0x182447C0

## Symptom

At the settled OoT3D title-demo, Azahar's top screen shows atmospheric landscape
colours (green grass, dark hills, dim sky) with the Zelda logo composited on top.
SoH3D's title-demo renders the same 3D scene but ends up (approximately) black.

The SW-rasterizer draw log (`task16_lighting.log`, produced by a prior patch to
`sw_rasterizer.cpp`) captured every `ProcessTriangle` at title. Every landscape
triangle's TEV output is `MODULATE(PrimaryColor=(0,0,0,0), Texture0)` = 0. So
the visible landscape colours **cannot** come from any rasterized 3D draw. The
earlier hypothesis that two menu-BG CTXBs (`common_bg01.ctxb` / `ura.ctxb`)
were the atmospheric backdrop was wrong — those are UI-overlay quads (Zelda
logo + strip). See `Shipwright/soh/src/zelda3d/zelda3d.c`'s stubbed
`Zelda3D_TryDrawTitleAtmos` for the wrong-asset write-up.

## Instrumentation

Added a blit tap to `Azahar/src/video_core/gpu.cpp` (recorded in
`tools/soh3d_harness/AZAHAR_PATCH.md` "Patch 3"). Set
`SOH3D_HARNESS_LOG_BLIT=<path>` to record every `GPU::MemoryFill`,
`GPU::MemoryTransfer` (DisplayTransfer / TextureCopy), and `GPU::VBlankCallback`
(with each LCD framebuffer_config) — the three non-triangle paths through which
scanout FBs get filled.

Probe: `scratch/title_blit_probe.py` (600 frames of `run` with the env set).

## Finding

600 title-demo frames produced:
- 0 MemoryFills
- 490 DisplayTransfers (no TextureCopys)
- 600 VBlanks

At settled title (frame ≥ 61, 538 of 600 VBlanks):

```
top scanout: topFB1=0x20359DA0 topFB2=0x20313890 (RGB8 stride=720 240×400)
bot scanout: botFB1=0x203D86C0 botFB2=0x203A02B0 (RGB8 stride=720 240×?)
```

Steady-state DisplayTransfer streams:
```
   244  in=0x18000000  out=0x203A02B0 / 0x203D86C0
        inW=240 inH=320 outW=240 outH=320 ifmt=0 (RGBA8) ofmt=1 (RGB8)
        flags=0x00001000 scale=0 vflip=0 ilin=0 crop=0
   243  in=0x182447C0  out=0x20313890 / 0x20359DA0
        inW=480 inH=400 outW=240 outH=400 ifmt=0 (RGBA8) ofmt=1 (RGB8)
        flags=0x00001004 scale=0 vflip=0 ilin=0 crop=1
```

Cross-referencing scanout addrs with DT outputs:
- **Top-screen scanout = the `0x182447C0 → 0x2031xxxx/0x2035xxxx` stream.**
  The 3D pipeline is NOT feeding the visible top screen.
- **Bottom-screen scanout = the `0x18000000 → 0x203Axxxx/0x203Dxxxx` stream.**
  The 3D rasterizer output (all the black-modulated landscape triangles) is
  scanned out on the BOTTOM screen, not the top.

## What this means

The visible OoT3D title-demo top screen is a **static pre-rendered image**
living at VRAM `0x182447C0` (480×400 RGBA8 tiled). It's uploaded once —
neither MemoryFill nor any DisplayTransfer with `input_linear=1` writes to
that region during the 600-frame window. It must be memcpy'd/DMA'd there by
game code at boot from a ROM asset.

Downstream DT config progression (all with `in=0x182447C0`):
- Frames 61..63: `outW=240 crop=1 scale=0` (take left 240 cols).
- Frames 65+: `outW=480 crop=0 scale=1` (ScaleX = 2× box downsample).

Both produce 240-wide top-screen output; the settled title uses the 2×
downsample path (higher-quality).

Bottom-screen: the 3D FB from 0x18000000 (240×320 RGBA8) is DT'd 1:1 to
0x203Axxxx/0x203Dxxxx (240×320 RGB8) — the game's 3D "scene" during title-demo
lands on the bottom screen, not the top. That's consistent with the SW draws
being ~black (nothing user-visible to draw on the bottom during title-demo).

## Dump

Raw: `scratch/title_atmos_src.rgba` (768000 bytes = 480×400×4 RGBA8 tiled).
Detiled: `scratch/title_atmos_untiled.png` (8×8 Morton un-swizzle + PICA A,B,G,R → R,G,B,A).

## Next

1. Confirm detiled image = the visible top-screen atmospheric backdrop (visual
   comparison against an Azahar top-screen snapshot).
2. Locate the ROM asset that populates VRAM `0x182447C0` at boot. Two paths:
   (a) grep the ROM for the RGBA8 (or ETC1-decoded) byte prefix
   (b) instrument Az's `MemorySystem::Write<T>` (already patched for
       `MemoryWatchpoint`) to watch a page in the 0x182447C0..0x182FFFC0 range
       during boot; capture the PC of the writer, then Ghidra it.
3. Port the asset into SoH3D as a full-screen billboard drawn during title-demo
   (same slot the old wrong-asset atmos stub occupied in
   `Shipwright/soh/src/zelda3d/zelda3d.c`).
