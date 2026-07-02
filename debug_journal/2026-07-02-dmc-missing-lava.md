# 2026-07-02 — Death Mountain Crater has NO lava / crater floor in SoH3D

## Sweep tool
`ZELDA3D_HEADLESS=1 python3 tools/parity_ab.py 0x147 --time 0x8001 --name dmc`
Composite: `scratch/screenshots/ab_dmc_cmp.png`.

## Symptom
SoH3D at scene 0x61 (SCENE_DEATH_MOUNTAIN_CRATER, "spot17"):
- Room 0 (`roomwarp 0`): dark red-lit rock tunnel, floor of stone, no lava.
- Room 1 (spawn): similar rocky corridor, wooden crate prop.
- No open crater bowl, no lava rivers, no molten pools, no distant volcano wall.

OoT3D oracle at the same entrance shows: a large open crater with lava rivers, molten
pools, orange particles, distant caldera walls, signposts. The complete "crater" scene
that DMC is famous for.

## Reproducing
```
source .env
tools/oracle_boot.sh
ZELDA3D_HEADLESS=1 python3 tools/parity_ab.py 0x147 --time 0x8001 --name dmc
# then in a running SoH3D:
tools/zelda3d_repl.py cmd "roomwarp 0"
tools/zelda3d_repl.py shot dmc_room0
```

## Root cause: **UNCLEAR — needs investigation**
Both rooms load a CMB (SoH3D isn't falling back to N64 — the rock walls are the
OoT3D `spot17_room_[01]` mesh). But the lava planes / crater bowl aren't visible.
Hypotheses to check:
1. **XLU planes stripped**: DMC lava may be authored as XLU (translucent) meshes in
   the room CMB that `Zelda3D_TryDrawRoom` / `Zelda3D_DrawRoomGL` skips. Related
   precedent: [[soh3d-water-rendering]] (#103) — Lake Hylia water body was actor
   c_s06beforewater, previously skinned-skip. Different mechanism (there it was
   an actor, here it might be a scene-CMB XLU pass), but same class of "translucent
   plane omitted".
2. **Scene-room wrong CMB**: DMC in OoT3D might store the crater proper in a room
   OTHER than 0 or 1 (multi-room scene where the crater is room N ≥ 2). Check
   `/scene/spot17/*` in the OoT3D romfs listing — how many rooms is DMC?
3. **Sub-scene ("beforewater"-style) actor**: OoT3D might spawn a per-scene "lava
   surface" actor (mirror of c_s06beforewater for lava) that Zelda3D's actor
   dispatch skips.

Verification steps for a next session:
- Enumerate the .zsi/.cmb files under `/scene/spot17/` in the OoT3D romfs. If there
  are >2 rooms, SoH3D is missing them. If there are exactly 2, look at each CMB's
  material breakdown (OPA vs XLU meshes).
- Check `Zelda3D_DrawRoomGL` for XLU handling — it currently emits into `POLY_OPA`;
  translucent scene meshes may need `POLY_XLU`.
- Grep OoT3D EnLava / EnDmc actor overlays that spawn per-scene (in
  `oot3d-decomp/build/decomp/`).

## Status
NOT FIXED. Recorded for next-session pickup — needs the OoT3D romfs enumeration
+ scene-CMB XLU handling before a port is possible. Not a kanban card (per
user directive parity findings live here, not on the board).
