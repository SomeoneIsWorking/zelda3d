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
