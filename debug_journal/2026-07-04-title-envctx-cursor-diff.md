# 2026-07-04 — title envCtx cursor-diff pinned 3 candidate offsets

Follow-through from `2026-07-04-title-envctx-re-progress.md` (attack #3)
and `2026-07-04-title-envctx-re-ghidra-attacks.md`. The Ghidra static-
analysis path was blocked by Thumb-2 false positives; pivoted to the
dynamic cursor-diff attack — faster to converge.

## Method

`scratch/envctx_cursor_diff.py`:

1. Boot the harness (Az at title, gPlayState populated).
2. For each cursor in `[100, 400, 700, 900, 1200, 1600, 1900, 2100,
   2400, 2800]`, `force titletime <N>` and run 90 frames to let the CS
   dispatcher process any SET_LIGHTING that fires.
3. `mem` dump play struct at 0x0871E840 + 0x1000..0x8000 (28 KB).
4. For each byte offset, compute the set of values across all cursors.
5. Filter to bytes taking 2..5 distinct small integer values (<32),
   i.e. plausible u8 slot indices.

`scratch/envctx_cursor_trace.py`: trace each surviving candidate byte's
value trajectory across a denser cursor grid `[50, 200, 400, 600, 750,
900, 1100, 1400, 1700, 2000, 2100, 2400, 2700]` to see which one
correlates with the known title-demo shot cuts at ~cursor 755 and ~2015.

## Findings

Out of 168 varying bytes, 10 fit the u8-slot-index profile. Trace:

```
cursor  +0x22b9  +0x2dda  +0x2e06  +0x31da  +0x324d  +0x5bf5
    50         0         2         2         8         8         0
   200         0         2         2         8         8         0
   400         0        13        13         8         8         0
   600         0         2         2         7         7         0
   750         1        13        13         7         7         1
   900         1         2         2         9         9         1
  1100         1        13        13         9         9         1
  1400         1         2         2         8         8         1
  1700         1        13        13         8         8         1
  2000         1         2         2         8         8         1
  2100         2        13        13         7         7         2
  2400         2         2         2         7         7         2
  2700         2        13        13         6         6         2
```

Three offset FAMILIES:

- **`play+0x22b9` + shadow `play+0x5bf5`**: 0→1→2 monotonic. Values
  {0,0,0,0, 1,1,1,1,1,1, 2,2,2} — perfectly aligned with the known
  shot-cut cursors at ~755 (0→1) and ~2015 (1→2). This is
  **almost certainly `csCtx.currentShot`** or an equivalent shot
  counter, NOT `envCtx.unk_BF` (spot00 slot 0 has
  ambient=(2,1,109) — very blue — which doesn't match Az's observed
  ground color of green (33,51,27) at cursor=650 inside shot 0).

- **`play+0x2dda` + shadow `play+0x2e06`**: toggles {2, 13} on cursor
  parity within each shot. This is either a lerp-target-slot (Az's
  environment update pre-computes NEXT slot to interpolate to?) or a
  buffer alternator. NOT a stable per-shot value.

- **`play+0x31da` + shadow `play+0x324d`**: {6, 7, 8, 9} — takes 4
  distinct values. Trajectory not neatly per-shot, but the value RANGE
  is a plausible spot00 palette slot range. Shadow copy at delta
  +0x73 (=115 bytes), same 4 values. Best candidate for
  `envCtx.unk_BF` OR `envCtx.unk_BD` (the "prev slot" the N64 code
  keeps for interpolation).

## Cross-check via SoH palette

`Shipwright/soh/src/zelda3d/zelda3d_scene_lighting.inc` spot00 slots:

```
slot 0: amb=(  2,  1,109)  l1col=( 63,  0,174)  ← very blue (night/moon?)
slot 1: amb=( 61, 72, 72)  l1col=(229,229, 99)  ← grey-warm (dawn?)
slot 2: amb=(109,  0,  0)  l1col=(255,255,255)  ← red-only (sunset?)
...
slot 6: amb=( 99, 72, 72)  l1col=(160,198,198)
slot 7: amb=( 40, 72, 72)  l1col=(109,109,130)
slot 8: amb=( 79, 72, 72)  l1col=( 20, 40, 99)  ← ground-shot fit?
slot 9: amb=( 61, 72, 72)  l1col=(150,150,140)
```

At the pinned cursor=650 (Shot 0, mostly-static ground-level, journal
`2026-07-04-title-parity-pinned650.md`), Az's bot-⅓ ground RGB is
(33, 51, 27). SoH's shader after 072c01f5 drops the shade-compound so:

    ground = t * vColor * (sceneAmb * matAmb) * combScale

For grass (matAmb=(1,1,1), combScale=2) reducing to `ground = t *
vColor * sceneAmb * 2`. Az ground green-heavy (G>R>>B) implies
sceneAmb G>R>>B. Of the {6,7,8,9} range, none is a clean green-heavy
ambient — but slot 9's amb=(61,72,72) is the closest to
grey-with-slight-cool-tint, and combined with l1col=(150,150,140)
diffuse contribution could yield a mixed green after texture and
vertex-color modulation.

The 0x31da value at cursor 600 (inside Shot 0) is **7** —
amb=(40,72,72). Neither slot 7 (blue-ish grey) nor slot 8 (yellow-blue)
matches a green ground perfectly. Verifying which of {6,7,8,9} is
envCtx.unk_BF vs a related sibling field requires either:

1. Reading the palette runtime copy at 0x099d7284 + N*22 to see which
   slot's values FLOW into the shader-visible ambient (the value
   `Zelda3D_GL_SetLightParams` pushes to `gZelda3dAmbient`), OR

2. Watching writes to the 0x31da offset via the harness's watchhook
   at title-cs time — the writer PC's enclosing fn IS
   Cutscene_Command_SetLighting, resolving the RE simultaneously.

## Concrete next steps

1. **Extend `envctx_cursor_diff.py`**: at each cursor, also read the
   raw ambient bytes from Az's palette runtime copy at 0x099d7284
   PLUS from SoH's `SohState_Lighting`. If Az's shader-visible ambient
   at cursor 650 equals `spot00[8].amb`, the shot 0 slot is 8; then
   verify play+0x31da == 8 at that cursor.

2. **Watch writes to play+0x31da via harness**:
   ```
   watch 0x08721A1A 1
   force titletime 700
   run 200
   force titletime 800   # cross shot cut
   run 200
   hits 0x08721A1A       # writer PC + LR + arg regs
   ```
   The writer's PC lies inside CS_Command_SetLighting (or the fn
   that dispatches it). The strb immediate = envCtx.unk_BF's
   play offset if this candidate is the real one.

## Session artifacts

- `scratch/envctx_cursor_diff.py` — sweep + diff
- `scratch/envctx_cursor_trace.py` — per-cursor trajectory for the top
  candidates + 32-byte neighborhood dump

## Falsified

- `play+0x22b9` = envCtx.unk_BF → **NO**. Values (0,1,2) don't map to
  Az's observed ground color at cursor=650. This is a shot counter,
  not the slot index. `play+0x5bf5` is its shadow.

## Still open

- `play+0x31da` vs `play+0x2dda` — which one is envCtx.unk_BF (if
  either). Next-session watchpoint attack resolves this.
