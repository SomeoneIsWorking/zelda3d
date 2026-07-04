# 2026-07-04 — title envCtx RE: Ghidra attacks + one dead end

Follow-through from `2026-07-04-title-envctx-re-progress.md` and the
pinned650 arc. Goal: pin the envCtx layout in OoT3D so the title-cs
slot chosen at pinned cursor=650 can be RE-derived (not empirically
picked, per the "no tuning" directive).

## What ran this session

### Dead end: 0x001758f8 is a self-decrement countdown, NOT SetLighting

Prior journal noted 0x001758f8 as the sole ARM-mode match for
`sub #1; strb Rd, [Rn, #imm]` on base register R0 (play). Forced ARM
disasm around it (see `oot3d-decomp/tools/ghidra_scripts/DisasmThumb.py`
— new script, supports OOT3D_DISASM_MODE=arm|thumb + forced context reg):

```
001758ec  LDRB R1, [R0, #0x1C0]     ; R1 = *(u8*)(R0+0x1C0)
001758f0  CMP  R1, #0
001758f4  BEQ  0x00175908
001758f8  SUB  R1, R1, #1           ; the `sub #1` the coarse search hit
001758fc  ANDS R1, R1, #0xFF
00175900  STRB R1, [R0, #0x1C0]     ; STORES BACK to the SAME offset
00175904  B    0x00175910
00175908  LDR  R1, [PC, #4]         ; pool constant
0017590c  STR  R1, [R0, #0x1BC]
00175910  BX   LR
```

**This is a self-decrement countdown**: LDRB from `[R0,#0x1C0]`, SUB #1,
STRB back to the *same* offset. Cutscene_Command_SetLighting has the
shape `envCtx.unk_BF = cmd->setting - 1` — two different pointers
(`cmd` for the load, `envCtx` inside `play` for the store). The `sub #1
; strb` shape matched a countdown by chance. play+0x1C0 is a byte
countdown; play+0x1BC is a u32 that gets a pool-loaded value when the
counter hits zero. Neither is envCtx.

**Ruled out. Falsified note pinned here so the next session doesn't
revisit 0x001758f8.**

### ARM-mode fingerprint scan: 0 hits (CS handler likely Thumb)

Wrote `oot3d-decomp/tools/ghidra_scripts/FindCsSetLighting.py`:

- Scan .text (0x00100000..0x004C0000) for ARM SUBS with `Rn==Rd` and
  `imm==1` (mask `0xFFF00000 == 0xE2400000`, plus register-match).
- For each hit, walk forward up to 16 insns for an `LDR Rd, [PC, ±#k]`
  whose pool word equals `0x3F800000` (=1.0f, the `envCtx.unk_D8 =
  1.0f` write).
- Print (sub PC, ldr PC, pool VA, enclosing fn).

**Result: 0 hits.** Cutscene_Command_SetLighting is almost certainly
compiled in Thumb (matches the earlier finding that 0x001758f8's Thumb
region was unrecognized by Ghidra's fn table). The scanner needs a
Thumb-encoding fingerprint added:

- Thumb T1 `SUBS Rd, Rn, #imm3` with `Rn==Rd` and `imm3==1` →
  halfword `0x1E49` shape (bits `0001111_001_Rn_Rd`); the halfword
  value for `Rn==Rd==1` is `0x1E49`, but any small-reg `Rn==Rd` is
  fine — mask `0xFE00 == 0x1E00`, then check `Rn==Rd` and `imm3==1`
  from the low bits.
- Thumb `LDR Rd, [PC, #imm8*4]` pool load: `01001 Rd imm8` (halfword
  mask `0xF800 == 0x4800`); pool VA = `(PC & ~3) + 4 + imm8*4`.
- 1.0f neighbor filter as before.

Next session: extend `FindCsSetLighting.py` with the Thumb pass and
re-run.

### Live gl light-params probe added

Added REPL `soh_z3dlive` (`tools/soh3d_harness/main.cpp`) backed by
`SohState_Zelda3DLive` (`tools/soh3d_harness/soh_state.cpp`) reading
`gZelda3dAmbient / gZelda3dLight1Col / gZelda3dLight2Col` — the values
`Zelda3D_GL_SetLightParams` pushed into the shader-side globals this
frame. Bypasses the envCtx→lightSettings path so we see exactly what
the renderer sees after all overrides. Useful to verify at pinned
cursor whether the diffuse-term pre-bake is receiving non-zero light1
color, and to detect any SoH-side override diverging from the palette
values that the RE work is pinning down.

Also added a comment block to `zelda3d_sdl3gpu.cpp` documenting the
grass-material data point from the pinned650 diagnostic
(`matAmb=(1,1,1) matDif=(0,0,0) combScale=2`) so the shader reader
sees the material context without re-deriving it.

## Concrete plan for the next Ghidra session

1. Extend `FindCsSetLighting.py` with Thumb-encoding scan (see above).
2. For each hit, decompile the enclosing fn — filter to fns that ALSO
   contain a `STRB Rd, [Rn, #imm]` writing R0's field within 8 insns
   of the SUB (that's `envCtx.unk_BF = ...`). The strb IMMEDIATE from
   R0 is the play-struct offset of `envCtx.unk_BF`. Subtract the N64
   layout offset within envCtx (0xBF-0xB8=+7 past lightSettingsList)
   to derive envCtx's play-struct base.
3. Cross-check: the same fn should also contain a `STR Rd, [Rn, #imm2]`
   with the 1.0f pool value; `imm2 - imm == 0x19` (0xD8-0xBF) confirms
   the fn is CS_SetLighting.

## Files touched (uncommitted so far — session ended before commit)

- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — inline
  comment block (grass-material data point)
- `tools/soh3d_harness/main.cpp` — REPL `soh_z3dlive`
- `tools/soh3d_harness/soh_state.cpp` — `SohState_Zelda3DLive`
- `oot3d-decomp/tools/ghidra_scripts/DisasmThumb.py` — new: forced
  Thumb/ARM disasm with context-reg override
- `oot3d-decomp/tools/ghidra_scripts/FindCsSetLighting.py` — new,
  ARM-only pass (Thumb pass TODO)
