# 2026-07-04 — inline Azahar memlog + interpreter override → 3 writer PCs pinned

Follow-through from `2026-07-04-title-cs-re-pivot.md`. User directive:
"Edit Azahar source code, place watches inside" + "You can run under
interpreter if you want."

## Tooling landed

1. **Inline memlog in `Azahar/src/core/memory.cpp`** — a per-VA write
   logger placed at every `MemorySystem::Write<T>` store site (fast
   path, MemoryWatchpoint, RasterizerCachedMemory,
   RasterizerCachedMemoryWatchpoint). Constructor-time init of
   `Soh3dMemLogCfg` reads `SOH3D_MEMLOG_VAS` and `SOH3D_MEMLOG_PATH`
   env vars. Byte-granular match against sizeof(T) write range. Prints
   one line per matching write with PC, LR, r0-r3, SP.

2. **Interpreter override in `Azahar/src/core/core.cpp`** — new env
   `SOH3D_CPU_INTERPRETER=1` forces `ARM_DynCom` (interpreter) over
   `ARM_Dynarmic` (JIT). Required because Dynarmic's JIT compiles guest
   stores as direct memcpy via the page_table pointer array, bypassing
   `MemorySystem::Write<T>` entirely — the memlog then never fires.
   Interpreter routes every write through the `MemoryWrite8/16/32`
   callback → `MemorySystem::Write<T>` → the memlog. Slower per frame
   but surfaces every guest store.

3. **Patch doc updated** — `tools/soh3d_harness/AZAHAR_PATCH.md`
   includes Patch 4 (memlog) so both edits can be re-applied to a
   fresh Azahar clone.

## Distinct writer PCs to envCtx.unk_BF (env+0xA5 = play+0x31DA)

Full 63-cursor sweep in interpreter mode with the watchpoint page
armed (RegisterWatchpoint nulls the fast-path pointer, forcing every
write through the switch):

```
PC=0x0045efcc  hits=1543  data=0x09  lr=0xFF     — Env_Update (FUN_0045dd30)
PC=0x0034329c  hits=1     data=0x00  lr=0x0      — memset/zero init
PC=0x002de754  hits=1     data=0x50  lr=0x0044ffc4 — FUN_002de738 setter (init)
```

Env_Update fires every frame writing the current slot. The other two
are one-shot init writes. **No CS-handler PC surfaced** — the sweep ran
in interpreter mode with `force titletime` pinning cursor 0x0054CC3C
across 50..3200, but the interpreter is slow enough that Az's actual
CS state didn't advance far enough to fire SET_LIGHTING boundaries.
All 1543 Env_Update writes committed `slot=9` — Az stayed on that
slot for the whole run.

## Env+0xC4 (Env_Update line 397 reads a ushort here)

```
PC=0x0034329c  hits=1  data=0  lr=0x0        — memset zero
PC=0x0044fffc  hits=1  data=0  lr=0x0044ffd4 — FUN_0044ff18 init (2788B)
```

Only two writes across the whole sweep, both value 0, both at init.
This field ISN'T the CS SET_LIGHTING target either — the CS handler
doesn't touch it per-cursor. Whatever `param_2 + 0xc4` reads in the
Env_Update lerp math is a per-scene constant set at init.

## Falsified reads of the Env_Update decomp

- `param_2 + 0xa7` was assumed to be the CS target (Env_Update line 133
  reads it as bVar5). But my earlier probe showed +0xA7 has values
  111-114 — outside the `< 0x20` gate at line 140, so the shadow-swap
  path never fires. Yet 1543 unk_BF writes still land at line 143.
  Decomp condition tree may be simplified; the actual store at PC
  0x0045efcc appears unconditional and writes whatever value's in
  bVar5 (or a related reg).

## Key durable finding

**envCtx.unk_BF has only ONE per-frame writer: Environment_Update at
PC 0x0045efcc.** The 3DS title-cs SET_LIGHTING handler does NOT write
unk_BF directly — it must write to some OTHER field that Env_Update
reads and commits. Candidate fields Env_Update reads: +0xA6, +0xA7,
+0xC4, +0xCC. Of these, +0xC4 is only init-touched. Next probe must
watchpoint a wider range (env+0xA0..+0xE0) simultaneously to catch the
handler's actual write target.

## Next step

Two viable attacks:

1. **Wider watch range.** RegisterWatchpoint with size=48 covering
   env+0xA0..+0xD0. Every CS handler write within the envCtx tail will
   be captured; filter by data value < 32 or by PC not in
   {FUN_0045dd30, memset, FUN_002de738} to isolate the handler.

2. **Locate title-playback dispatcher via 0x0054CC3C reads.** The 3
   readers found earlier (`FUN_003fd27c`, `FUN_0041af08` — plus writer
   `FUN_004175d4`) include one that's likely the demo-cursor
   dispatcher. Decompile the reads, follow into whatever fn consumes
   the cursor value to schedule CS commands.

The RE plumbing is now in place — memlog + interpreter mode + patch
doc. Next session picks up either attack.

## Files

- `Azahar/src/core/memory.cpp` — inline memlog (gitignored per repo policy)
- `Azahar/src/core/core.cpp` — SOH3D_CPU_INTERPRETER override (gitignored)
- `tools/soh3d_harness/AZAHAR_PATCH.md` — Patch 4 documented
- `scratch/memlog_cs_hunt.py` — envCtx.unk_BF probe
- `scratch/memlog_env_c4.py` — env+0xC4 probe
- `oot3d-decomp/build/decomp/002de738.c` — small setter fn
- `oot3d-decomp/build/decomp/0044ff18.c` — 2788B scene init caller
