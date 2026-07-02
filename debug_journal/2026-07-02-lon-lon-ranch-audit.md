# 2026-07-02 — Lon Lon Ranch (ent 0x157, scene 0x63) structured actor audit

Sweep tool: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart 0x157 0x8001` +
`actorsnear 10000`. Fourth sweep.

**Invariant reminder**: N64 rendering is a gap, not an acceptable state.

## Save state note

The current save state lands in adult-era + Ingo-takeover phase (Ingo is present,
Malon / Talon / cuccos / cows are NOT spawned in this scene setup). Child-era Malon /
Talon audit belongs to a follow-up sweep from a child-era save.

## Per-actor render path (current save)

| id | actor | count | render path | note |
|----|-------|-------|-------------|------|
| 0x000 | Player (Link) | 1 | **N64 (gap)** | #117 + player-port pipeline. |
| 0x009 | En_Door | 3 | MODULE(3DS) | door.cpp. |
| 0x018 | EN_ELF (Navi) | 1 | **N64 (gap)** | **Present here at d=39 from Link.** Falsifies the "Navi requires a primed save" concern from the Kokiri audit — she is spawned in this current save, just absent in Kokiri Forest (probably scene-side unspawn for Link's home area). Use this as the #140 close-test scene. |
| 0x03B | En_River_Sound | 2 | invisible | sound-only. |
| 0x03C | En_Horse | 6 | AUTO skin (renders OoT3D) | zelda_horse_normal.zar — Epona-family horses. |
| 0x077 | En_Kusa (bush) | 1 | AUTO | zelda_wood02.zar. |
| 0x09B | Door_Ana (grotto) | 1 | MODULE(3DS) | door_ana.cpp. |
| 0x0CB | En_In (Ingo) | 1 | AUTO skin (renders OoT3D) | zelda_in.zar. |
| 0x111 | Obj_Tsubo (pots) | 7 | TABLE | sModelTable pot entry. |
| 0x1A0 | env sound mgr | 1 | invisible mgr | non-drawing. |

## Real gaps (visible, drawing, N64)

- **Player** (Link) — active track #117.
- **Navi** (EN_ELF, 0x18) — 1 instance, tracked by #140. **Reproduction confirmed here**
  — no primed-save plumbing needed for the close-test.

## Non-gaps

- En_River_Sound, env sound mgrs — invisible.
- All other actors already render OoT3D via AUTO / MODULE / TABLE / behavior module.

## Follow-ups

- **Child-era Lon Lon Ranch** sweep (Malon/Talon/cuccos/cows) needs a save state
  from earlier in the story arc. Left for a follow-up sweep.
- Note on #140: Lon Lon Ranch is the cleanest single-scene reproducer for the Navi
  close-test today.
