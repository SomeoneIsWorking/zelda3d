# En_Horse module port + REPL spawn object auto-load (2026-07-16)

Two changes, one session — the `enhorse.module-port` RE-frontier step, and the isolated-testing tooling
that verified it.

## 1. En_Horse behaviors → `behaviors/actor/en_horse.{cpp,h}` (enhorse.module-port)

Structural port only — the underlying RE was already `re-verified` (render-gap, hoof-dust, rider-pos,
gallop-rate). Consolidated the two draw-adjacent EnHorse behaviors that had landed as pokes scattered
across two files into a dedicated per-actor module (CLAUDE.md structure rule):

- `Zelda3D_HoofDustWorldPos` — hoof-dust spawn-Y terrain-warp reconcile. **Moved from** `core/zelda3d.c`.
- `Zelda3D_HorseSaddleOffset` (#152 rider seat) + its draw-transform record — **moved from**
  `render/zelda3d_render.cpp`. The `sZelda3dHorseDraw*` statics became module-local (`sHorseDraw*`),
  populated via a new `Zelda3D_EnHorse_RecordDraw(actor, modelId, scale, groundOff)` that
  `Zelda3D_EmitModelDraw` calls once per replaced EnHorse draw (replacing the inline static writes).

Logic is byte-identical; only the TU boundary + the record indirection changed. Both functions keep
their C linkage and existing `zelda3d.h` declarations (comment updated to point at the new file).

## 2. REPL `spawn` now auto-loads the actor's object → spawn ANY actor in ANY scene

**Problem:** `spawn`/`spawnp` called `Actor_Spawn` directly, which only works if the actor's dependency
object is already resident in the scene. En_Horse couldn't be spawned in, e.g., Kokiri Forest (no
OBJECT_HORSE) — the actor fell back to object bank 0 and drew garbage / self-killed. This blocked
isolated actor testing (the whole point of a spawn command).

**Fix** (`Shipwright/soh/src/zelda3d/repl/zelda3d_repl.cpp`):
- New `Zelda3D_EnsureActorObject(play, actorId)` — looks up `ActorDB_Retrieve(actorId)->objectId` and,
  if not already loaded, calls `Object_Spawn(&play->objectCtx, objId)`. SoH gates actor init on
  `Object_IsLoaded` (which `Object_Spawn` marks true the same frame — DmaMgr is a no-op in SoH, asset
  bytes resolve through the resource manager), so this one call is sufficient for init + draw.
  `Zelda3D_SpawnInFrontP` calls it before `Actor_Spawn`.
- Unified `spawn` and `spawnp` into one handler: `spawn <name|0xID> [params]`. Raw `0x..` ids let any
  actor (not just `sModelTable` names) be spawned; params is optional (default 0). `spawnp` kept as an
  alias.

**Verified live** (Kokiri Forest, entrance 238):
- `spawn 0x14 6` → Epona (HORSE_EPONA, uses OBJECT_HORSE) spawns and persists, classified
  `AUTO:/actor/zelda_horse.zar (skin)`. Renders correctly (saddled, white mane/tail) —
  `scratch/screenshots/en_horse_spawn.png`. Proves the object auto-load.
- Relocated `Zelda3D_HorseSaddleOffset` fires through the full path: `[rider] PostDraw riderPos=...
  src=3ds-bone14` — i.e. it returns 1 using the module-local statics fed by `Zelda3D_EnHorse_RecordDraw`.
  This exercises exactly the moved code. Proves the module port.

### Gotcha for future spawns
En_Horse **params bit 0x8000 = HORSE_HNI** (the Ingo/black horse), which needs a *second* object
(OBJECT_HNI) and `Actor_Kill`s itself if it isn't loaded (`EnHorse_Init` ~L769). The auto-load only
loads the actor's PRIMARY object (from ActorDB), so `spawn 0x14 0x8003` still self-kills. Use
`spawn 0x14 6` (or `1`) for Epona. Actors needing a second object are the exception, not the rule.
