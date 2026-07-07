# 2026-07-07 — title cs LOCATED for real (spot99) + " BDQ" format SOLVED

## Headline

1. **The OoT3D title scene is `spot99`, not spot00.** At title, live memory
   holds `/scene/spot99_info.zsi` (VA `0x0877AA20`) and `/scene/spot99_0_info.zsi`
   (VA `0x088C7070`), verified 960/960 bytes against ROM files via the new
   harness `memscan`. Neither spot00 ZSI is loaded at all. This falsifies
   PLAN-title-scripted-port.md's "scene 0x51 = spot00 on both engines"
   assumption and explains every failed FCRAM scan for the spot00 cs blob.

2. **The " BDQ" cs container is decoded and the interpreter alignment is
   pinned.** FUN_002c5ba0's `param_3` points AT the `" BDQ"` magic:
   `{u32 " BDQ", u16 ver=3, u16 0, s32 cmd_count, s32 end_frame}` then
   commands at +0x10. The 16 bytes BEFORE " BDQ" (e.g. "OHHH…") are a
   separate container prefix (cmd-0x17 alt-header entry ptr + 0x10 lands
   on " BDQ"... precisely: entry ptr = prefix start, prefix is 16B).

3. **The title cs is at `spot99_info.zsi + 0x3518`** (prefix; " BDQ" at
   +0x3528), live VA `0x0877DF38`/`0x0877DF48`. Live bytes are IDENTICAL to
   the ROM file (the only live-vs-file diffs in the whole ZSI are relocated
   collision pointers at +0x3058 and 3 bytes at +0x3480).

## The title cs walk (tools/walk_oot3d_cs.py spot99_info.zsi 0x3528)

```
cmd_count=13 end_frame=2400            # 40 s loop @60fps
+0x3538: op=0x0a cnt=3  [0x40 f300..622] [0x24 f622..750] [0x24 f750..925]
+0x35d0: op=0x0d cnt=1  [0x1 f0..4500]
+0x3608: op=0x0a cnt=3  [0x40 f924..925] [0x24 f925..1108] [0x24 f1108..1380]
+0x36a0: op=0x03 cnt=1  [0x1e f345..346]
+0x36d8: op=0x03 cnt=1  [0x1f f1930..1931]
+0x3710: op=0x0a cnt=1  [0x41 f1380..1619]
+0x3748: op=0x3e8 (1000) 16B
+0x3758: op=0x0a cnt=2  [0x41 f0..15] [0x24 f15..300]
+0x37c0: op=0x8c cnt=2  (12B records — time-of-day advance)
+0x37e0: op=0x0a cnt=6  [0x40 f1619..1620] [0x41 f1620..1665] [0x24 f1665..1695]
+0x3908: op=0x7c cnt=1  [0x4 f2310..2460]   (transition fade)
+0x3940: op=0x3e cnt=1  [0x1 f0..90]
+0x3978: op=0x97 len=0x2200                 (SPLINE BLOCK — camera + actor motion)
ends exactly at -1 terminator, 8 bytes before file end.
```

The stream walks CLEAN — every stride from FUN_002c5ba0's case bodies.

- **op 0x97 (len 0x2200) is the flyover.** Its case body parses a
  sub-container via FUN_00494600(csCtx+0x88, data), selects the segment
  whose [seg+8, seg+0xC) frame range contains the current frame, then
  FUN_0033cb90(&seg, frame, csCtx+0x94) + FUN_0033cb1c(actor, csCtx+0x94,
  play+0x20ac, 0) — spline-evaluated ACTOR/camera motion. Grezzo replaced
  the N64 camera cmds (1/2/5/6, still supported by the interpreter) with
  this block for the title. NEXT: decomp FUN_00494600 / FUN_0033cb90 /
  FUN_0033cb1c / FUN_0032b69c and decode the 0x2200-byte payload.
- op 0x0a records (sub-ops 0x40/0x41/0x24) land in csCtx+0x40 —
  lighting/env cue family (N64 CS_CMD_MISC-like). The env-palette timeline
  the earlier sessions chased lives HERE.
- op 0x8c: byte@+6 and byte@+7 feed a scaled sum written to s16
  (DAT_002c6550+3 and local_34+0xa8) — time-of-day advance cue.

## Falsified along the way (fix your priors)

- ~~`0x0054CC3C` is the "Az live cs frame counter"~~ (title_cs_blob_located
  journal). It is a GENERIC per-frame tick: a thunk at `0x003fd444`
  increments `[0x0054CC34]+8` whenever event mask r0==0x400 fires, then
  tail-calls a fn ptr. Struct at 0x0054CC34 = {fn, arg, tick}. Not csCtx.
- ~~title cs = spot00_info.zsi cmd-0x18 entry[0] blob at 0x28a24~~
  (`scratch/oot3d_title_cs/title_cs.bin`). That IS a " BDQ" cs
  (cmd_count=8, end_frame=4800), but it belongs to spot00's gameplay
  setups and is NOT loaded at title. Its first command is op 0x0d
  cnt=1 [0x1 f0..2250] — the "2250" that earlier sessions took for the
  title end_frame is a RECORD FIELD of the wrong cutscene.
- The interpreter header is NOT `{unk, u16, u16, cmd_count, end_frame}`
  floating after a 32B prefix — param_3 sits exactly on " BDQ".

## New tooling

- **harness `memscan <va_start> <va_end> <hexpattern>`** — scan mapped
  guest VA space (page-table aware, cross-page matches, ≤32 hits).
  `tools/soh3d_harness/main.cpp::HandleMemScan`. This is what found the
  live ZSIs after physical-dump scans + linear-VA guesses both failed
  (app heap is page-mapped, VA≠linear(PA)).
- **tools/walk_oot3d_cs.py** — the " BDQ" stream walker with the exact
  per-opcode strides from FUN_002c5ba0 (supersedes tools/scan_oot3d_cs.py,
  whose uniform-stride assumption was wrong).
- oot3d-decomp: `tools/ghidra_scripts/DisasmForce.py` — force-disassemble
  regions auto-analysis never reached (fn-ptr-dispatched code).

## Next session

1. Decomp op-0x97 helpers (FUN_00494600, FUN_0032b69c, FUN_0033cb90,
   FUN_0033cb1c) → decode the 0x2200 spline payload (camera eye/at +
   Link/Epona path for the flyover).
2. Decode op 0x0a sub-ops 0x40/0x41/0x24 (case-10 pointer lands at
   csCtx+0x40; find the consumer) → env palette timeline.
3. Find csCtx: watch writes to VA 0x0877DF48-referencing slots, or scan
   for u32 == 0x0877DF48 via memscan (csCtx holds the script ptr).
4. Port: Zelda3D_Cutscene_Update consuming these bytes (PLAN Phase 3),
   spot99 assets for the title scene.
