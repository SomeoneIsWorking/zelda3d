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

## Next-session concrete moves

1. **Decode the CS format via Ghidra:**
   - `FUN_0023449c` = Scene_CmdCutsceneData (per `scene_command_handler.md`)
   - Its callee that consumes the cs blob is the CS interpreter — find
     via forward xref from FUN_0023449c on the ptr it stores.
   - Identify: 20-byte prefix (skip? checksum?), 4-byte length field,
     command-stream opcode table.

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
