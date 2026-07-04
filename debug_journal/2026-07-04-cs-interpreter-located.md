# 2026-07-04 — OoT3D CS interpreter LOCATED (Phase 2 kickoff)

Follow-up to `2026-07-04-title-cs-blob-located.md`. Building on the
decoded container header, this session identifies the CS interpreter
and enumerates its opcode set.

## The interpreter

**`FUN_002c5ba0`** (8964 bytes) is the OoT3D CS per-frame command
executor. Signature: `(param_1=play_or_csctx, param_2=???, param_3=cmd_bytes)`.

Key mechanics (first 90 lines):

```c
FUN_00470758(auStack_40,  param_3,       4);   // read header0 u32
FUN_00470758(auStack_44,  param_3 + 4,   2);   // read u16
FUN_00470758(auStack_48,  param_3 + 6,   2);   // read u16
FUN_00470758(&local_4c,   param_3 + 8,   4);   // read cmd count
FUN_00470758(&local_58,   param_3 + 0xc, 4);   // read end frame

if (local_58 < *(u16*)(param_2 + 0x20) && *(u8*)(param_2 + 8) != 4) {
    (param_1 + 0x22ac)++;                        // advance frame counter
    ...
}

// Iterate commands from puVar17 = param_3 + 0x10:
while (true) {
    FUN_00470758(&local_50, puVar17, 4);         // read opcode u32
    if (local_50 == -1) break;                   // terminator
    switch (local_50) {
        case 0x4b: ... case 0x4c: ... case 0x77: ... case 0x8e: ...
    }
}
```

**FUN_00470758 is `memcpy_endian_swap`** — the raw cs bytes are in
big-endian on-disk and get byteswapped into little-endian locals. This
matches OoT3D being a byte-swap of the N64 asset conventions.

## Opcode set (~100 distinct)

Enumerated by grepping `case 0x??:` bodies:

```
0x0b 0x0c 0x0d 0x0e 0x0f 0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17 0x18
0x19 0x1a 0x1b 0x1c 0x1d 0x1e 0x1f 0x20 0x21 0x22 0x23 0x24 0x25 0x26
0x27 0x28 0x29 0x2a 0x2b 0x2c 0x2d 0x2e 0x2f 0x30 0x31 0x32 0x33 0x34
0x35 0x36 0x37 0x39 0x3a 0x3c 0x3e 0x3f 0x40 0x41 0x42 0x43 0x44 0x45
0x46 0x48 0x4a 0x4c 0x4d 0x4e 0x4f 0x50 0x51 0x52 0x53 0x54 0x55 0x56
0x57 0x58 0x59 0x5a 0x5d 0x5e 0x69 0x6a 0x6b 0x6c 0x6e 0x6f 0x72 0x73
0x74 0x75 0x76 0x77 0x79 0x7b 0x7c 0x7d 0x7e 0x7f 0x80 0x81 0x82 0x83
0x84 0x85 0x86 0x87 0x89 0x8a 0x8b 0x8c 0x8d 0x8e
+ 0x4b (top-level actor cue?) + 0x78 + 0x88 + 0x8f + 0x91 + 0x96 + 0x97
```

N64 OoT z_demo.c has ~40 cs opcodes (CS_CMD_00..CS_CMD_2E-ish); OoT3D
has ~100+, roughly split:

- **0x0b..0x50**: mirrors N64 z_demo cs opcodes (CS_CMD_CAMERA_POS,
  CS_CMD_MISC_ACTION, CS_CMD_LIGHTING, etc). Likely direct port with
  some field additions.
- **0x51..0x8e**: OoT3D-added opcodes for the refactored scripted
  playback (probably includes CS_CMD_SCENE_TRANS_FX,
  CS_CMD_ACTOR_ANIM_SET, and title-demo-specific opcodes).

Each cmd record is 16 bytes (0x10 stride, confirmed by FUN_00375750's
`command_table[i]` access being 0x10-strided).

## What this means for the port

- **Direct C port is feasible.** The interpreter is one 8964-byte fn.
  Decomp it fully via Ghidra headless, then hand-translate opcode by
  opcode. ~100 cases but most are 5-20 lines each (mostly LERP a
  keyframe against `csCtx.frames`).
- **N64 mirror gets us 40% for free.** SoH's `z_demo.c` already has
  ~40 opcode handlers with the same LERP shape — reuse them by mapping
  OoT3D opcode → equivalent SoH handler where the semantics match.
