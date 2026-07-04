# 2026-07-04 — title lighting timeline ported into SoH; Az non-determinism found

Follow-through from `2026-07-04-title-slot-divergence.md`. Now that
the divergence is pinpointed to the CS command stream, port Az's
title-demo slot progression into SoH so both engines pick the same
spot00 slot at the same cursor.

## What landed

- `Shipwright/soh/src/zelda3d/zelda3d_title_lighting_timeline.inc` —
  transcribed table of (start_cursor, end_cursor, slot) triples
  extracted from a dense Az sample (cursor 0..3200 by 20, one shot
  per pin, via `scratch/az_title_lighting_timeline.py`).
- `zelda3d.c` `Zelda3D_ApplyTitleCam` — new override that, when
  `csCtx.frames` falls inside a table row with `slot < 32`, writes
  `play->envCtx.unk_BF = slot` and `play->envCtx.unk_D8 = 1.0f`.
  Sentinel rows (0xFF/0xFE/0xFD) fall through to SoH's CS pick.
  Gated by `SOH3D_TITLE_NO_LIGHTING_OVERRIDE=1` for A/B.

## Verification via `compare lighting` — SoH-side patch works, source
data doesn't

`scratch/slot_ab_v2.py` runs both engines lockstep across a 14-cursor
sweep and prints Az vs SoH slot:

```
 cursor    3ds    soh  match
     50      9      8      X       (Az was 8 at cursor=50 when I transcribed the timeline)
    300      9      8      X
    500      9      7      X
    700      9      9      ✓
    900      8      8      ✓
   1100      8      7      X
   1300      8      6      X
   1500      7      5      X
   ...
```

**SoH's slot exactly follows the transcribed timeline** (cursor=50 →
slot 8 matches `{0..460, 8}`; cursor=500 → slot 7 matches `{480..640,
7}`; etc.). The override mechanism works as designed.

**Az's slot at the same cursor is different from what the transcription
captured.** The pinned counter at Az VA `0x0054CC3C` (what `force
titletime` writes) is NOT the sole driver of Az's CS timing — Az has a
SECOND counter that ticks based on wall-clock / emulator-internal
timing, and `force titletime` doesn't pin it. Different runs of the
harness produce different cursor→slot mappings on the Az side.

## What this means

The RE'd "Az cycles slots {6,7,8,9,...,0}" pattern is real — Az does
cycle through those slots. But the mapping cursor → slot is not
deterministically pinnable via the current single-write. The CS handler
Az uses likely reads a different counter than 0x0054CC3C.

## Next-step attacks

1. **RE the true CS counter.** Watch the u32 field the Az CS handler
   compares against `cmd->startFrame` at Az's cursor start. Approach:
   - Find the Az CS_CMD_SET_LIGHTING handler by hitting distinct
     writer PCs to envCtx.unk_BF (my previous +0xA5 probe only caught
     Environment_Update's shadow catchup at PC=0x0045e470; the CS
     handler writer is a different PC that fires only at boundaries).
   - Read what register or `[Rn, #imm]` the CS handler compares to
     `cmd->startFrame`. That's the true counter's VA/offset.
   - Extend `force titletime` to write BOTH counters.

2. **OR live-mirror mode.** When the harness is running both engines,
   read Az's envCtx.unk_BF each frame and write it into SoH's
   envCtx.unk_BF. Guaranteed-matching slot by construction. Not a
   production port (requires the oracle), but a strong intermediate
   step to close visual parity in-harness while (1) proceeds.

3. **Or replicate the CS byte stream.** Locate the 3DS title-cs
   command list in ROM (the scene 0x51/0x6B cs data), parse the
   CutsceneCmd_EnvironmentLighting entries, port them into SoH's title
   cutscene data. This is the deepest RE arc but produces a
   standalone-runnable SoH port with no harness dependency.

## Files

- `Shipwright/soh/src/zelda3d/zelda3d_title_lighting_timeline.inc`
- `Shipwright/soh/src/zelda3d/zelda3d.c` — new override block in
  Zelda3D_ApplyTitleCam
- `scratch/az_title_lighting_timeline.py` — dense-sample driver
- `scratch/slot_ab_v2.py` — post-patch verification

## Caveat noted

The transcribed timeline is a BEST-EFFORT starting point, not a
verified-deterministic ground truth. Better than the previous fixed
slot=1 (which never matched Az anywhere in the demo) — SoH now varies
across the sequence — but still not full parity until (1) or (3)
lands. Kept as a table + env-var override so a future correct sample
just replaces the `.inc` file.
