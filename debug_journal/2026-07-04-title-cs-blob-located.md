# 2026-07-04 — OoT3D title cs blob LOCATED (Phase 1 done)

**Direction:** port OoT3D's title-demo scripted path (see
`PLAN-title-scripted-port.md`).

## Result

The OoT3D title-demo cutscene bytes are at:

- **File:** `/scene/spot00_info.zsi` in decrypted OoT3D RomFS
- **Offset:** `0x28a24`
- **Length:** `0x2da0` = 11,680 bytes

Located by walking spot00's ZSI scene-header commands and finding the
CutsceneData reference. Repro: `python3 tools/dump_oot3d_title_cs.py`
(writes to gitignored `scratch/oot3d_title_cs/`).

## spot00 scene-header cmd list (verified)

```
off=0x0010 cmd=0x18 count= 13 ptr=0x000004c8   # AltHeaders / setup table
off=0x0018 cmd=0x15 count=  2 ptr=0x01000585
off=0x0020 cmd=0x04 count=  1 ptr=0x000004fc   # RoomList
off=0x0028 cmd=0x19 count=  0 ptr=0x00000000
off=0x0030 cmd=0x03 count=  0 ptr=0x00016174   # Collision
off=0x0038 cmd=0x06 count= 18 ptr=0x000161a0   # EntranceList
off=0x0040 cmd=0x07 count=  1 ptr=0x00000002   # SpecialFiles
off=0x0048 cmd=0x0d count=  1 ptr=0x00016250
off=0x0050 cmd=0x00 count= 18 ptr=0x00016258   # SpawnList
off=0x0058 cmd=0x11 count=  0 ptr=0x00000001   # SkyboxSettings
off=0x0060 cmd=0x13 count=  0 ptr=0x00016378
off=0x0068 cmd=0x0f count= 17 ptr=0x00016398   # TransitionActorList
off=0x0070 cmd=0x14 count=  0 ptr=0x00000000   # End
```

## cmd 0x18 table shape (13 × 8B at ptr 0x4c8)

Not a flat AltHeaders list — the payload is a **mini scene-header per
setup**, inlined. Interpretation:

| idx | word_a (LE)  | word_b (LE)  | shape                          |
|----:|--------------|--------------|--------------------------------|
| [0] | `0x00000017` | `0x00028a24` | inline SceneCmd: **CS_CMD_CUTSCENE_DATA, ptr=title_cs** |
| [1] | `0x00000014` | `0x00000000` | inline SceneCmd: **END**       |
| [2-7] | offset_A   | offset_B     | setup-N alt-header offsets (in-file) — each contains a nested (0x17 ptr) + (0x14 0) sequence |
| [8-11] | ASCII bytes | ASCII bytes | string pool: `"rom:/scene/spot00_0_info.zsi\0"` |
| [12] | 0            | 0            | terminator |

Verified: entry[7].b = 0x408 dumps `17000000 8c4a0200 14000000 …` — a
setup-7 alt-header pointing to a **second** cs script at file-offset
`0x24a8c`. So setup 0 uses the title cs (0x28a24), setup 7 uses a
different cs (0x24a8c). Which one SoH's title-demo actually plays
depends on how Play_Init consumes sceneSetupIndex=7 on the OoT3D
engine.

**TODO:** verify whether Az's title cursor 0 runs on setup 0's cs
(0x28a24) or setup 7's cs (0x24a8c) — dump both and compare against
Az's live cs frame counter (memory VA `0x0054CC3C`).

## title_cs.bin format (first 64B)

```
+0000: 4f48484813314bb8b8b8316495000a13   ; 20-byte GREZZO signature/hash
+0010: 20424451                           ; magic " BDQ" (or a version stamp)
+0014: 03000000                           ; version = 3?
+0018: 08000000                           ; header size = 8?
+001c: c0120000                           ; total cs length = 0x12c0 = 4800 bytes
+0020: 0d000000                           ; command count = 13?
+0024: 01000000                           ; ?
+0028: 01000000                           ; ?
+002c: ca080000                           ; end frame = 0x8ca = 2250 frames?
+0030: 0000000000000000feffffff10000000   ; command 0? (0xfffffffe = -2 = END_MARKER?)
```

The magic ` BDQ` and 20-byte prefix suggest a versioned wrapper — NOT
N64's raw command stream. Needs Ghidra decomp of the CS interpreter
entry point to decode.

## Format decoded further via header comparison (setup0 vs setup7)

Dumping the SECOND cs blob (setup 7, at file-offset 0x24a8c) with the
same tool confirms the container format is stable across cutscenes:

| Offset | Setup 0 | Setup 7 | Interpretation                          |
|-------:|--------:|--------:|-----------------------------------------|
| +0x00  | (hash A)| (hash B)| 20-byte GREZZO signature/hash (differs) |
| +0x14  | `20424451` | same | Magic `" BDQ"` (constant)              |
| +0x18  | 3       | 3       | Version = 3 (constant)                  |
| +0x1C  | `0x12c0` | `0x1059`| Command-stream length (differs)        |
| +0x20  | 13      | 10      | Command count                           |
| +0x24  | 1       | 3       | ?                                       |
| +0x28  | 1       | 0x38    | ?                                       |
| +0x2C  | `0x8ca` | `0x3fc` | End frame (setup0 = 2250 = ~37.5s @60fps; setup7 = 1020 = ~17s) |
| +0x30  | cmd table start                              |

