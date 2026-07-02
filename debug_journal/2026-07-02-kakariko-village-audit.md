# 2026-07-02 — Kakariko Village Day (ent 0xDB, scene 0x52) structured actor audit

Sweep tool: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart 0xDB 0x8001` +
`actorsnear 10000`. Third sweep, same methodology as Market Day / Kokiri Forest.

**Invariant reminder**: N64 rendering is a gap, not an acceptable state.

## Per-actor render path

| id | actor | count | render path | note |
|----|-------|-------|-------------|------|
| 0x000 | Player (Link) | 1 | **N64 (gap)** | #117 + player-port pipeline. |
| 0x019 | En_Niw (cuccos) | 7 | AUTO skin (renders OoT3D) | zelda_nw.zar. Small clothing/palette polish maybe pending. |
| 0x01E | En_Butte (butterflies) | 2 | MODULE(3DS) | behaviors/actor/en_butte.cpp. |
| 0x039 | En_A_Obj (message-block) | 2 | **N64 (gap)** | Filed #141 from Kokiri; same actor here confirms cross-scene. |
| 0x03B | En_River_Sound | 1 | invisible | sound-only. |
| 0x077 | En_Kusa (bush) | 1 | AUTO | zelda_wood02.zar. |
| 0x094 | Obj_Mure (foliage cluster) | 1 | **N64 (gap)** | Filed #143. Sibling of Obj_Mure2 (#142). |
| 0x09B | Door_Ana (grotto) | 1 | MODULE(3DS) | door_ana.cpp. |
| 0x0B3 | En_Heishi2 (day guard) | 1 | AUTO skin (renders OoT3D) | zelda_sd.zar → soldier.cmb. |
| 0x0E5 | Bg_Spot16_Doughnut (ring FX) | 1 | AUTO | zelda_efc_doughnut.zar. |
| 0x100 | Bg_Gate_Shutter (gate) | 1 | AUTO | zelda_spot01_matoyab.zar. |
| 0x102 | Bg_Spot01_Fusya (windmill blades) | 1 | AUTO forced-CMB | c_s01fusya via sWindmillMeas. |
| 0x103 | Bg_Spot01_Idohashira (well arch) | 1 | AUTO forced-CMB | c_s01idohashira via sWellArchMeas. |
| 0x104 | Bg_Spot01_Idomizu (well water) | 1 | AUTO forced-CMB | c_s01idomizu. |
| 0x111 | Obj_Tsubo (pots) | 11 | TABLE | sModelTable pot entry. |
| 0x112 | En_Wonder_Item | 1 | invisible trigger | no visual surface. |
| 0x125 | env sound mgr | 8 | invisible mgr | non-drawing. |
| 0x132 | En_Toryo (carpenter boss) | 1 | AUTO skin (renders OoT3D) | zelda_toryo.zar → bosshead.cmb. |
| 0x13C | En_Niw_Lady (cucco lady) | 1 | AUTO skin (renders OoT3D) | zelda_ane.zar → chickenlady.cmb. |
| 0x141 | En_Kanban (signposts) | 2 | AUTO (N64 by policy) | See Kokiri audit. |
| 0x14E | En_Ishi (rocks) | 2 | ISHI-rock(3DS) | field-keep rock CMB. |
| 0x167 | En_Ani (rooftop man) | 1 | AUTO skin (renders OoT3D) | zelda_ani.zar → roofman.cmb. |
| 0x178 | En_Heishi4 (patrol guard) | 2 | AUTO skin (renders OoT3D) | zelda_sd.zar → soldier.cmb (shared with Heishi2). |
| 0x19D | Bg_Spot01_Objects2 | 1 | AUTO | zelda_spot01_matoyab.zar. |
| 0x1A0 | env sound mgr | 5 | invisible mgr | non-drawing. |
| 0x1BC | En_Daiku_Kakariko (carpenters) | 3 | AUTO skin (renders OoT3D) | zelda_daiku.zar → disciple.cmb. |

## Shared-ZAR CMB-routing check (EN_TG pattern)

Confirmed AUTO picks the correct main-body CMB for every shared-ZAR case in this scene:

- **zelda_sd.zar** (En_Heishi1/2/3/4 all share this): 2 CMBs — `soldier.cmb` (40448B, 16
  bones — the live guard model) and `soldier2.cmb` (23552B, **5 bones**). The 5-bone
  variant is a truncated pose, most likely En_Heishi4's dying-guard corpse variant.
  Different use case, not a wrong-actor mapping. AUTO picks `soldier.cmb` for all live
  guards — correct.
- **zelda_spot01_objects.zar** (Fusya/Idohashira/Idomizu share this): already routed
  per-actor via sWindmillMeas / sWellArchMeas / sIdomizuMeas forced-CMB paths in
  `zelda3d.c`.
- **zelda_spot01_matoyab.zar** (Bg_Gate_Shutter + Bg_Spot01_Objects2 share this):
  gate + objects2, currently both AUTO — worth a follow-up structural check but not
  in scope for this pass.

## Real gaps (visible, drawing, N64)

- **Player** (Link) — active track #117.
- **En_A_Obj** (0x39) — 2 instances, cross-scene gap already filed #141.
- **Obj_Mure** (0x94) — 1 instance foliage cluster, sibling to Obj_Mure2 (#142). Filed
  this sweep as #143.

## Non-gaps

- En_Wonder_Item, En_River_Sound, env sound mgrs — invisible.
- All other actors already render OoT3D via AUTO / MODULE / TABLE / behavior module.

## Follow-ups

- **soldier2.cmb** in zelda_sd.zar is unused today (AUTO picks soldier.cmb). When
  En_Heishi4 dying-guard port lands, that variant should force-route to soldier2.cmb
  via the sActorForcedAuto pattern (like EN_TG → couple.cmb).
- Structural check on zelda_spot01_matoyab.zar (2-actor share, worth a peek) is a
  small follow-up.
- Otherwise Kakariko Village Day is in a clean structured state.
