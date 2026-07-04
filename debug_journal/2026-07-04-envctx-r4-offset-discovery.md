# 2026-07-04 — R4 in Env_Update is env+0x5B, not env base

Follow-through from `2026-07-04-memlog-cs-writer-hunt.md`. Ghidra static
ARM disasm of PC 0x0045efb0..0x0045efd0 (inside FUN_0045dd30 =
Environment_Update) reveals my earlier assumption was wrong.

## The mistake

Ghidra decomp lines 133 and 143 read as:

```
bVar5 = *(byte *)(param_2 + 0xa7);   // "read from env+0xA7"
*(byte *)(param_2 + 0xa5) = bVar5;   // "write to env+0xA5"
```

Interpreted this as "env+0xA7 is the CS target that Env_Update commits
to env+0xA5". The memlog data falsified this: env+0xA7 has
countdown-shape values (0x8B, 0x8A, 0x89, ...), never matches slot
values, and yet unk_BF (env+0xA5) is written with slot 0x09 every
frame. Contradiction.

## The truth via raw ARM

Dumping ARM at PC 0x0045efb0..0x0045efd0:

```
0045efb0  ldrb r0, [r4, #0xb7]     ; read r4+0xB7
0045efb4  strb r0, [r4, #0x32]     ; write r4+0x32
0045efb8  ldrb r0, [r4, #0xbb]     ; read r4+0xBB
0045efbc  strb r0, [r4, #0x48]     ; write r4+0x48
0045efc0  ldrb r0, [r4, #0xbc]     ; read r4+0xBC
0045efc4  strb r0, [r4, #0x49]     ; write r4+0x49
0045efc8  ldrb r0, [r4, #0xbd]     ; read r4+0xBD    ← LOAD SOURCE
0045efcc  strb r0, [r4, #0x4a]     ; write r4+0x4A   ← the store I captured
```

The captured write was to VA `0x08721A1A` (env+0xA5). If that's
`r4+0x4A`, then **R4 = env+0x5B**, NOT env base. The whole store block
uses offsets RELATIVE to R4 = env+0x5B, not to envCtx base.

Recomputing every referenced offset:
- `r4+0x4A` = env+0x5B+0x4A = env+0xA5 (unk_BF) ✓
- `r4+0xBD` = env+0x5B+0xBD = **env+0x118** (the READ source, not env+0xA7)
- `r4+0x32` = env+0x5B+0x32 = env+0x8D
- `r4+0x48/49/4A` = env+0xA3/A4/A5
- `r4+0xB7/BB/BC/BD` = env+0x112/116/117/118

Ghidra's decomp variable `param_2` was NOT env base — it was env+0x5B
(or some equivalent transformation). The `param_2 + 0xa5` in decomp
means `r4+0x4A` in ARM, and `param_2 + 0xa7` means `r4+0x4C` in ARM,
which is env+0xA7. But my probe of env+0xA7 showed countdown values —
that's still a real field; Env_Update reads it AND reads env+0x118 (a
DIFFERENT ldrb site).

## Env_Update's actual chain

Each frame Env_Update:

1. Computes some lerp math around PC 0x0045e470-0x0045e4a0:
   - Multiplies a pool float by s0 (previous state)
   - Converts to int, negates
   - Stores the negated int as u8 at r4+0xBD = **env+0x118**
2. Later, at PC 0x0045efc8/cc:
   - Reads r4+0xBD (env+0x118) → writes to r4+0x4A = env+0xA5 (unk_BF)

So env+0xA5 tracks env+0x118 every frame. And env+0x118 tracks the
computed lerp result. Env_Update's slot output is a self-contained
loop with an internal state variable at env+0x118.

## Where the CS handler actually feeds in

Env_Update reads env+0xB5, +0xB6, +0xB7 (three consecutive bytes) as
part of the math chain that eventually becomes env+0x118 → env+0xA5.
That's the LERP INPUT chain. The CS SET_LIGHTING handler must write to
one of these upstream bytes (env+0xB5..+0xB7 or upstream of that).

Memlog probes so far covered env+0xA0..+0xE0. Every hit in that range
was Env_Update itself or init. The CS handler must reach either:
- Env+0x40..+0xA0 (upstream inputs to the lerp math)
- Env+0xE0..+0x120 (Env_Update also reads env+0x118 = r4+0xBD, and
  writes there — anything the CS writes to env+0x118 gets clobbered by
  Env_Update the next frame anyway, so unlikely)

## Next-step attack

Extend the range probe to env+0x40..+0xA0 (matches the Env_Update lerp
input registers). Filter writer PCs; anything NOT in FUN_0045dd30 or
FUN_0044ff18 (scene init) is a CS handler candidate.

Also: run in JIT mode (much faster) so Az's title-cs progresses
through many shot cuts; interpreter mode is too slow to naturally
advance the demo counter. Since Dynarmic JIT bypasses my inline
memlog, RegisterWatchpoint the target range explicitly via `watch` —
that nulls the page pointer and forces Dynarmic to fall back through
MemorySystem::Write<T>, hitting the memlog.

## Files

- `Azahar/src/core/memory.cpp` — range mode added
  (`SOH3D_MEMLOG_RANGES=start:end,...`)
- `oot3d-decomp/tools/ghidra_scripts/DumpArmRange.py` — new: raw ARM
  hex + Ghidra disasm in a VA range
- `scratch/memlog_env_range.py` — env+0xA0..+0xE0 probe (findings above)
- `scratch/memlog_env_118.py` — pinned env+0x118 as internal
- `oot3d-decomp/build/decomp/002d97e4.c` — a per-frame writer at
  env+0xBF; not the CS handler (float lerp math)
