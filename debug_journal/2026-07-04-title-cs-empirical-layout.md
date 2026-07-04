# 2026-07-04 — title_cs.bin empirical layout via header-pattern search

Follow-up to `2026-07-04-cs-format-structural-correction.md`. Rather
than continue chasing FUN_002c5ba0's caller and param_3 alignment,
this session takes an empirical route: scan `title_cs.bin` for byte
patterns that look like cs headers (`cmd_count` small + `end_frame` in
the plausible title range 1..2300).

## Key discovery: a big 48-byte record table at +0x98

The scan surfaces a striking regular pattern starting at file-offset
`+0x98` in `title_cs.bin`:

```
+0x0098  index=1   at +0x8, ef=100 (0x64) at +0xC, 48B record
+0x00C8  index=2   at +0x8, ef=100 at +0xC, 48B record
+0x00F8  index=3   ...
+0x0128  index=4
+0x0158  index=5
+0x0188  index=6
+0x01B8  index=7
+0x01E8  index=8
+0x0218  index=9
+0x0248  index=10
... continues at 48B stride
+0x0518  index=25  (last one printed by the 30-hit cap)
```

Each 48-byte record has shape:
- +0x00: u32 = 0
- +0x04: u32 = 0
- +0x08: u32 = sequential 1, 2, 3, ... (index or actor slot?)
- +0x0C: u32 = 100 (constant — likely per-record end_frame)
- +0x10..0x2F: `00 00 00 00 a0 00 00 00 ...` — record-specific data

This is exactly the **48B sub-record stride** predicted by
FUN_002c5ba0's default case (caseD_b) — but starting at +0x98, not
+0x30. So this IS a chunk of the cs cmd stream, in the shape the
interpreter expects.

## Full-blob layout (empirical)

```
+0x00 .. +0x1F   GREZZO container prefix (32B)
+0x20 .. +0x2F   cs metadata (cmd_count, end_frame, unks)
+0x30 .. +0x97   VARIABLE-STRIDE opcodes (probably camera pos/focus
                 via opcodes 1, 2; matches the 12-byte-header +
                 16B-atom-list shape in FUN_002c5ba0's opcode 1/2)
+0x98 .. ~0x13??  BIG 48-byte record table (~100 entries × 48B ≈
                 cs_len 0x12c0)
```

Regions at +0x30..+0x60 contain byte patterns like `feffffff 10000000`
which look like inline atoms with a `-1` terminator — matching
FUN_002c5ba0's opcode 1/2 camera-position atom-list mechanic.

## What "cmd_count = 13" really means

Prior journal assumed cmd_count = 13 in the (+0x20) header meant
"13 outer commands". Looking at the empirical layout, this is more
likely:
- 13 = number of top-level "tracks" (camera pos, camera focus, misc
  action, misc lighting, misc bgm, misc trans-fx, etc — matching N64
  z_demo's ~13 CS_CMD_* command types).
- Each track has its own opcode + sub-count + sub-record list of
  opcode-specific stride.

This is the classical z_demo cs layout after all — the interpreter's
outer loop iterates 13 tracks, each track dispatches by opcode to its
own inline walker.

## Sub-record content clues

Each of the 48-byte records at +0x98+ has:
- +0x08: sequential index (1..25 for the first 25)
- +0x0C: 100 = a per-record duration/end_frame
- +0x14: `a0 00 00 00` = 160 (some duration or actor param)
- +0x18+: mostly zeros

This pattern (sequential ID + fixed 100-frame duration + short data)
looks like an **actor cue track** — likely `CS_CMD_SET_ACTOR_ACTION_N`
family in SoH terms. The title-demo has Link on Epona riding across
Hyrule Field with a sequence of animations firing per shot; 25+
sequential 100-frame cues matches ~25 shots over 37.5 seconds.

## Confidence

- The 48B-stride finding is HIGH confidence (regular table, matches
  FUN_002c5ba0's default case exactly).
- The "which opcode this table belongs to" is MEDIUM confidence
  (need to see the opcode value that precedes it at +0x?? — likely
  right before +0x98, so around +0x94 or +0x90).
- The variable-stride +0x30..+0x98 region is LOW confidence for its
  exact opcode boundaries — needs FUN_002c5ba0 case-body decomp of
  opcodes 0, 1, 2, 3 to correctly walk.

## Concrete next-session move

1. Extract the u32 word at file offset +0x94 (right before the 48B
   table's first entry) — that should be the sub_count for the track
   whose sub-records the table holds. If sub_count = 100, and the
   table spans 100×48 = 4800 bytes = 0x12C0 = cs_len exactly, then:
   - The "cmd stream" is a single track (opcode + sub_count=100 + 100
     sub-records)
   - cs_len = 100*48 = 4800 = 0x12C0 ✓
   - Prior "cmd_count = 13" is a DIFFERENT field (maybe number of alt
     header segments, or a track index?)

2. Then the opcode at +0x8C or nearby names the track type. Look up
   that opcode in FUN_002c5ba0's switch and read its case body.

3. Iterate: bake this into the interpreter port.

## Files

- `tools/scan_oot3d_cs.py` — the earlier full-blob walker (uniform
  stride) — its FAILURE is what motivated this empirical scan
- The header-pattern scan is inline in this journal (short one-liner);
  can be turned into a tool if reused
