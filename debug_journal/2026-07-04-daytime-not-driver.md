# 2026-07-04 — daytime u16 at param_1+0x30A8 is NEVER per-frame updated

Follow-up to `2026-07-04-dataflow-breakthrough.md`. Watchpoint'd the
u16 pair at play+0x55D0..+0x55D8 (the fields Env_Update reads/stores
back via `[param_1 + 0x3000 + 0xa4/a8]`). Interpreter-mode free-run
40x100 ticks.

## Result

Only 4 writes captured total, all at init:

```
PC=0x0034329c  hits=2  va=0x08723e10  sz=4  data=0  lr=0  (memset)
PC=0x00343298  hits=2  va=0x08723e10  sz=4  data=0  lr=0  (memset)
```

Both are the known memset init writer chain. Zero per-frame writes.
Zero writes from Env_Update itself either — the `STRH r0, [param_1 +
0x3000 + 0xa8]` at PC 0x0045dfe0 apparently sits inside a conditional
branch that isn't taken at title.

## Falsifies

The "time-based lighting driven by a daytime u16" hypothesis from
the previous journal. Env_Update DOES read/write those fields
statically, but at title they're stub-zero-init'd and no per-frame
advance touches them. So even though the two-level lookup structure
(env[0x21]/[0x22] → 54B table → slot idx → palette) is a real
compiled pattern, it isn't the mechanism producing the {6,7,8,9}
slot cycle we observed earlier.

Combined with earlier falsifications:

- Not CS-driven (no distinct SET_LIGHTING writer PC surfaced across
  multiple range probes)
- Not daytime-driven (this journal)
- Env_Update's slot-committing store at PC 0x0045efcc IS the sole
  per-frame writer to `param_2 + 0x4a` (VA 0x08721A1A)

## What we've actually pinned so far

The strb at PC 0x0045efcc writes value from `[param_2 + 0xbd]`.

Env_Update body has hundreds of stores to `param_2 + K` bytes across
the 0xB2..0xC3 range (per DFS output). These are LERP RESULTS
between two Zelda3dLightSlot rows read from the palette. The
`[param_2 + 0xbd]` byte specifically is a computed lerped color
channel — NOT a slot index in the pure sense.

So the value 0x09 written to unk_BF-like byte at param_2+0x4a
might actually be a byte OF a color result, not a slot INDEX. The
palette-lookup pattern (`+ slot*0x1C` with R5 = palette base) IS
real and reads slot indices, but the READ SITE Ghidra decomp
identified isn't what we've been chasing.

## Where the session leaves the RE arc

Multiple hypotheses have now been each partly confirmed and each
partly falsified. Every deeper probe surfaces one more level of
abstraction but never the CS SET_LIGHTING dispatcher (or now, the
daytime-advance driver).

**Rethinking:** Az's title-demo run in interpreter mode may not be
advancing the underlying scripted-playback state at all. The
"slot cycling {6,7,8,9}" I saw in EARLIER (JIT-mode) probes was
driven by `force titletime` writes to the cursor at 0x0054CC3C.
That cursor is a VBLANK counter (per FUN_003fd27c decomp) — NOT
the title-playback frame counter.

If `force titletime` only nudges VBLANK, then the "slot cycle"
observed earlier may be a rendering-artifact response (the trig
math around the light angle updates differently as VBLANK jumps),
NOT a real CS or daytime advance.

**Which means Az's title-demo scripted playback advances only
naturally (per emulated wall clock), and my `force titletime`
probes were producing arbitrary intermediate states — not
snapshots of the title playback at cursor N.** That would explain
why the same probe run-to-run produces DIFFERENT slot values at
the same cursor (see `2026-07-04-title-lighting-timeline-port.md`
non-determinism note).

## Concrete recommendation

Stop chasing the title-cs RE via probes + Ghidra spot-checks.
The productive next step is:

1. **Get Az's title-playback to actually advance deterministically**
   — either by running the interpreter for MANY thousands of ticks
   until the demo naturally progresses through shot cuts, or by
   identifying the ACTUAL title-playback counter (NOT VBLANK) via
   the boot-chain RE angle the user mentioned earlier.

2. **Or step around this entire RE:** SoH's title runs through
   Play_Main. If the target is "the same rendering", the port
   might just replicate the OoT3D title as a SoH-side scripted
   render override — set envCtx to specific values at specific SoH
   csCtx.frames, matching what Az's title actually displays. This
   is close to the earlier reverted static-timeline approach but
   with rigor: capture Az's DISPLAY state (not internal state)
   frame-by-frame, replicate that display state in SoH.

Neither of these is "port the 3DS CS bytes" cleanly. The
mechanism appears to not be CS-based at all.

## Files

- `scratch/memlog_daytime.py` — this probe
- `/tmp/daytime.out`, `/tmp/memlog_daytime.out` — the run outputs
