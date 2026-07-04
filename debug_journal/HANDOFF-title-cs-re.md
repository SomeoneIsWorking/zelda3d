# Handoff — 3DS title-cs RE (2026-07-04)

Ground truth durable across ~7 attempts this session. The RE hit
diminishing returns — the next session needs a different tool, not
more spot-checks.

## Ground truth (verified, byte-for-byte)

1. **Environment_Update = FUN_0045dd30** (4828B, sole caller
   FUN_002e2e60 Actor_UpdateAll at PC 0x002e43cc).

2. **Per-frame unk_BF-style store** at PC 0x0045efcc:
   `strb r0, [r4, #0x4a]` → captured VA `0x08721A1A` on Az's live
   layout (play VA `0x0871E840`).

3. **Palette lookup pattern** `rsb r,r,r,lsl#3; add r,R5,r,lsl#2` =
   `palette_base + slot*0x1C`. R5 = `*(param_1 + 0x3230)` = palette
   base ptr (per Env_Update prologue).

4. **Angle input to Env_Update's trig math** is at global VA
   `0x00587964` (u16). READ via `iVar17 = *(int*)(0x0045e140) =
   0x00587958`, then `*(short*)(iVar17 + 0xc)`. This is NOT part of
   envCtx and NOT written by SET_LIGHTING — its only writer is
   FUN_0033b880 which is an actor state gate, not a lighting handler.

5. **Palette contents** (byte-identical to SoH's spot00 kSlots):
   17 slots at `0x0877dc74`, runtime copy at `0x099d7284`.

## Falsified hypotheses (do NOT re-walk)

- env base at `play + 0x3135` → real is `play + 0x3190` (per R4
  arithmetic in Env_Update). Note: even this is UNVERIFIED against an
  independent structural anchor; may still be off.
- env+0xA7 as CS target byte → internal Env_Update countdown.
- env+0xC4 as CS target → init-touched only.
- env+0x21 (= play+0x31B1) as "current CS entry idx" driving palette
  lookup via 54-byte-stride table at play+0x318F, with slot at
  entry+5 → byte at entry+5 = 0x65 = 101, not a slot value.
- FUN_0033b880 as SET_LIGHTING dispatcher → callers pass param_3 = 0
  and install actor-state fn pointers on success, not a cs handler
  shape.
- CS_CMD_SET_LIGHTING at 0x001758f8 (older session finding) →
  self-decrement countdown timer, ruled out.

## The actual mechanism (still unknown)

Something writes a slot value (0..16) that Env_Update indirectly
consumes. That something is invisible to all my probes so far:

- Watchpoints on env+0xA5, env+0xA7, env+0xC4, env+0x118 → only
  Env_Update fires.
- Global angle 0x00587964 → only actor-state fn writes.
- CS-entry-shape table at play+0x318F → doesn't look like CS entries.

Possible remaining explanations:
1. The slot cycling {6,7,8,9,...} at Az's title is NOT CS-driven at
   all. It's a byproduct of Env_Update's own trig(angle) lerp math
   producing byte values that HAPPEN to fall in that range as the
   global angle at 0x00587964 rotates slowly. The palette-lookup
   pattern I found may not use this "slot"-shaped value.
2. The real slot input is at an envCtx offset outside my range probe
   (env+0xE0..0x120 or env+0x120..+0x200).
3. Az's 3DS engine writes the slot via memory-mapped I/O or DMA
   that bypasses MemorySystem::Write<T> entirely (unlikely for
   ARM11 code but possible for the scene loader stage).

## Concrete next-session attack

**Systematic data-flow tool.** Write a Ghidra script that:

1. Loads FUN_0045dd30 (Env_Update).
2. From the prologue registers (R4 = param_2, R5 = *(param_1+0x3230),
   etc.), tracks register provenance forward through EVERY instruction.
3. At each LDRB/STRB, emits `PC → base_reg_provenance + immediate`
   (e.g. "PC 0x0045efcc: STRB r0, [param_2 + 0x4a]").
4. Aggregates: every field OFFSET touched, classified as READ or
   WRITE, keyed by the arg-register chain.

This gives ground truth per-store without any register-reuse
mistakes. Once the palette-lookup LDR's register-and-offset is
resolved, the true slot-input field is known.

Alternative: instead of chasing forward from Env_Update, work
BACKWARD from the DISPLAY. Az's rendered frame at title has known
per-pixel bytes (via harness snapshot). The GPU vertex/fragment
pipeline consumed a specific `lightSettings` struct — trace back
from the GPU submit code to find what `lightSettings` struct pointer
was submitted, and who wrote that pointer's contents.

## Files (all committed)

Debug journals (this session):
- `2026-07-04-title-cs-re-pivot.md`
- `2026-07-04-memlog-cs-writer-hunt.md`
- `2026-07-04-envctx-r4-offset-discovery.md`
- `2026-07-04-envctx-basepointer-shift.md`
- `2026-07-04-palette-lookup-decoded.md`
- `2026-07-04-cs-table-dump-mismatch.md`
- `2026-07-04-angle-writer-fun_0033b880.md`

Azahar patches documented in `tools/soh3d_harness/AZAHAR_PATCH.md`
(Patch 4 = inline memlog with range mode, Patch 5 =
SOH3D_CPU_INTERPRETER override).

Ghidra scripts in oot3d-decomp/tools/ghidra_scripts/:
- `DumpArmRange.py`
- `FindStructFieldWriters.py`
- `FindCsSetLighting.py` (Thumb + ARM pass)
- `DisasmThumb.py`