- **The 0x51..0x8e novelty is the real port work.** These need Ghidra
  RE case-by-case. Priority order: enumerate which of these appear in
  `title_cs.bin` (setup 0) first; those are the ones we actually need
  to play the title.

## Container header CORRECTED (was 20B hash + magic; actually 16B hash)

Re-read of first 48 bytes of `title_cs.bin`:

| Offset | Size | Field                                    |
|-------:|-----:|------------------------------------------|
| +0x00  | 16   | Hash / signature (differs per cs)        |
| +0x10  | 4    | Magic `" BDQ"` (LE 0x51444220)          |
| +0x14  | 4    | Version = 3                              |
| +0x18  | 4    | Header size = 8 (bytes past cs_len? tbd)|
| +0x1C  | 4    | Command-stream length (setup0: 0x12c0)  |
| +0x20  | 4    | Command count (setup0: 13)               |
| +0x24  | 4    | unk (setup0: 1)                          |
| +0x28  | 4    | unk (setup0: 1)                          |
| +0x2C  | 4    | End frame (setup0: 0x8ca = 2250 = 37.5s)|
| +0x30  | ...  | Commands begin                            |

(Prior journal's 20B/32B header numbers were off by 4; corrected here.)

## First-cut opcode scan tool

`tools/scan_oot3d_cs.py` parses the 48B header cleanly and walks
16-byte records from +0x30. Result: a **flat 16B walk under-decodes**
the command structure — most opcodes come out zero because commands
have variable-length inline sub-records (like N64 z_demo's
`CS_ACTION_LIST` shape). Real decoding needs the interpreter's
`puVar17 → puVar20` pointer arithmetic per case block.

The tool IS useful as-is for:
- Confirming header magic + version + command count + end frame
- Producing raw byte dumps for cross-referencing with FUN_002c5ba0's
  case-block decomp

## Concrete first port move (Phase 3)

1. **Extract opcode histogram from `scratch/oot3d_title_cs/title_cs.bin`.**
   Write `tools/scan_oot3d_cs.py` that parses the 32B header, walks
   16-byte cmd records from offset 0x30, and prints
   `opcode → count-in-title-cs`. Reveals which of the ~100 opcodes the
   title actually uses.

2. **For each opcode in the title histogram, decomp its case block**
   from FUN_002c5ba0 and translate to C in a new file
   `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d.c`. Skeleton:

   ```c
   typedef struct {
       Vec3f eye_start, eye_end;
       Vec3f at_start,  at_end;
       s16   start_frame, end_frame;
       // opcode-specific fields
   } Oot3dCsCmd;

   void Zelda3D_Oot3dCutscene_Update(PlayState* play,
                                     const Oot3dCsCmd* cmds, int count) {
       for (int i = 0; i < count; i++) {
           const Oot3dCsCmd* c = &cmds[i];
           if (play->csCtx.frames < c->start_frame ||
               play->csCtx.frames > c->end_frame) continue;
           switch (c->opcode) {
               case OOT3D_CS_CAMERA_POS: ...
               ...
           }
       }
   }
   ```

3. **Hook into SoH title-cs.** In `Shipwright/soh/src/code/z_play.c` or
   `z_demo.c`, when `gSaveContext.cutsceneIndex == 0xFFF3` AND scene is
   spot00 AND title-demo mode: dispatch to
   `Zelda3D_Oot3dCutscene_Update` INSTEAD OF the N64 `Cutscene_Update`.

4. **Verify per-frame parity** via the harness: for each frame N of the
   title playback, compare (Az's `csCtx.frames`, camera basis, player
   pos) vs SoH's ported title-cs at the same frame N.

## Files

- Decomp already in-repo:
  - `oot3d-decomp/build/decomp/002c5ba0.c` — interpreter (8964 B)
  - `oot3d-decomp/build/decomp/0023449c.c` — Scene_CmdCutsceneData
  - `oot3d-decomp/build/decomp/0037573c.c` — CsCtx_SetScript
  - `oot3d-decomp/build/decomp/00375750.c` — CsCmd_GetKeyframe
- CS bytes:
  - `scratch/oot3d_title_cs/title_cs.bin` — 11,680 B (setup 0)
  - `scratch/oot3d_title_cs/spot00_info.zsi` — 178,116 B
