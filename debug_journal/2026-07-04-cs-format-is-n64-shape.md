# 2026-07-04 — MAJOR simplification: OoT3D CS format IS N64 z_demo shape

Follow-up to `2026-07-04-cs-interpreter-located.md`. Diffing the OoT3D
interpreter (FUN_002c5ba0) against SoH's `Cutscene_ProcessCommands`
(z_demo.c:1719) reveals near-identical outer-loop shapes.

## Side-by-side

**OoT3D FUN_002c5ba0 outer loop:**
```c
puVar17 = cs_bytes + 0x10;   // past cs_len + end_frame words
local_3c = 0;
while (true) {
    local_50 = *(u32*)puVar17;          // opcode
    puVar20  = puVar17 + 1;              // +4 bytes (opcode consumed)
    if (local_50 == 0xFFFFFFFF) break;   // -1 terminator
    switch (local_50) { ... }
    local_54 = *(u32*)(from switch);     // sub-count
    puVar20  = puVar17 + 2;              // +8 bytes (opcode + count consumed)
    for (i = 0; i < local_54; i++)       // per sub-record
        puVar20 += 0xc;                  // +48 bytes per sub (uint*)
    local_3c++;
    puVar17 = puVar20;                   // advance to next cmd
    if (local_3c >= local_4c) return;    // done
}
```

**N64 SoH `Cutscene_ProcessCommands`:**
```c
memcpy(&totalEntries,     ptr, 4);  ptr += 4;
memcpy(&cutsceneEndFrame, ptr, 4);  ptr += 4;
for (i = 0; i < totalEntries; i++) {
    memcpy(&cmdType,    ptr, 4);  ptr += 4;
    if (cmdType == -1) return;
    switch (cmdType) {
      case CS_CMD_MISC:
        memcpy(&cmdEntries, ptr, 4);  ptr += 4;
        for (j = 0; j < cmdEntries; j++) {
            func_80064824(play, csCtx, (void*)ptr);
            ptr += sizeof(CutsceneData) * 12;   // += 48 bytes
        }
        break;
      ...
    }
}
```

**Byte-for-byte identical shape:**
- 4-byte opcode
- 4-byte sub-count
- N × 48-byte sub-records
- outer loop counter, `-1` terminator

## What the GREZZO prefix hides

The 32-byte prefix (16B hash + `" BDQ"` + ver + hdr_size + 4B?) is
PADDING/METADATA that OoT3D layered on top of the N64 shape. The
actual N64-compatible cs data starts at **file-offset +0x18** in
`title_cs.bin` — where the (cs_len, end_frame) header words are.

Wait: N64 has (totalEntries, endFrame). OoT3D has (cs_len, cmd_count,
unk, unk, end_frame) — 20 bytes vs N64's 8 bytes. So SoH's parser
would need to skip the extra 12 bytes and use cmd_count as its
totalEntries.

Correct pointer arithmetic to feed SoH's parser:
- Skip 16B hash + 4B magic + 4B ver + 4B hdr_size + 4B cs_len = 32
  bytes, then read cmd_count → that's SoH's `totalEntries`.
- Skip 8B (unk + unk), then read end_frame → SoH's `cutsceneEndFrame`.
- Start walking cmds at file-offset +0x30.

## Opcode overlap

Confirmed via the case enumeration in FUN_002c5ba0 (see prior journal):
- **0x0b..0x50** — mirrors N64 z_demo cs opcodes. These decode with
  N64 handlers directly.
- **0x51..0x8e** — OoT3D additions. First cut: implement as
  "consume-subcount, no-op" stub. The outer loop still walks correctly
  and the ported N64 opcodes execute — only OoT3D-specific behaviors
  are missing.

Later port work: RE each 0x51..0x8e case block from FUN_002c5ba0 and
implement its behavior.

## Phase 3 minimum-viable port

**One commit lands basic title-cs execution.**

1. In `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d.c` (new):
   - `Zelda3D_TitleCs_Setup()` — loads `title_cs.bin` bytes into a
     buffer with SoH's expected `(totalEntries, endFrame)` prefix
     synthesized from the OoT3D GREZZO header.
   - `Zelda3D_TitleCs_Update(play, csCtx)` — wraps
     `Cutscene_ProcessCommands()` pointed at the reformatted buffer.
     Add stub cases for opcodes 0x51..0x8e that just advance the
     pointer.

2. In `Shipwright/soh/src/code/z_play.c` or z_demo.c: when scene is
   spot00 AND cs is title-demo, dispatch to
   `Zelda3D_TitleCs_Update` INSTEAD OF the N64 script driven by
   `cutsceneIndex=0xFFF3`.

3. Wire in via a scene-load hook so `title_cs.bin` gets loaded from
   the OoT3D ROM once (into a static buffer) at first title entry.

## What may still differ from Az

Even with a byte-accurate port of the N64-shape opcodes:

- **Actor spawn scheduling** at title. Az's spot00 setup 0 pre-spawns
  actors different from SoH's setup 7 (per `title_gamestate_v2.md`
  Player + Epona are Az-specific). Actor list ports separately from
  the CS.
- **Cam basis LH↔RH** already handled by
  `kZelda3dTitleEye/At/Up` static override.
- **PICA-off lighting** already matched (worldshade off at title).
- **Bloom + HDR tone map** — remain as
  `title_render_pipeline_scope.md` gaps 2 & 3. Not blockers on the
  scripted-path parity, just rendering-effects deltas.

## Files touched next session

- New: `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d.c`
- New: `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d.h`
- Edit: `Shipwright/soh/src/code/z_play.c` (dispatch hook)
- Baked: OoT3D `title_cs.bin` bytes via
  `tools/dump_oot3d_title_cs.py` output → embedded C array (not
  committed; loaded from ROM at runtime).
