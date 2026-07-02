# 2026-07-02 — Kokiri Forest (ent 0xEE, scene 0x55) structured actor audit

Sweep tool: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart 0xEE 0x8001` +
`actorsnear 10000`. Second sweep after the Market Day one, same methodology.

**Invariant reminder**: N64 rendering is a gap, not an acceptable state.

## Per-actor render path

| id | actor | count | render path | note |
|----|-------|-------|-------------|------|
| 0x000 | Player (Link) | 1 | **N64 (gap)** | #117 + player-port pipeline. |
| 0x018 | EN_ELF (Navi) | 0 | absent | story-flag gated (spawns post-Deku-Tree). Close-test for #140 must reproduce her spawn (fresh save state), not just warp to Kokiri. |
| 0x039 | EN_A_OBJ (message-block prop) | 2 | **N64 (gap)** | Interactive sign/block prop. Small visible gap. |
| 0x03B | EN_RIVER_SOUND | 1 | invisible | sound-only, no visual surface. |
| 0x095 | En_St (Skulltula) | 1 | AUTO skin (renders OoT3D) | zelda_st.zar. |
| 0x112 | EN_WONDER_ITEM | 6 | invisible triggers | no visual surface. |
| 0x125 | env sound mgr | 12 | invisible mgr | non-drawing. |
| 0x126 | En_Kanban (bean plant/mamenoki) | 1 | AUTO | zelda_mamenoki.zar. |
| 0x141 | En_Kanban (signposts) | 8 | AUTO (N64 by policy) | zelda_kanban.zar auto-resolves but Zelda3D_TryAuto returns 0 for OBJECT_KANBAN (zelda3d.c:2130) — breakable-piece behavior falls apart with OoT3D geometry. Policy gap. |
| 0x146 | En_Sa (Saria) | 1 | AUTO skin (renders OoT3D) | zelda_sa.zar. |
| 0x14E | En_Ishi (rocks) | many | ISHI-rock(3DS) | zelda_field_keep rock CMB. |
| 0x14F | Obj_Hana (grass/debris) | many | HANA-bush/debris(3DS) | field-keep, per-variant CMB. |
| 0x151 | Obj_Mure2 (foliage cluster) | 1 | **N64 (gap)** | Gameplay_keep-based cluster. Small visible gap. |
| 0x163 | En_Ko (Kokiri kids) | many | AUTO skin + kokiri_kid.cpp | head/torso track + facial (#116 landed). |
| 0x16D | En_Md (Mido) | 1 | AUTO skin (renders OoT3D) | zelda_md.zar. |
| 0x3B | (see 0x03B) | | | |

## Real gaps (visible, drawing, N64)

- **Player** (Link) — active track (#117 + player-port pipeline). Not this sweep.
- **EN_A_OBJ** (0x39) — 2 instances. Message-block/wooden prop. Small gap, worth a card.
- **OBJ_MURE2** (0x151) — 1 instance. Foliage cluster. Small gap, worth a card.
- **EN_KANBAN** (0x141) — 8 instances (signposts). Currently kept N64 by policy per
  `zelda3d.c:2130` because auto-replace of the intact sign breaks the CUT behaviour
  (broken pieces respawn as intact signs). A real port needs the breakable-piece
  handling. Card exists in kanban already; if not, worth filing.
- **Navi** — story-flag gated, invisible here; port planned in #140 with concrete plan.

## Non-gaps

- EN_RIVER_SOUND, EN_WONDER_ITEM, env sound mgrs — invisible, no render surface.
- All other actors already render OoT3D via AUTO/MODULE/behavior module.

## Follow-ups

- File cards for EN_A_OBJ and OBJ_MURE2 (small isolated gaps).
- Confirm existing EN_KANBAN card / policy status; if none, file one with the
  breakable-piece requirement as the deliverable.
- Kokiri Forest is otherwise in a clean structured state.
