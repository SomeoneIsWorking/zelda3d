# 2026-07-04 — CS format CORRECTED — opcode-specific sub-record strides

Follow-up to `2026-07-04-cs-format-is-n64-shape.md`, which was
**partially wrong**. Fresh read of FUN_002c5ba0 prologue (lines 56–100)
+ case bodies for opcodes 0..3 reveals the real structure.

## The interpreter's per-frame call shape

The interpreter takes a 16-byte **"cs header"** pointed at by param_3.
It does NOT consume the raw GREZZO blob directly:

```c
FUN_00470758(auStack_40, param_3,        4);   // dummy? not used later
FUN_00470758(auStack_44, param_3 + 4,    2);   // u16 (unused)
FUN_00470758(auStack_48, param_3 + 6,    2);   // u16 (unused)
FUN_00470758(&local_4c,  param_3 + 8,    4);   // cmd_count (outer loop count)
FUN_00470758(&local_58,  param_3 + 0xc,  4);   // end_frame

if (end_frame < csCtx.frames && csCtx.state != 4) { advance/end }
else {
    puVar17 = param_3 + 0x10;                  // command stream starts here
    for (i = 0; i < cmd_count; i++) {
        opcode = *(u32*)puVar17;
        if (opcode == -1) break;
        // dispatch (see below), which updates puVar20 to end of this cmd
        puVar17 = puVar20;
    }
}
```

Somewhere upstream, whatever calls FUN_002c5ba0 passes it a pointer to
a specific 16-byte header in the GREZZO blob. **That mapping is
still open** — needs the caller RE.

Empirical alignment guess: if `param_3` = file offset `0x20`, then the
interpreter reads cmd_count = 1 (from `+0x28`) and end_frame = 2250
(from `+0x2C`). With cmd_count = 1 and opcode = 0 at `+0x30`, the
whole title cs would be a single no-op — which visibly is not what Az
runs. So this alignment isn't right; the caller must pass a different
`param_3`.

## Sub-record strides are OPCODE-SPECIFIC

Falsifies the "always 48B stride" claim in the prior journal. The
default case (caseD_b) uses 48B, but opcodes 0..0xA use a completely
different shape:

**Opcode 0** — no-op / skip / padding.

**Opcodes 1, 2** — camera POS / camera FOCUS keyframe track:
```
+0x00: opcode u32 (= 1 or 2)
+0x06: u16 startFrame
+0x08..+0x09 (low u16 of puVar17[2]): u16 endFrame
+0x0C..: N × 16-byte atoms, terminated by atom[0].byte0 == -1
```

**Opcode 3** — MISC action with 48B sub-records + inner-switch:
```
+0x00: opcode u32 (= 3)
+0x04: u32 sub_count
+0x08..: sub_count × 48B records; each record's u16 at +0
        is a MISC action id dispatched in an inner switch
```

**Opcodes 0x0B..0x8E** — mostly 48B stride records (npc-action-like).

**Terminator** — opcode == `-1` (0xFFFFFFFF).

## Consequences for the port

**The "point SoH's Cutscene_ProcessCommands at OoT3D bytes" strategy
in prior journal WON'T work.** Reasons:

1. **Opcode numbers don't match.** OoT3D opcode 1 = camera POS;
   SoH's is `CS_CMD_SET_CAMERA_POS = 0x0F`. Direct feed misrouts every
   command.
2. **Sub-record strides differ per opcode.** SoH walks all opcodes at
   48B stride EXCEPT camera opcodes which call a bespoke
   `Cutscene_Command_CameraPositions`. OoT3D's opcode 1/2 also use
   inline-atom lists, but the record shape/atom size may differ.
3. **The 16-byte header the interpreter reads is at some unknown
   offset in the blob**, not the naive file-offset-0x20 assumption.

**Real port strategy** (revised):

- **Write a fresh OoT3D CS interpreter** in
  `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d.c` — a straight
  translation of FUN_002c5ba0's outer loop + per-opcode case blocks.
- **Do NOT reuse SoH's `Cutscene_ProcessCommands`.** Reuse only the
  effect handlers (e.g. `Cutscene_Command_SetLighting`,
  `Cutscene_Command_PlayBGM`) where they exist and semantically match.
- **First implement opcodes 0, 1, 2** (no-op + camera pos/focus) —
  those alone should get the title camera trajectory playing.
- Layer additional opcodes (misc actions, npc cues, lighting) as they
  come up on the title timeline.

## Open questions for next session

1. **Who calls FUN_002c5ba0, and with what `param_3`?**
   - `FUN_002c5ba0` has zero static callers in decomp (function-pointer
     table). RE requires: (a) find the fn-ptr slot that holds
     0x002c5ba0 (grep .data for the address literal), (b) find the
     dispatcher that indexes that slot, (c) trace how it computes
     `param_3` from the cs blob.
   - Alternative: harness live-trace. Watch `csCtx.script` (+0x229c)
     set on Az at title, breakpoint FUN_002c5ba0 entry, dump the R2
     register at that call to get the actual `param_3`.

2. **Where in the blob is the 16-byte "cs header" the interpreter
   consumes?** Once question 1 is answered, this falls out.

3. **How does the OoT3D csCtx `+0x50..+0x68` field family map to
   SoH's `csCtx.npcActions[]`?** These are the pointer slots the
   interpreter writes when a cue matches — porting them semantically
   means the actors read them at the same offsets.

## What's still solid

- CS blob is at file-offset 0x28a24 in spot00_info.zsi, 11,680 bytes,
  wrapped in the GREZZO container (magic " BDQ", version 3).
- FUN_002c5ba0 IS the interpreter (8964 bytes, ~100 opcodes).
- Opcode enum landed in
  `Shipwright/soh/src/zelda3d/zelda3d_cutscene_oot3d_opcodes.h`.
- Format details (byte layouts, stride variants) documented here.

## Files

- Corrected: this journal.
- Prior journal `2026-07-04-cs-format-is-n64-shape.md`: keep for
  reference but its "point SoH's parser at OoT3D bytes" plan is
  falsified — see this journal's "Consequences" section.
