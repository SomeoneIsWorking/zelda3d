# 2026-07-04 — DFS tool reveals: env[0x21]/[0x22] are TIME indices, not CS

Built a data-flow Ghidra script (`DataFlowStores.py`) that tracks
symbolic register provenance through Env_Update's 1207 instructions,
emitting every LDRB/STRB with base-reg + offset in terms of function
args. Ran it on FUN_0045dd30 → 267 tracked memory ops (dumped to
`/tmp/env_update_dfs.txt`, produced by the run below).

## The palette-lookup pattern, decoded

Env_Update reads TWO indices per frame:

```
0045e604  LDRB r1, [param_2 + 0x22]     ; env+0x22 = "next" time cursor
0045e70c  LDRB r1, [param_2 + 0x22]
0045e6ac  LDRB r0, [param_2 + 0x21]     ; env+0x21 = "current" time cursor
```

For each idx, the compiled ARM does:

```
LDRB r0, [r4, #0x21]              ; r0 = env[0x21]
add r0, r0, r0, lsl #3            ; r0 = 9*r0
add r0, r0, r0, lsl #1            ; r0 = 27*r0
add r0, r9, r0, lsl #1            ; r0 = R9 + 54*r0
add r0, r0, r7, lsl #1            ; r0 += 2*R7
```

So computes `entry = R9 + 54*env[0x21] + 2*R7`. Then:

```
LDRB r3, [r0, #0x5]               ; slot_idx = entry[5]
rsb r3, r3, r3, lsl #3            ; r3 = 7*r3
add r3, r5, r3, lsl #2            ; palette_row = R5 + 28*slot
LDRB r12, [r3, #0x1a]             ; read field at slot+0x1A
```

**So: env[0x21] → 54B entry → entry[5] = SLOT IDX → palette[slot*0x1C]
+ field.**

This is a TWO-LEVEL lookup: an intermediate 54-byte table indexed by
a TIME cursor, whose slot-idx-byte then indexes the actual palette.

## Matches N64 OoT time-based lighting table

N64 z_kankyo.c line 628 uses `D_8011FC1C[envCtx->unk_17][i]` — a
`TimeBasedLightEntry`:

```c
typedef struct {
    u16 startTime;
    u16 endTime;
    u8  lightSetting;      // byte @ +4
    u8  nextLightSetting;  // byte @ +5
} TimeBasedLightEntry;    // 6 bytes
```

3DS is expanding this to 54 bytes (9x larger — probably 8x adding
padding for alignment + additional fields like fog params).

**env[0x21] and env[0x22] are TIME-BAND indices**, NOT CS command
indices. The palette-slot progression during the title demo is
DAYTIME-DRIVEN, not CS-driven.

## What this means for the port

If Az's title lighting changes come from a virtual daytime counter
advancing through the time table, then:

- **There is no CS_CMD_SET_LIGHTING to port for title.**
- The port target is the time-based lighting table format + the
  daytime advancement that Az's scripted title playback does.
- SoH already has an equivalent time-based system in z_kankyo.c
  (`D_8011FC1C`, `TIME_ENTRY_1F`, `Environment_Update` lines 883-1080).

The 3DS format for the 54-byte entry is unknown — needs decoding from
the palette-adjacent scene data (or the ROM's scene 0x51 payload).

## Falsifies

- My CS-command-table hypothesis at play+0x318F. The 54-byte entries
  ARE at some VA computed by `R9 + 2*R7`, but that isn't a static VA.
- All prior journal claims about "SET_LIGHTING handler writes to
  env+K" — no such handler for title.
- The mem dump at play+0x318F with byte-5 = 0x65 = NOT-a-slot. That
  read was at wrong VA because R7 in the ARM code wasn't at its
  prologue value.

## Solid ground truth (updated)

- Env_Update = FUN_0045dd30. Reads env[0x21] and env[0x22] as time
  cursors. Their per-frame values drive palette selection via the
  intermediate 54-byte time-entry table.
- The intermediate table's base = `param_5 (R9) + 2*R7` — where R9
  is Env_Update's 5th arg = Actor_UpdateAll param_1 + 0xc63, R7 was
  set in prologue to `param_1 + 0x3000` but reassigned before this
  site (unclear where).
- The palette base = `*(param_1 + 0x3230)`, stride 0x1C = 28 bytes.

## Next attack (much cleaner than before)

1. **Watchpoint env[0x21] via memlog** at Az VA = param_2 + 0x21.
   Given env base = play + 0x3190, VA = play + 0x31B1 = 0x087219F1.
   Whoever writes env[0x21] is the daytime-cursor advancer. Its
   enclosing fn is the title-scripted-playback daytime driver.

2. **Extend DataFlowStores.py to track through RSB and shift-add**
   patterns so the `[? + 0x5]` and `[? + 0x1A]` bases get resolved.
   That reveals whether R7 in the palette-lookup site is
   (param_1+0x3000) — if so, the 54B table is at
   `(param_1 + 0xc63) + 2*(param_1 + 0x3000)` = some fixed play offset.

## Files

- `oot3d-decomp/tools/ghidra_scripts/DataFlowStores.py` — new
  systematic tool. Ran successfully on Env_Update: 1207 insns,
  267 tracked LDR/STR sites. Base for future data-flow work.
- `/tmp/env_update_dfs.txt` — the full store table for the current run.
