# 2026-07-04 — env-base shift: R4 = play+0x3190, not play+0x3135

Follow-up correcting `2026-07-04-envctx-r4-offset-discovery.md`. Deeper
raw-ARM reading + tracing back through Env_Update's caller reveals the
true base pointer.

## What I got wrong

Previous journal said "R4 = env+0x5B" based on the mismatch between
Ghidra decomp offsets (`param_2 + 0xa5`) and the raw ARM offsets
(`[r4, #0x4a]`). That deduction assumed env base = play + 0x3135.

## The correct chain

Env_Update prologue at PC 0x0045dd30-0x0045dd50:
```
0045dd30  push {r4-r11, lr}
0045dd34  cpy r4, r1                    ; R4 = param_2
0045dd38  add r7, r0, #0x3000            ; R7 = param_1 + 0x3000
0045dd3c  cpy r8, r0                    ; R8 = param_1
0045dd54  ldr r5, [r7, #0x230]           ; R5 = *(param_1 + 0x3230)
```

Actor_UpdateAll (FUN_002e2e60) calls Env_Update at line 797 of decomp:
```
FUN_0045dd30(param_1, param_1 + 0xc64, param_1 + 0x29c,
             param_1 + 0xa28, param_1 + 0xc63);
```

So Env_Update's param_2 = Actor_UpdateAll's param_1 + 0xc64.

The captured memlog write was at VA 0x08721A1A, produced by
`strb r0, [r4, #0x4a]` at PC 0x0045efcc. So:

    R4 = 0x08721A1A - 0x4A = 0x087219D0
    R4 = param_2 = Actor_UpdateAll_param_1 + 0xc64

    Actor_UpdateAll_param_1 = 0x087219D0 - 0xc64 = 0x08720D6C
                            = play(0x0871E840) + 0x252C

**So Actor_UpdateAll is called with `play + 0x252C`, NOT with `play`
directly.** This makes 0x087219D0 (R4 = env base for Env_Update) equal
to `play + 0x3190`, not `play + 0x3135`.

## Corrected envCtx layout — the true offsets

- **envCtx base = play + 0x3190**  (Az live: 0x087219D0)

- **unk_BF (current lightSettings slot) = env + 0x4A** (r4+0x4a in ARM)
  → play + 0x31DA (matches probe VA)

- The read source for unk_BF's per-frame write:
  `ldrb r0, [r4, #0xbd]` at PC 0x0045efc8 = **env + 0xBD** = play + 0x324D
  → also confirmed by memlog probe at that VA showing PC=0x0045e4a0
  writing there each frame with the same data value 0x09.

- The palette-base pointer field. R7 = param_1 + 0x3000, R5 = *(R7+0x230)
  = *(param_1 + 0x3230). With param_1 = play + 0x252C:
  **palette pointer field is at play + 0x252C + 0x3230 = play + 0x575C**.

## What env+0xBD actually holds

The write at PC 0x0045e4a0 stores `-int(cos_or_sin(angle) * pool_const)`
as a u8. The angle comes from `ldrh r0, [r10, #0xc]; sub #0x8000; sxth`
(a signed-16 rotation). r10 is set from `ldr r9, [sp, #0x40]` in the
prologue (5th param = Actor_UpdateAll param_1 + 0xc63 = play + 0x3187).

So env+0xBD isn't necessarily a "shadow slot index" — it might be one
byte of a computed light-direction vector (cos/sin encoded as signed
u8), and the byte at env+0x4A (that we've been calling unk_BF) may be
ONE such byte in a Zelda3dLightSlot's l1dir/l2dir field, not a slot
INDEX at all.

Values seen at env+0x4A (0x08721A1A) were {6, 7, 8, 9} — a small
range consistent with cos(angle) * 127 where angle stays near a
specific value at title (~1.4 rad → cos ≈ 0.17 → 0.17*127 ≈ 22, close
but not exact match — the pool_const might not be 127).

## Palette lookup line 366 of decomp

`pfVar14 = (float *)(iVar18 + (uint)*(byte *)(param_2 + 0xa5) * 0x1c);`

Ghidra's `param_2 + 0xa5` is NOT the same as R4+0xA5 in raw ARM. Its
decomp-variable naming was reassigned. The actual slot-index byte
Env_Update uses for the palette lookup is at whatever ARM offset the
raw instruction implements, which needs DumpArmRange around line 366's
compiled site to pin.

## Implications

- Every earlier claim about "envCtx.unk_BF at play+0x31DA" needs
  re-verification with raw ARM. It might actually be a light-dir byte,
  and the true unk_BF sits elsewhere.

- The 3DS OoT envCtx layout is NOT close to the N64 layout (which had
  unk_BF at fixed +0xBF from envCtx base). The 3DS refactored the
  struct heavily.

- The CS SET_LIGHTING handler's actual write target is deeper in the
  RE than my earlier journals claimed. Need to disasm the palette
  lookup's raw ARM to find the true slot-index byte, then watchpoint
  it to catch the handler.

## Next-step attack

1. `DumpArmRange 0x0045e6c0 0x0045e720` (spans decomp line 366 in
   FUN_0045dd30). Find the raw `ldrb rN, [r4, #imm]` that supplies the
   palette-slot index for the `iVar18 + slot*0x1C` multiply.
2. That `imm` (relative to R4) is the true unk_BF offset within
   Env_Update's base.
3. Convert to env-base and play-relative VA.
4. Watchpoint that VA. Non-Env_Update writers to it = the CS handler.

## Files

- `debug_journal/2026-07-04-envctx-r4-offset-discovery.md`
  (previous journal — first-order correction, kept for the traceback)
- `oot3d-decomp/tools/ghidra_scripts/DumpArmRange.py`
  (already committed)
