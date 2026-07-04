# 2026-07-04 — CS table dump: byte-5 hypothesis falsified

Dumped 54 bytes at `play+0x318F` (the R9 base I derived for the
supposed CS command table), with `env+0x21` (the alleged "current
entry idx") reading as `0x00000000` at the moment of the dump.

## Raw dump

env+0x21 (u32): `00 00 00 00`  → idx = 0

Entry 0 (54 bytes at play+0x318F):
```
00 00 00 05 00 65 c6 29 45 3c 82 93 c4 c5 02 7a
c3 63 63 00 29 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 01 00 00 00 6c ce
f8 4a 4a
```

Byte at offset 5 = **0x65 = 101 decimal**. Should be a slot idx (0..16
range) per my hypothesis. It isn't.

Entries 1-2 (108 bytes at play+0x318F+54):
```
3f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01  (entry 1 header)
00 00 00 94 32 08 4a 7d 18 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 fc ff 7f 3f 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 80 3f 50 00 50 00 50 00 00 00 00 00 00
00 04 00 00 00 d8
```

## Interpretation

The bytes don't look like structured 54-byte CS entries with a
"startFrame, endFrame, slot" shape à la N64 CsCmdEnvLighting. Mostly
zeros with scattered payload — reads more like an actor-state array
than a cutscene command list.

## Falsification

My earlier decomposition of the ARM at PC 0x0045e6ac..0x0045e6c0
assumed R7 in that region still holds the value from Env_Update's
prologue (`param_1 + 0x3000`). But R7 is caller-saved AND used
extensively through this 4828-byte fn — very likely reassigned to a
loop counter or another local by the palette-lookup site. So my
computation of `R0 = R9 + 54*env[0x21] + 2*R7` was wrong at the R7
term, and quite possibly at the R9 term too (if R9 was also
repurposed).

The `rsb r, r, r, lsl #3; add r, r5, r, lsl #2` pattern that produces
`palette_base + slot*0x1C` IS clearly a palette lookup. But whose
slot? Not necessarily `env[0x21]` — that was my chain-back guess and
it broke.

## Where the RE actually is

Solid ground truth:
- Env_Update = FUN_0045dd30 (4828B).
- Its per-frame slot commit at PC 0x0045efcc is a `strb r0, [r4,#0x4a]`
  writing to VA 0x08721A1A.
- The source of that r0 comes from `ldrb r0, [r4, #0xbd]` at PC
  0x0045efc8 → env-internal shadow at env+0xBD.
- env+0xBD itself is written by lerp math around PC 0x0045e4a0 that
  depends on trig(angle) — a scripted angle input at R10+0xC.
- `rsb r, r, r, lsl #3; add r, r5, r, lsl #2` = palette_base+slot*0x1C.
  R5 = *(param_1+0x3230) = the palette base pointer.

Uncertain:
- The exact register that holds the slot index at the palette lookup.
  Ghidra decomp calls it `param_2 + 0xa5` in decomp variables, but
  we've established decomp variable naming doesn't map 1:1 to ARM
  offsets in this fn.
- The identity of the fn/table populated at play+0x318F (dump above
  doesn't match a CS command list shape).
- Whether the "slot changes over the title demo" behavior we observed
  earlier (Az cycling through 6/7/8/9) is even driven by a CS command
  table, or by some other mechanism (e.g. the trig-driven light angle
  producing byte values that happen to match the 6-9 range).

## Session verdict

The Env_Update decomp is complex enough that spot-checking individual
ARM ranges + register chases hasn't converged on the CS handler
after multiple probes. The next productive session needs either:

1. A full ARM DATA-FLOW pass through Env_Update — cross-referencing
   every register's provenance from the prologue to every store site,
   in one dense sweep. That's a Ghidra-scripted pass, not a
   spot-check.

2. A DIFFERENT RE angle entirely: locate the 3DS title-demo scripted
   playback engine via the boot chain (per the user's earlier
   guidance about Az booting straight to title), then follow the
   playback dispatcher's fn table for the SET_LIGHTING command
   entry.

Committing the raw dump as durable data so the next attempt starts
from measured ground truth, not extrapolation.

## Files

- `scratch/cs_table_probe.py` — the mem read driver
