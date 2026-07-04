# 2026-07-04 — Az title-demo envCtx RE (in progress)

Follow-through from 2026-07-04-title-parity-pinned650.md. After the
lightslot sweep was rejected as tuning (user directive: "RE and port
please, not like this"), pivoted to locating Az's runtime envCtx values
directly.

## Progress made

### spot00 lightSettings palette LOCATED in Az memory

Corrected ZSI 28-byte layout per `tools/gen_oot3d_scene_lighting.py`
docstring:

```
+0x00 u8[3] ambient   +0x03 pad   +0x04 u8[3] l0col   +0x07 s8[3] l0dir
+0x0a u8[3] l1col    +0x0d s8[3] l1dir   +0x10 f32 fogEnd   +0x14 f32 drawDist
```

(My initial search used the SoH C-struct layout `amb l0dir l0col l1dir
l1col` which is a RE-DERIVED tuple, not the raw ZSI order.)

4 palette instances found in Az heap:
- `0x0877dc74` (ZSI-loaded source blob)
- `0x0877dec8` (+0x254; second occurrence within same blob region)
- `0x099d7284` (runtime working copy in a different heap allocation)
- `0x099d74d8` (+0x254; second occurrence in that heap allocation)

All 17 slots dumped match SoH's `zelda3d_scene_lighting.inc:kSlots_spot00[]`
byte-for-byte. Palette source is IDENTICAL between engines — the divergence
must be WHICH slot Az actively uses at title, not what values are in the
palette.

### envCtx pointer NOT YET LOCATED

No pointer to any palette VA found:
- Play struct (0x0871E840..0x08726840, 32KB scanned): 0 hits
- Az `.data` (0x00500000..0x00600000): 0 hits
- Wider heap (0x08000000..0x0A000000): 0 hits for palette base itself

Access must be via a BASE_PTR + LARGE_CONSTANT_OFFSET pattern (immediate
form), not a stored pointer field. Or the palette is referenced through a
struct field whose VA I haven't dumped.

Adjacent pointer finds in play struct:
```
play+0x229c = 0x0877df48   (+724 from palette base — scene-data blob)
play+0x22d8 = 0x0877e1b0   (+1340)
play+0x22fc = 0x0877e368   (+1780)
play+0x2320 = 0x0877e3a0   (+1836)
play+0x3230 = 0x0877ded8   (+612)
play+0x5c08 = 0x0877de60   (+492 — just past palette end)
play+0x5c0c = 0x0877dea8
play+0x5c18 = 0x0877dea4
play+0x5c1c = 0x0877deb8
```

These are likely other ZSI-cmd payloads (skybox, environment, room-list)
within the same scene-data blob. The `play+0x5c08` region matches the
prior-RE'd transition/scene-control area at `play+0x5c2d = transitionTrigger`.

## Next attacks

1. **JIT watchpoint** on Az memory 0x099d7284 (runtime palette copy). Its
   writer at boot IS the palette install — its enclosing fn's decomp
   reveals the envCtx offset it writes to.

2. **Ghidra static** — search for `movw/movt` pairs materializing
   `0x099d7284` or `0x0877dc74`. Instructions using those constants are
   inside the Environment_Update-equivalent; the fn body reveals envCtx
   layout.

3. **Play-struct diff by SLOT change** — pin cursor at cursor=650 and
   cursor=750 (across the shot cut at cursor=755 per prior demo-loop
   analysis). The 22-byte lightSetting values must differ across shot
   cuts (different CS_CMD_SET_LIGHTING slot). Find bytes that change:
   3-byte and 22-byte spans within play struct that match a KNOWN slot
   pair after the diff.

4. **Structural scan** for a struct-field-shape `{slot_index_u8,
   blend_factor_f32, ptr_to_palette_base}` — the OoT3D EnvironmentContext
   equivalent likely has these near the top.

## Session-progressive parity metrics (unchanged from previous journal)

- Camera |Δdir|: 1.4143 → **0.0001** (metric bug fix, RE-driven)
- Ground MISSING: 27690 → 7952 (-71%) — shader compound-dim fix (RE-driven)
- Full-frame closer via empirical slot pick: REJECTED, reverted

## Files

- `scratch/find_az_palette.py` (first-attempt, wrong layout)
- `scratch/find_az_palette_v2.py` (correct 28-byte ZSI layout — this
  worked)
- `scratch/find_az_env_live.py` (palette dump + pointer scan)
- `scratch/find_az_envctx.py` (structural lightSetting-shape scan)

## Update — palette is in the SCENE-DATA blob, not envCtx

Dumping 256 bytes BEFORE the source palette VA `0x0877dc74` reveals
actor-list-shaped data (`0fff0000 <params> <pos> <rot>` records with the
`0x0FFF` flag mask characteristic of OoT Actor.flags). So `0x0877dc74`
sits inside a scene-setup ZSI blob that also contains the room's actor
list. It's the ROM-loaded scene payload, not the runtime env state.

The runtime envCtx must be a separate heap allocation whose pointer
lives inside play struct. All 17 lightSettings-slot fingerprint scans of
play (32KB, 4KB chunks) returned 0 hits — meaning the LIVE blended
ambient values do NOT match any raw palette slot byte-for-byte. This is
consistent with Environment_Update lerping between adjacent slots (the
blended result rarely equals a slot exactly).

Ghidra approach — post-actor fn (`FUN_002e25f0`, called from Play_Main
AFTER Actor_UpdateAll, 12 KB decomp) accesses many `param_1+0x17xx`
byte-offset fields. These could be envCtx.skyboxDisabled / unk_BF /
etc. based on N64 layout adjacency. Confirming which specific offset
holds envCtx.lightSettings.ambientColor requires cross-referencing the
Cutscene_Command_SetLighting equivalent — that fn writes exactly
`envCtx.unk_BF = cmd->setting - 1` (a u8 field), which pins the offset
directly. Prior LR-chain RE showed pose eval on the update thread; the
CS-lighting handler would be on the same update thread's dispatch, so
watchpointing a candidate u8 slot at cursor pinning to catch CS write
would identify envCtx.unk_BF's exact play-offset. Then envCtx base +
lightSettings offset is derivable.

Deferred — the arc is well-scoped; the next session's tools are the
harness watchpoint + Ghidra decomp of the CS-lighting handler. Session
ran out of runway; full parity target is the same but the path is
identified.

## Additional Ghidra-side findings (2026-07-04 late)

- **Prior Play_Main decomp had one incorrect BL line**: Ghidra printed
  `FUN_002e25f0(param_1)` as the third call after Actor_UpdateAll, but
  the actual BL sequence in `Play_Main` (0x0045238c..0x00452598) is:
  ```
  0x004523f0  BL 0x002f70c4   (early sync)
  0x004523fc  BL 0x0034fc6c   (early sync)
  0x0045241c  BL 0x002e4514   (delayed-effect cleanup, 252 B)
  0x00452428  BL 0x002e2e60   (Actor_UpdateAll, 5372 B)
  0x004524b8  BL 0x00347258   (712 B — keyframe evaluator, per-entry;
                                   NOT env update)
  0x0045252c  BL 0x00343280   (memset0 utility, 84 B)
  ```
  So Environment_Update-equivalent must be nested INSIDE
  FUN_002e2e60 (Actor_UpdateAll) or elsewhere in the update thread —
  it isn't a direct Play_Main call.

- **Palette-slot-size (0x1C) static scan**: 1024 uses of the constant
  0x1C across the binary. Top fns by usage:
  - FUN_002c8434 × 57 (utility-heavy — buffer stride?)
  - FUN_0014da40 × 15
  - FUN_002ee864 × 14
  These are candidates for the palette-indexing loop but need
  filtering by "index * 0x1C + palette_base" pattern (not just
  0x1C as a constant).

- **Cutscene_Command_SetLighting search**: 138 candidates matching
  `sub #1; strb Rd, [Rn, #imm]` across the binary. Filtered by
  base register R0 (play), only ONE match at 0x001758f8 — but no
  containing fn in Ghidra's fn table (Thumb-only fn, unrecognized).
  Decompiling adjacent code and identifying the fn boundary is the
  next Ghidra step.

- **The one small fn with a 1.0f pool constant** (candidate for
  Cutscene_Command_SetLighting via "stores 1.0f" heuristic) was
  FUN_0021cde4 (126 B) — decompiled but turned out to be a
  Math_MatrixCopy-shape VFP-based float copy. Not the target.

## What actually WOULD work next session

The palette VAs at 0x0877dc74 (source) and 0x099d7284 (runtime copy)
were LOCATED — the whole 17-slot spot00 palette is byte-identical to
SoH's `zelda3d_scene_lighting.inc`. The remaining unknown is which
slot Az actively uses at title.

Concrete next-step attack (RE-driven, not tuning):

1. Extend `watchhook.cpp` to also catch READs (currently only
   writes). Watch reads to 0x099d7284..0x099d7460 (whole palette).
   Environment_Update-equivalent WILL read slot X during the CS-active
   window; hitting a specific slot-X's byte pinpoints X.

2. Or Ghidra static-analyze FUN_00347258 (712 B, called from Play_Main
   right after Actor_UpdateAll) — it may contain the palette lookup +
   ambient blend even if it's tagged "keyframe evaluator" per its
   FUN_00350820(anim_ptr, ..., 0x60, 3) call. Env update's lerp between
   two slots IS a per-channel keyframe blend — same evaluator subsystem.