So the header is a **32-byte GREZZO cutscene container** (`GREZZO_CS_V3`
= " BDQ" 3), followed by command records, followed by parameter/keyframe
data referenced from commands. Total blob length = header + command
stream + keyframe pool.

Command records: the byte stream at +0x30 in setup0 is
`00000000 00000000 feffffff 10000000 00000000 feffffff 10000000
00000000 00000000 00000000 2d000000 00000000 0b000000 01000000 ...`.
That's a mix of 16-byte and 8-byte-ish records — needs the CS
interpreter decomp to disambiguate.

## What the RE hit this session

- **FUN_0023449c = Scene_CmdCutsceneData handler** (40-byte
  trampoline). Confirms the cs script pointer path:
  `csctx_or_play[0x229c] = scene_data_base + cmd[4]`.
- **FUN_0037573c = CsCtx_SetScript**: sets `+0x229c` (script ptr) and
  clears `+0x22ac` (frame counter). 20-byte fn.
- **FUN_00357ea0 = CsCtx_GetScript**: reads `+0x229c`. 12-byte fn.
- **FUN_0037571c = CsCtx_GetU8At_0x22a0**: reads `+0x22a0` (u8 state?).
- **FUN_00375750 = CsCmd_GetKeyframe(cs_ctx, idx)**:
  - `cs_ctx + 0xC` = command table base (16-byte stride)
  - `cs_ctx + 0x34` = current command index (-1 = idle)
  - `cs_ctx + 0x60` = keyframe u32 pool base
  - Returns `keyframe_pool[idx]` if `idx < *(command_table[current_cmd])`.
- **Every command record is 16 bytes** (0x10 stride confirmed by the
  ARM decomp of FUN_00375750).

So the csCtx (or a sub-struct of it, offset TBD from PlayState base)
has this layout:

| CsCtx offset | Field                        |
|-------------:|------------------------------|
| +0x0C        | cmd table base ptr (16B stride) |
| +0x34        | current cmd index (int; -1 = idle) |
| +0x60        | keyframe u32 pool base ptr  |
| +0x229c      | raw script ptr (installed by CsCtx_SetScript) |
| +0x22a0      | u8 state byte               |
| +0x22ac      | frame counter (u32? cleared to 0 by SetScript) |

## Live-verify handle (harness)

Az play @ VA `0x0871E840`. So:
- `r32 0x087210F4` (= play+0x08 or a csCtx sub-struct? — TBD) — check
  the parsed csCtx pointer if csCtx is a nested struct.
- `r32 0x08720ADC` (= play+0x229c) — if `+0x229c` is a PLAY offset,
  this reads the current cs script ptr. Expected value at title:
  `0x28a24 + scene_data_base` (setup 0) or `0x24a8c + scene_base`
  (setup 7). Distinguishes which setup Az actually runs.
- `r32 0x08720AF4` (= play+0x22ac) — frame counter. Should advance
  monotonically during title playback.

## Next-session concrete moves

1. **Find the CS interpreter fn.** The one that reads the raw script
   at `+0x229c` and populates the parsed cmd-table at `+0xC` +
   keyframe pool at `+0x60`. It runs once per csctx install (i.e. at
   scene load, not per-frame). Candidates: FUN_00358188 (2253 bytes)
   or a fn in `00375...` family. Find via callers of FUN_00357ea0
   whose first action is `LDR base, [_, #0x229c]; skip 32; walk cmds`.

2. **Find the per-frame CS advancer.** The one that increments
   `+0x22ac` (frame counter) and evaluates cmds. Likely called from
   `Play_Main` or a sub-fn. Candidates: search fns that STORE to
   `+0x22ac` (not just SetScript's clear-to-0).

2. **Confirm which setup Az's title plays:**
   - Harness: read Az's active cs script pointer from PlayState (offset
     within csCtx — TBD from RE) at settled title. Compare against
     0x28a24 (setup 0) and 0x24a8c (setup 7).

3. **Once opcode format is decoded:** enumerate opcodes used by
   title_cs.bin. Should be a small set (~10-20). Map each to behavior
   via Ghidra decomp of the interpreter's switch.

## Falsifies

- Earlier assumption that "no cs port target exists because PICA
  lighting is off." The PICA-off note is correct (see
  `title_lighting_disabled.md`) — but the cs script is what drives
  camera/actor/timing, which is the actual scripted-path target.

## Files

- `tools/dump_oot3d_title_cs.py` — repro tool (in-repo)
- `scratch/oot3d_title_cs/title_cs.bin` — 11,680 B blob (gitignored)
- `scratch/oot3d_title_cs/spot00_info.zsi` — full scene header (gitignored)
