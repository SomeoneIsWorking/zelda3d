# 2026-07-04 — palette lookup decoded via raw ARM: CS entry table found

Follow-up to `2026-07-04-envctx-basepointer-shift.md`. Raw ARM at
PC 0x0045e790..0x0045e79c inside FUN_0045dd30 (Env_Update) reveals the
actual palette-lookup pattern.

## The pattern

```
0045e790  ldrb r3, [r0, #0x5]       ; load slot index from a table entry
0045e794  rsb r3, r3, r3, lsl #0x3  ; r3 = 7 * r3
0045e798  add r3, r5, r3, lsl #0x2  ; r3 = R5 + 28 * r3 = palette_base + slot*0x1C
0045e79c  ldrb r12, [r3, #0x1a]     ; read a byte from the slot at offset +0x1A
```

`rsb r, r, r, lsl #3` computes `7*r` (r << 3 minus r). Then
`add r, base, r, lsl #2` shifts by 2 to give `28 * r`. So the slot
stride is 0x1C — matches the palette entry size.

## Where the slot index comes from

R0 was computed at PC 0x0045e6ac-b8:

```
0045e6ac  ldrb r0, [r4, #0x21]        ; env + 0x21 (= "current CS entry idx"?)
0045e6b0  add r0, r0, r0, lsl #0x3    ; r0 = 9*r0
0045e6b4  add r0, r0, r0, lsl #0x1    ; r0 = 3*(9*r0) = 27*r0
0045e6b8  add r0, r9, r0, lsl #0x1    ; r0 = R9 + 2*(27*r0) = R9 + 54*r0
0045e6bc  add r0, r0, r7, lsl #0x1    ; r0 += 2*R7 (another table offset)
```

So R0 = `R9 + 54 * env[0x21] + 2*R7`. R9 was set in the prologue from
`ldr r9, [sp, #0x40]` = the 5th param = Actor_UpdateAll's
`param_1 + 0xc63` (= play + 0x318F ≈ envCtx-adjacent).

R7 was `param_1 + 0x3000` = play + 0x252C + 0x3000 = play + 0x552C.

## Interpretation

- **env+0x21** (= play + 0x31B1, VA 0x08721996 for the Az addr layout)
  holds the CURRENT CS-command INDEX driving the palette pick.

- Each CS-command entry is **54 bytes wide** (0x36) and lives in a
  table at R9 = play + 0x318F (env-adjacent). Byte at entry+5 = the
  slot index for that command.

- Env_Update reads env+0x21 → indexes the table → reads the entry's
  byte 5 → uses as palette slot.

So the CS SET_LIGHTING handler:

- Either writes the SLOT byte at some entry+5 in the CS table
- Or advances env+0x21 to a new CS-command idx pointing at a different
  entry (which already has its slot byte set at scene load).

Either mechanism, the WRITER we're chasing touches either env+0x21 or
one of the CS-entry bytes at play+0x318F+n*0x36+5.

## Discovered field addresses (Az at 0x0871E840)

| Field | ARM offset | play offset | VA |
|-------|------------|-------------|----|
| envCtx base (R4 in Env_Update) | r4+0    | play+0x3190 | 0x087219D0 |
| env+0x21 (CS command idx)      | r4+0x21 | play+0x31B1 | 0x087219F1 |
| unk_BF byte                    | r4+0x4A | play+0x31DA | 0x08721A1A |
| Env_Update internal shadow     | r4+0xBD | play+0x324D | 0x0872_1A8D |
| CS command table base          | -       | play+0x318F | 0x087219CF |
| Palette base pointer           | -       | *(play+0x575C) | (heap) |

## Next-step attack

Watchpoint env+0x21 (VA 0x087219F1). Its writer PC is either:

1. The demo-cursor engine that steps through active CS commands, OR
2. The CS_CMD_SET_LIGHTING handler itself (if it advances the idx).

Its enclosing fn IS the title-cs playback engine.

Alternative: dump 54*17 = 918 bytes starting at play+0x318F. If the
byte at offset 5 within each 54-byte entry looks like a slot value
(0..16), we have the CS SET_LIGHTING command list — the direct port
target for SoH3D.

## What port shape SoH3D needs

If the CS command table at play+0x318F is populated at title-scene
init from scene ROM data, we can:

1. Locate its populator fn (scene init) via a memlog watchpoint on
   the byte at play+0x318F+5 (first entry's slot byte). Static
   analysis of the populator reveals the raw scene-data format.
2. Extract the 3DS title cs command list from ROM.
3. Port those raw bytes into SoH3D's title-cs data blob, driving SoH's
   own CS handler with the OoT3D command sequence.

## Files

- `oot3d-decomp/tools/ghidra_scripts/DumpArmRange.py` (already
  committed, exit env vars are OOT3D_DUMP_START/END).
