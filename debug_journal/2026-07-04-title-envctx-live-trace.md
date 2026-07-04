# 2026-07-04 — Az title-demo cycles slots {6,7,8,9}, no fixed value works

Closes the "which slot does Az use at title" open question. Live
harness read of the RE'd 3ds envCtx across the title-demo cursor:

```
cursor=  50: slot=8   (shot 0 early)
cursor= 300: slot=8
cursor= 600: slot=7   (shot 0 late)
cursor= 900: slot=7   (shot 1)
cursor=1300: slot=6
cursor=1700: slot=9   (shot 2)
cursor=2100: slot=9
cursor=2500: slot=8
```

envCtx base = **play + 0x3135** (Az live: 0x08721975), unk_BF at +0xA5.
See `oot3d-decomp/docs/env_context_layout.md` for the RE trail and
Environment_Update body decomp.

## Why the earlier lightslot sweep was wrong regardless of the value

The reverted `SOH3D_TITLE_LIGHTSLOT` (empirical best = 12) was picked
as a fixed value at pinned cursor=650. This trace shows Az DOESN'T use
a fixed slot at title — it cycles through spot00 slots {6, 7, 8, 9}
across the three-shot demo:

| Slot | spot00 ambient  | Shot correspondence   |
|------|-----------------|-----------------------|
|  6   | ( 99, 72, 72)   | mid-demo interstitial |
|  7   | ( 40, 72, 72)   | shot 0 late / shot 1  |
|  8   | ( 79, 72, 72)   | shot 0 early / late shot 2 |
|  9   | ( 61, 72, 72)   | shot 2                |

None of these matches the empirical "slot 12" (114, 72, 72) exactly.
The best 3-shot approximation Az uses is a per-shot slot pick, driven
by CS_CMD_SET_LIGHTING firings at each shot boundary. A single fixed
override is fundamentally the wrong shape.

## Cross-check against Az observed ground color

At pinned cursor=650 (inside shot 0, slot=8), Az's bot-⅓ ground RGB is
(33, 51, 27) per `debug_journal/2026-07-04-title-parity-pinned650.md`.
spot00[8] has `amb=(79, 72, 72)`. Under SoH's post-072c01f5 shader
`ground ≈ t * vColor * sceneAmb * combScale` with grass matAmb=(1,1,1)
combScale=2 and plausible texture ~0.5, vertex color ~0.7:

    R: 0.5 * 0.7 * (79/255)  * 2 = 0.217 = 55/255  (Az observed 33)
    G: 0.5 * 0.7 * (72/255)  * 2 = 0.197 = 50/255  (Az observed 51) ✓
    B: 0.5 * 0.7 * (72/255)  * 2 = 0.197 = 50/255  (Az observed 27)

G matches almost exactly. R and B are off, but Az PICA200 also adds
L1/L2 diffuse contributions that our simplified estimate ignores.
Directionally correct.

## What to port into SoH

Two options:

1. **Mirror the live envCtx** (fast to land, closes visual parity).
   Add a REPL cmd `az mirror envctx` or wire directly into
   `Zelda3D_ApplyTitleCam` — read Az's envCtx.unk_BF each frame,
   write it into SoH's `play->envCtx.unk_BF`. Simple, but keeps SoH
   dependent on the Az oracle at title.

2. **Port the CS-driven slot changes** into SoH's title-demo
   cutscene handler (proper fix). The title-demo already runs as a
   Play state on SoH; the cutscene stream includes CS_CMD_SET_LIGHTING
   entries (per the OoT format). If SoH's title-demo cutscene isn't
   firing SET_LIGHTING commands at the shot boundaries, the CS stream
   for scene 0x51/0x6B needs to include them. Ground truth for the
   {6,7,8,9,7,6,9,8} slot sequence is now measurable directly from
   Az via `compare lighting`.

## Harness change committed

Added the 3ds branch to `CompareLightingImpl` — reads envCtx from
`play + 0x3135` (falling back to `AZ_PLAY_STRUCT_VA = 0x0871E840` when
GPLAYSTATE_VA hasn't populated yet). Exposes:

```
3ds: envCtx@<va>  slot=<u8>  prevSlot=<u8>  lerpWeight=<float>  mode=<u8>
```

Note the trace showed `lerpWeight = 0.000` at every sample point.
Possible reads:
- Environment_Update just committed the transition and reset unk_D8
  (matches N64 sequence: after `envCtx->unk_D8 = 0.0f` in the shadow
  catchup block).
- OR my byte extraction for +0xC8 is off. Read32 at env_base+0xC8
  returned the u32 that I interpreted as float — sanity-checking the
  offset is a followup.

Trace also shows `prevSlot` in the 69-77 range, which don't look like
slot indices. This is the same countdown-value shape seen on the
watchpoint at +0xA7 (values 111-114 = 0x6F-0x72). So env_base +0xA6
is likely a per-frame countdown, NOT a shadow slot — the "shadow"
role goes elsewhere in the 3DS struct. Also a followup, but doesn't
block the port direction.

## Files

- `tools/soh3d_harness/main.cpp` — `CompareLightingImpl` 3ds branch
  (this commit)
- `scratch/envctx_live_trace.py` — the driver that produced the trace
