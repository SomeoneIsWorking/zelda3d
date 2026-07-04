# 2026-07-04 — title lighting divergence PINNED to cs stream (not shader)

Follow-up to `2026-07-04-title-envctx-live-trace.md`. Extended `compare
lighting` to also read SoH's `envCtx.unk_BF` (and prev/weight). Side-
by-side at title:

```
default title (no cursor pin):
  3ds: envCtx@0x08721975  slot=7  prevSlot=71  lerpWeight=0.000
  soh: slot=1              prevSlot=1              lerpWeight=1.000
       envLightSettings ambient=(40,35,77) ...
```

**Az picks spot00 slot 7, SoH picks spot00 slot 1.** Both engines have
byte-identical spot00 palettes (proven earlier). Both engines pick
from the CS-driven path (unk_BF < 32, not 0xFF/0xFE). Both settle
their lerp (SoH lerpWeight=1.0).

## Not a shader defect — it's the CS command stream

The gap is NOT in the shader math. It's not in the palette. It's in
which slot the title-demo cutscene picks.

- SoH runs N64 OoT's title-demo cutscene bytes (baked into
  Shipwright's ROM/asset data — a `CutsceneCmd_EnvironmentLighting`
  entry list with (startFrame, endFrame, setting) triples).
- Az runs OoT3D's title-demo cutscene bytes (in the 3DS ROM's scene
  data) — a REWRITTEN sequence that fires SET_LIGHTING commands
  targeting a different slot progression.

Az's actual slot progression across the demo (from the harness trace):

```
cursor    slot   (Az)
    50      8    shot 0 early
   200      8
   400      8
   600      7    shot 0 late
   700      7
   800      9    shot 1
   900      9
  1100      8
  1400      8
  1700      8    shot 2 setup
  2000      7
  2100      7
  2400      6    shot 2 end
  2700      6
```

Six distinct slot transitions across three demo shots. SoH's N64 title
cs picks slot 1 constant (or lerps between 1 and something adjacent).

## Why the earlier "empirical sweep to slot 12" was a bandaid

The reverted `SOH3D_TITLE_LIGHTSLOT=12` override was a symptom fix —
picking whatever fixed value least-badly matched the average frame.
The real defect is that SoH's title-cs is playing the N64 sequence
against an OoT3D-styled expected-render. No single slot value can
close a dynamic sequence.

## Two paths forward

1. **Port OoT3D's title-cs SET_LIGHTING commands into SoH.** Right
   architecture: the OoT3D title cs is data in scene 0x51 / 0x6B. RE
   the 3DS cutscene bytes (needs a small RE arc — locate the cs
   header + walk the command list), extract the (startFrame, endFrame,
   setting) triples, port them into SoH's cutscene data OR into
   Zelda3D_ApplyTitleCam as a scripted override that mirrors the CS
   stream cursor-by-cursor. This is the port, not a workaround.

2. **Live-mirror Az's unk_BF while the harness is running.**
   Development-only. Adds a REPL `az mirror envctx` command that
   copies Az's slot into SoH's envCtx.unk_BF each frame. Useful for
   proving the shader math + rest of the pipeline reaches parity when
   the CS input matches. NOT the port; a diagnostic before it.

Path 1 is the correct next RE arc. Path 2 needs one more small piece
of tooling (write into SoH via an extended SohState_SetEnvSlot) and
would confirm that once CS parity is achieved, the visual gap fully
closes (or highlight any remaining shader-side residual).

## Harness change committed this session

- `SohState_Lighting` extended to output `envCtx.unk_BF`,
  `envCtx.unk_BD`, `envCtx.unk_D8` for the soh side.
- `CompareLightingImpl` prints them in a `slot=X prevSlot=Y lerpWeight=Z`
  line adjacent to the 3ds line — direct A/B diff.
- Diagnostic script `scratch/slot_ab.py` walks title cursors and
  prints Az vs SoH slots for a full trace.

## Files

- `tools/soh3d_harness/soh_state.cpp` — new outUnkBF/BD/D8 params
- `tools/soh3d_harness/main.cpp` — `SohState_Lighting` decl + printf
- `scratch/slot_ab.py` — the AB driver (gitignored)
- `scratch/soh_playstate_probe.py` — playstate-boot smoke test
  (gitignored)
