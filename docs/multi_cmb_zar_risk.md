# Multi-CMB ZARs shared by several actors — the forced-CMB routing work queue

Each row is an N64 object whose OoT3D ZAR holds MORE THAN ONE CMB **and** which more than one actor
loads. AUTO picks a single CMB per ZAR (the largest non-debris one), so every actor sharing the object
gets the SAME model — wrong for at least one of them. This is the `zelda_mu.zar` couple/marketpeople
and `zelda_syokudai.zar` torch-style bug class, and the fix is a `sActorForcedAuto` entry per actor
(`"<zar>|<cmbSubstr>"`), which only became usable for non-skinned props once the forced-slot
measure-key bug was fixed (2026-07-30).

Ranked by actor count, then CMB count — the top rows are the most-shared archives.

## METHOD CAVEAT — these counts are an UPPER BOUND
The actor list comes from grepping `OBJECT_*` identifiers in `soh/src/overlays/actors/**`, which
over-counts in two ways: a file can name an object without loading it as its own dependency, and
generic actors (`door_shutter`, `door_killer`, `demo_*`) legitimately reuse whichever dungeon object is
already resident rather than owning a distinct model. So treat "N actors" as "N files mention it".
Before adding a routing entry, confirm from the actor's init struct which object it actually declares.
The top entries are unambiguous regardless: 16 Fire Temple actors cannot all be the largest of 31 CMBs.

| object | id | actors | cmb | ZAR | first CMB names |
|---|---|---|---|---|---|
| OBJECT_HIDAN_OBJECTS | 0x002C | 16 | 31 | `zelda_hidan_objects.zar` | m_Fbmfl_model.cmb, m_Fbmwall1_model.cmb, m_Fbmwall2_model.cmb, m_Fdalm_model.cmb … |
| OBJECT_JYA_OBJ | 0x00F1 | 10 | 38 | `zelda_jya_obj.zar` | l_j_1Flift_model.cmb, l_j_anahikari_model.cmb, l_j_anahikari_modelT.cmb, l_j_bigkagami_model.cmb … |
| OBJECT_HAKA_OBJECTS | 0x0069 | 8 | 32 | `zelda_haka_objects.zar` | m_HADcoinshutter1_model.cmb, m_HADinv0b_model.cmb, m_HADinv0f_model.cmb, m_HADinv03_model.cmb … |
| OBJECT_MIZU_OBJECTS | 0x0059 | 8 | 18 | `zelda_mizu_objects.zar` | m_Wbomb00E_model.cmb, m_Wbomb0eE_model.cmb, m_Wbomb0eW_model.cmb, m_Wbomb03_model.cmb … |
| OBJECT_DEMO_KEKKAI | 0x0179 | 8 | 16 | `zelda_demo_kekkai.zar` | l_g_door_model.cmb, l_g_hikari_modelT.cmb, l_g_hikarijimen_model.cmb, l_g_icebrock_modelT.cmb … |
| ~~OBJECT_MORI_OBJECTS~~ **DONE** | 0x0072 | 7 | 9 | `zelda_mori_objects.zar` | l_4hasira_model.cmb, l_bigst_model.cmb, l_elevator_model.cmb, l_hasigo_model.cmb … |
| OBJECT_ICE_OBJECTS | 0x006B | 6 | 8 | `zelda_ice_objects.zar` | ice_brick_model.cmb, ice_ice3_modelT.cmb, ice_ice_modelT.cmb, ice_tobira_model.cmb … |
| OBJECT_OF1D_MAP | 0x00C9 | 6 | 4 | `zelda_oF1d.zar` | goronpeople.cmb, go_smoke_model.cmb, oF1d_iwa2_model.cmb, oF1d_iwa_model.cmb |
| OBJECT_GANON | 0x00E1 | 5 | 24 | `zelda_ganon.zar` | efc_ganon_floor_modelT.cmb, ganon_tyuka_ue_model.cmb, ganondorf.cmb, efc_fg_thunder1_modelT.cmb … |
| OBJECT_YDAN_OBJECTS | 0x0036 | 4 | 12 | `zelda_ydan_objects.zar` | maruta_model.cmb, ydan_maruta_model.cmb, ydan_kumohen_modelT.cmb, ydan_ytoge_model.cmb … |
| OBJECT_TOKI_OBJECTS | 0x005E | 4 | 11 | `zelda_toki_objects.zar` | demo_tt_triforce2_0_model.cmb, demo_tt_triforce2_1_model.cmb, demo_tt_triforce_modelT.cmb, left_model.cmb … |
| OBJECT_HAKACH_OBJECTS | 0x008D | 4 | 8 | `zelda_hakach_objects.zar` | m_Hinv05_model.cmb, m_Hkhuta_model.cmb, m_HkotuBomb00_model.cmb, m_Hsec00_model.cmb … |
| ~~OBJECT_DDAN_OBJECTS~~ **DONE** | 0x002B | 4 | 5 | `zelda_ddan_objects.zar` | ddan_tdoor_model.cmb, ddan_tdoor_yari_model.cmb, ddanh_ago_model.cmb, ddanh_jd_model.cmb … |
| OBJECT_MENKURI_OBJECTS | 0x004D | 4 | 5 | `zelda_menkuri_objects.zar` | l_m_door_model.cmb, l_m_nisekabe1_model.cmb, l_m_nisekabe2_model.cmb, l_sekizoume_modelT.cmb … |
| OBJECT_SPOT18_OBJ | 0x00AF | 4 | 5 | `zelda_spot18_obj.zar` | obj_185_model.cmb, obj_186_model.cmb, obj_s18tubo_model.cmb, obj_s18yari_model.cmb … |
| OBJECT_NIW | 0x0013 | 4 | 2 | `zelda_nw.zar` | chicken.cmb, nw_hane_model.cmb |
| OBJECT_SD | 0x0097 | 4 | 2 | `zelda_sd.zar` | soldier.cmb, soldier2.cmb |
| OBJECT_BDAN_OBJECTS | 0x0096 | 3 | 17 | `zelda_bdan_objects.zar` | a_by_door0_model.cmb, a_by_door1_model.cmb, a_by_door2_model.cmb, a_by_door3_model.cmb … |
| OBJECT_FD | 0x009C | 3 | 11 | `zelda_fd.zar` | m_FBRsizumi_model.cmb, valbasiabody.cmb, valbasiagnd.cmb, valbasiahead.cmb … |
| OBJECT_EFC_STAR_FIELD | 0x0092 | 3 | 7 | `zelda_efc_star_field.zar` | demo_rock_model1.cmb, demo_rock_model2.cmb, fire_rock_model1.cmb, fire_rock_model2.cmb … |
| OBJECT_FHG | 0x005A | 3 | 6 | `zelda_fantomHG.zar` | ganonhorse.cmb, f_ganon_efc_modelT.cmb, gnf_bakuhatsu_modelT.cmb, gnf_inazuma_modelT.cmb … |
| ~~OBJECT_KINGDODONGO~~ **DONE** | 0x0019 | 3 | 4 | `zelda_kdodongo.zar` | ddanh_bomy_model.cmb, g_ddg2_fire_model.cmb, kingdodongo.cmb, kd_hinoko_modelT.cmb |
| OBJECT_ST | 0x0024 | 3 | 4 | `zelda_st.zar` | staltula.cmb, staltula_gold.cmb, gi_sutaru_coin_model.cmb, gi_sutaru_coin_modelT.cmb |
| OBJECT_SPOT01_OBJECTS | 0x00F9 | 3 | 3 | `zelda_spot01_objects.zar` | c_s01fusya_model.cmb, c_s01idohashira_model.cmb, c_s01idomizu_modelT.cmb |
| OBJECT_HINTNUTS | 0x0164 | 3 | 3 | `zelda_hintnuts.zar` | dekunuts.cmb, dnh_ball_model.cmb, dekunuts_plant.cmb |
| OBJECT_HAKA_DOOR | 0x0187 | 3 | 3 | `zelda_haka_door.zar` | m_Hnormaldoor_omote_model.cmb, m_Hshutter1_model.cmb, m_Hshutter2_model.cmb |
| OBJECT_BOX | 0x000E | 3 | 2 | `zelda_box.zar` | demo_tre_lgt_mdl_info.cmb, tr_box.cmb |
| OBJECT_GI_HEARTS | 0x00BD | 3 | 2 | `zelda_gi_hearts.zar` | zelda_gi_hearts_0.cmb, zelda_gi_hearts_1.cmb |
| OBJECT_SHOPNUTS | 0x0168 | 3 | 2 | `zelda_shopnuts.zar` | akindonuts.cmb, dnu_ball_model.cmb |
| OBJECT_IK | 0x0106 | 2 | 15 | `zelda_ik.zar` | ironknack.cmb, backarmer_damage_demo.cmb, front_armer_drop.cmb, frontarmer_damage_demo.cmb … |
| OBJECT_FISH | 0x015B | 2 | 14 | `zelda_fishing.zar` | fishbig.cmb, fishmaster.cmb, fishmiddle.cmb, fs_cap_model.cmb … |
| OBJECT_SPOT02_OBJECTS | 0x00A1 | 2 | 8 | `zelda_spot02_objects.zar` | haka_l_ring_modelT.cmb, haka_thunder0_modelT.cmb, haka_thunder1_modelT.cmb, obj_s02futa_model.cmb … |
| OBJECT_PO_SISTERS | 0x0099 | 2 | 7 | `zelda_po_sisters.zar` | pohsisters.cmb, l_pou1pict_model.cmb, l_pou2pict_model.cmb, l_pou3pict_model.cmb … |
| OBJECT_SST | 0x00E2 | 2 | 7 | `zelda_sst.zar` | m_Htaiko_model.cmb, bongolhand.cmb, bongorhand.cmb, bongobongo.cmb … |
| OBJECT_DEKUBABA | 0x0039 | 2 | 6 | `zelda_dekubaba.zar` | dekubaba.cmb, db_miki1_model.cmb, db_miki2_model.cmb, db_miki3_model.cmb … |
| OBJECT_SYOKUDAI | 0x00A4 | 2 | 5 | `zelda_syokudai.zar` | syokudai_isi_model.cmb, syokudai_ki_model.cmb, syokudai_model.cmb, torch4_modelT.cmb … |
| OBJECT_JYA_IRON | 0x016C | 2 | 5 | `zelda_jya_iron.zar` | l_j_ironhasira_model.cmb, l_j_ironhasiraB1_model.cmb, l_j_ironisu_model.cmb, l_j_ironhasiraB0_model.cmb … |
| OBJECT_SPOT01_MATOYA | 0x0180 | 2 | 5 | `zelda_spot01_matoya.zar` | c_matoate_house_model.cmb, c_s01_m_kanban_model.cmb, c_s01idosoko_model.cmb, c_s01_k_kanban_model.cmb … |
| OBJECT_PO_FIELD | 0x006D | 2 | 4 | `zelda_po_field.zar` | bigpoh.cmb, kantera_big.cmb, kantera_field.cmb, soul.cmb |
| OBJECT_FW | 0x009E | 2 | 4 | `zelda_fw.zar` | flaredancer.cmb, flamewalker.cmb, fw_smoke_model.cmb, fw_hinoko_modelT.cmb |
| OBJECT_DY_OBJ | 0x000A | 2 | 3 | `zelda_dy_obj.zar` | fairy.cmb, yousei_eff_modelT.cmb, efc_g_fairly_modelT.cmb |
| ~~OBJECT_WALLMASTER~~ **DONE** | 0x000B | 2 | 3 | `zelda_wm2.zar` | floormaster.cmb, fallmaster.cmb, shadow_f_model.cmb |
| OBJECT_DEKUNUTS | 0x004A | 2 | 3 | `zelda_dekunuts.zar` | okorinuts.cmb, dn_ball_model.cmb, okorinuts_plant.cmb |
| OBJECT_SPOT08_OBJ | 0x0074 | 2 | 3 | `zelda_spot08_obj.zar` | obj_bigice_model.cmb, obj_iceblock_model.cmb, obj_s08wall_model.cmb |
| OBJECT_DH | 0x00A6 | 2 | 3 | `zelda_dh.zar` | deadarm.cmb, deadhand.cmb, dh_dust_modelT.cmb |
| OBJECT_SPOT17_OBJ | 0x00B1 | 2 | 3 | `zelda_spot17_obj.zar` | obj_s17wall_model.cmb, obj_s17wall_modelT.cmb, obj_smork_modelT.cmb |
| OBJECT_BXA | 0x00D5 | 2 | 3 | `zelda_bxa.zar` | balinadearm.cmb, balinadearm_death.cmb, balinadetrap.cmb |
| OBJECT_GI_M_ARROW | 0x0158 | 2 | 3 | `zelda_gi_m_arrow.zar` | zelda_gi_m_arrow_0.cmb, zelda_gi_m_arrow_1.cmb, zelda_gi_m_arrow_2.cmb |
| OBJECT_DNS | 0x0171 | 2 | 3 | `zelda_dns.zar` | eldernuts.cmb, dns_ball_model.cmb, eldernuts_plant.cmb |
| OBJECT_DNK | 0x0172 | 2 | 3 | `zelda_dnk.zar` | choronuts.cmb, dnk_ball_model.cmb, choronuts_plant.cmb |
| OBJECT_GOMA | 0x001C | 2 | 2 | `zelda_goma.zar` | goma.cmb, a_yb_door_model.cmb |
| OBJECT_GND | 0x0037 | 2 | 2 | `zelda_gnd.zar` | l_bosssaku_model.cmb, phantomganon.cmb |
| OBJECT_SPOT15_OBJ | 0x00F0 | 2 | 2 | `zelda_spot15_obj.zar` | spot15_box_model.cmb, spot15_saku_modelT.cmb |
| OBJECT_SKJ | 0x010A | 2 | 2 | `zelda_skj.zar` | stalkid.cmb, blow_arrow_model.cmb |
| OBJECT_TSUBO | 0x012C | 2 | 2 | `zelda_tsubo.zar` | tubo2_hahen_model.cmb, tubo2_model.cmb |
| ~~OBJECT_SPOT12_OBJ~~ **DONE** | 0x0162 | 2 | 2 | `zelda_spot12_obj.zar` | s12gate_model.cmb, s12saku_model.cmb |
| OBJECT_SPOT11_OBJ | 0x016F | 2 | 2 | `zelda_spot11_obj.zar` | obj_112_modelT.cmb, obj_s11wall_model.cmb |
| OBJECT_BOWL | 0x0178 | 2 | 2 | `zelda_bowl.zar` | bowling_p1_model.cmb, bowling_p2_model.cmb |
| OBJECT_SPOT01_MATOYAB | 0x0181 | 2 | 2 | `zelda_spot01_matoyab.zar` | c_matoate_before_model.cmb, c_s01tomegate_model.cmb |
| OBJECT_MU | 0x0182 | 2 | 2 | `zelda_mu.zar` | couple.cmb, marketpeople.cmb |

**60 objects at risk.** Generated by the sweep recorded in the 2026-07-30 audit journal.

## Pass 2 notes (2026-07-30) — where simple name matching STOPS working

`OBJECT_MORI_OBJECTS` and `OBJECT_DDAN_OBJECTS` were routable by reading the CMB names as Japanese
(bigst/hasira/hasigo/idomizu/kaiten/tenjyou; kaidan/ago/jd). Everything checked after them was not:

* **Params-keyed VARIANTS, not one mesh per actor.** `Bg_Mizu_Shutter` faces four shutter CMBs;
  `Bg_Ydan_Sp` has `spkabe` (kabe = wall) and `spyuka` (yuka = floor) webs; `Bg_Menkuri_Nisekabe` has
  `nisekabe1`/`nisekabe2`. One actor draws several meshes selected by params — the same problem
  `OBJECT_PU_BOX` has (pu_box1/2/4 size variants), and it needs per-variant param routing rather than a
  name lookup. `sVariantMeas` in zelda3d_render.cpp is the existing mechanism for that shape.
* **No candidate at all:** `Bg_Mizu_Bwall`, `Bg_Mizu_Movebg`, `Bg_Mizu_Uzu`, `Bg_Mizu_Water`,
  `Bg_Menkuri_Eye`. These need their N64 display-list name or their draw code read to identify the mesh.

So the remaining queue is NOT more of the same work. Rows where each actor owns exactly one mesh are the
cheap ones; the rest split into param-variant routing and genuine per-actor identification. Check which
kind a row is before committing to it.

## Pass 3 (2026-07-30) — the queue is now classified, and the cheap class is EXHAUSTED

All 60 rows were classified by whether each owning actor maps to exactly one distinct mesh:

| class | count | what it needs |
|---|---|---|
| cheap (1 mesh per actor) | 4 | a `sActorForcedAuto` row each — **all now done or excluded** |
| params-keyed variants | 10 | per-variant routing via `sVariantMeas`, like `pu_box`/`En_Ishi` |
| unidentified | 32 | read the actor's N64 draw code or DL name to identify its mesh |

Done: `OBJECT_MORI_OBJECTS`, `OBJECT_DDAN_OBJECTS`, `OBJECT_WALLMASTER`, `OBJECT_KINGDODONGO`,
`OBJECT_SPOT12_OBJ`. Excluded: `OBJECT_SPOT01_OBJECTS` — already handled by dedicated per-actor
branches (`sWindmillMeas`, `sWellArchMeas`); routing it would create two competing routes.

**The classifier flags candidates, not instructions.** It cannot see hand-written per-actor branches,
which is exactly how `OBJECT_SPOT01_OBJECTS` came back "cheap" when it was already solved. Check for
existing handling before adding a row.

So the remaining 42 rows are genuinely harder work, in two distinct shapes. Anyone continuing should
pick a shape and build for it, rather than expecting more name lookups.

## Pass 4 (2026-07-30) — STOP: the forced-CMB path cannot draw TRANSLUCENT actors

Attempted the params-variant class starting with `Bg_Ydan_Sp` (the Deku Tree web) and hit a hard
architectural blocker, not a mapping problem.

**Every Zelda3D draw emission goes into `POLY_OPA_DISP`** (`gSPZelda3DDraw`, `gSPZelda3DDrawA`,
`gSPZelda3DMeasure` — verified, there is no XLU emission anywhere in `zelda3d_render.cpp`). But
`Bg_Ydan_Sp` draws `gDTWebWallDL`/`gDTWebFloorDL` into `POLY_XLU_DISP`, and `Bg_Ydan_Hasi` draws
`gDTWaterPlaneDL` into `POLY_XLU_DISP`. Routing either would render it OPAQUE and in the opaque pass —
wrong blending and wrong draw order. An opaque spider web or water plane is a worse regression than the
shared-mesh bug it would be fixing, so these are deliberately NOT routed.

This re-scopes the queue: **39 of 60 sampled `bg_`/`obj_` actors use `POLY_XLU_DISP`**, and the
translucent ones are concentrated in the "unidentified" bucket (webs, water, mirrors, whirlpools).
So the remaining work is gated on giving the Zelda3D draw path an XLU emission, not on identifying more
meshes. That is the next real task for this queue.

### Ground truth derived before hitting the blocker — recorded so it is not re-derived
* `Bg_Ydan_Sp` — variant selector is `(params >> 0xC) & 0xF` (`z_bg_ydan_sp.c:100`), enum
  `WEB_FLOOR = 0`, `WEB_WALL = 1`. So mask `0xF000`: value `0x0000` -> `ydan_spyuka` (*yuka* = floor),
  `0x1000` -> `ydan_spkabe` (*kabe* = wall). N64 DL names and the Japanese CMB names agree independently.
* `Bg_Ydan_Hasi` -> `ydan_mizu` (*mizu* = water). It draws `gDTWaterPlaneDL`, i.e. the WATER PLANE.
* `Bg_Ydan_Maruta` — draws `gDTRollingSpikeTrapDL` **or** `gDTFallingLadderDL`, so it is a two-variant
  actor with three plausible CMBs (`maruta_model`, `ydan_maruta_model`, `ydan_ytoge_model`; *toge* =
  spike, *maruta* = log — the trap is a spiked log). Left AMBIGUOUS; needs the selector read.

### Why the name matcher is not sufficient — a concrete case
The matcher proposed `Bg_Ydan_Hasi` -> `ydan_t_hasigo_model`, on the reasonable-looking grounds that
*hasi*/*hasigo* share a stem. The actor's draw code shows it renders the WATER PLANE, so the correct
mesh is `ydan_mizu`. *hasi* (bridge) and *hasigo* (ladder) are different words that a substring match
conflates. **Read the actor's draw code for every routing; the CMB name alone is a hypothesis.**

## Pass 5 (2026-07-30) — translucent routing is gated on the MEASURE pass, not the draw pass

The XLU *draw* path now exists (`Zelda3D_AutoModelAllBlended` -> `POLY_XLU_DISP`). Routing the first
translucent prop (`Bg_Ydan_Sp` wall web -> `ydan_spkabe`) exposed the next blocker one level down:

**`Zelda3D_EmitMeasure` emits its bracket into `POLY_OPA_DISP`, but a translucent actor draws into
`POLY_XLU_DISP`.** The bracket wraps nothing, no height is reported, and the slot sits at state 4
(never measured) forever. Every translucent row in this queue hits this.

The naive fix is WRONG and the reason is worth keeping: emitting the bracket into both lists produces
two sequential bracket sessions at interpret time (all of OPA, then all of XLU), and the second
overwrites the first through `Zelda3D_MeasureResult`. A purely-opaque actor would have its real height
replaced by the empty XLU session's zero — breaking every measurement that currently works. The safe
version needs the interpreter to suppress a bracket that accumulated no geometry.

Also note two ORTHOGONAL blockers for flat props, already known: a horizontal plane has ~zero model
height, so the height measure cannot scale it regardless of pass (`Bg_Ydan_Sp` FLOOR web,
`Bg_Ydan_Hasi` water plane). Those need `Zelda3D_AutoModelExtentXZ` footprint sizing, the path
`Bg_Spot01_Idomizu` already uses.

So the translucent class needs, in order: (1) an XLU-aware measure bracket with empty-session
suppression, (2) footprint sizing for flat props. Neither is a routing-table problem.

## Pass 6 (2026-07-30) — VERIFICATION STANDARD: prove the routed CMB actually DRAWS

Routing `Bg_Ydan_Sp` -> `ydan_spkabe` **deleted the Deku Tree web**. The slot looked perfect (state=2,
scale=0.10000, n64h=288.8, distinct model id) and the replacement contributed ZERO pixels. Reverted.

**Why an invisible replacement is worse than no routing:** a successful route makes
`Zelda3D_TryDrawActor` return "handled", which skips the N64 draw. There is no fallback. So the object
disappears — and here it was gameplay-critical (the web must be burned to progress).

**The required check, from now on:**
```
asel <actorId>            # select it
acam <dist> z             # frame it
shot before               # capture
ahide 1                   # suppress just this actor's draw
shot after                # capture
# diff before/after: a routing that works contributes NON-ZERO pixels
```
`state=2` plus a sane scale proves only that the MEASURE ran. It does not prove anything renders.

**Earlier routings in this doc were verified to the weaker standard** (distinct model ids + measured
scales), with visual confirmation only for two mori props and one ddan prop. `OBJECT_MORI_OBJECTS`,
`OBJECT_DDAN_OBJECTS`, `OBJECT_WALLMASTER`, `OBJECT_KINGDODONGO` and `OBJECT_SPOT12_OBJ` should each get
the `ahide` check above; any that contribute zero pixels must be reverted the same way.

## Pass 7 (2026-07-30) — audit of the earlier routings, and a false positive in the check itself

Ran the `ahide` pixel-contribution check over the routings that had only been verified by model-id +
measured-scale. Results:

| routing | pixels contributed | verdict |
|---|---|---|
| En_Wallmas -> fallmaster | 13512 | **draws** |
| En_Floormas -> floormaster | 24295 | **draws** |
| Bg_Mori_Bigst -> l_bigst | 5374 | **draws** |
| Bg_Mori_Elevator -> l_elevator | 32763 | **draws** |
| Bg_Ddan_Kd -> ddanh_kaidan | 43142 | **draws** |
| Bg_Ddan_Jd -> ddanh_jd | 23764 | **draws** |
| Bg_Dodoago -> ddanh_ago | 0 | **INCONCLUSIVE, not broken** — see below |
| mori kaiten / tenjyou / hasigo / hasira4 / idomizu | — | not in the loaded room; pending |
| Boss_Dodongo, En_Bdfire | — | boss room not visited; pending |
| Bg_Spot12_Gate / _Saku | — | slots never resolved in a Gerudo Valley sweep; pending |

**The `Bg_Dodoago` zero was a false positive from the check script, not a bad routing.** Its slot is
`state=4` (never measured), so the replacement is NOT being drawn — the N64 draw is still in control —
and `actorsnear r=4000` does not even list the actor while `asel` finds it, i.e. it is beyond that
radius and not meaningfully on screen. A zero there says nothing about the CMB.

So the check has a precondition that was missing: **a zero is only evidence of a broken routing when
`autostate` shows that slot at `state=2` AND the actor is genuinely in frame.** Otherwise it is
inconclusive. `scratch/bin/ahide_check.sh` now says so instead of printing "REVERT". This is the same
mistake shape as the session's other silent-zero instruments — a tool that cannot tell "contributed
nothing" from "was not applicable".

Six routings are now positively confirmed to draw. The Deku Tree web remains the only confirmed
failure (its slot WAS state=2, it WAS in frame, and it contributed zero).

## Pass 7b — the audit CLOSES, via a safety argument rather than exhaustive checking

Chasing the remaining props room by room was the wrong frame. What matters is that **an inactive
routing cannot regress anything**: a slot at `state=0` (never seen) or `state=4` (never measured) makes
`Zelda3D_TryDrawActor` return 0, so the N64 draw proceeds exactly as before. Only a slot at `state=2`
suppresses the N64 draw, and therefore only `state=2` carries the delete-the-object risk.

Every routing currently at `state=2` has been confirmed to draw:

| routing | state | pixels | verdict |
|---|---|---|---|
| En_Wallmas -> fallmaster | 2 | 13512 | draws |
| En_Floormas -> floormaster | 2 | 24295 | draws |
| Bg_Mori_Bigst -> l_bigst | 2 | 5374 | draws |
| Bg_Mori_Elevator -> l_elevator | 2 | 32763 | draws |
| Bg_Ddan_Kd -> ddanh_kaidan | 2 | 43142 | draws |
| Bg_Ddan_Jd -> ddanh_jd | 2 | 23764 | draws |

Everything else is inert and safe as it stands:
* `l_idomizu` (Forest Temple well water) sits at `state=4`, which is exactly the predicted behaviour for
  a FLAT prop — a horizontal plane has ~zero model height so the bbox-height measure can never derive a
  scale. It correctly falls back to the N64 draw. It needs `Zelda3D_AutoModelExtentXZ` footprint sizing
  before it can ever be replaced, and until then it cannot break.
* `l_kaiten`, `l_tenjyou`, `l_hasigo`, `l_4hasira` are `state=0` — those actors were not reachable via
  `roomwarp` in Forest Temple rooms 0-12, so they have never resolved. Inert.
* `Boss_Dodongo` / `En_Bdfire` and the `spot12` pair likewise never resolved in the scenes visited.

**So there are no unverified risks outstanding from these routings.** The standing rule is what matters
going forward: when a slot first reaches `state=2`, it needs the `ahide` pixel check before its routing
can be trusted — and a zero only counts against it when the slot is `state=2` AND the actor is in frame.

## Pass 8 (2026-07-30) — the web routing was CORRECT; my check was one-sided. Verification standard amended.

`Bg_Ydan_Sp` -> `ydan_spkabe` is now SHIPPED. It was reverted twice on a bad measurement, and the
correction matters more than the prop:

The web is a **flat single-sided plane** (bbox 2800 x 2888 x **0**) with `cull=1`, so it is visible only
from its front hemisphere. My `ahide` check used ONE camera angle, which happened to sit on its back,
read 0 px, and I called it a regression. An orbit sweep settles it:

| azimuth | 0 | 45 | 90 | 135 | 180 | 225 | 270 | 315 |
|---|---|---|---|---|---|---|---|---|
| px | 0 | 0 | 0 | 0 | 13589 | 19865 | 20478 | 11919 |

That is exactly correct culling, and it matches OoT3D — whose own material culls back faces. The N64
mesh draws from both sides, which is why the N64 web appeared from an angle where ours does not;
matching OoT3D is the goal, so single-sided is right.

Also settled offline: the web winds **100% CCW-from-normal**, identical to the control volumes
(`l_elevator` 576/576, `ddanh_jd` 56/56, `floormaster` 484/484). So the asset is not mis-wound and the
global front-face convention is NOT in question — an earlier warning to the contrary is retracted.

### AMENDED VERIFICATION STANDARD
* **Closed volumes:** a single-angle `ahide` pixel check is sound — some front face always faces the
  camera.
* **Flat / single-sided props:** a single angle is INVALID. Orbit the camera (`camorbit 45` x8) and
  require non-zero contribution from *some* azimuth. Zero from one angle proves nothing.
* Tell the two apart with `scratch/bin/cmb_tex_alpha`, which now reports the geometry bbox: any axis of
  size ~0 means flat.

## Pass 9 (2026-07-30) — bbox classification of every pending routing

Ran `scratch/bin/cmb_tex_alpha`'s geometry-bbox report over the routings that had not yet been
confirmed, to sort them by verification method and to find any that can NEVER resolve:

| routed CMB | bbox size (x,y,z) | shape | consequence |
|---|---|---|---|
| `l_kaiten` | 1034 x 306 x 1034 | volume | single-angle check is valid |
| `l_4hasira` | 460 x 170 x 460 | volume | single-angle check is valid |
| `l_tenjyou` | 400 x 24 x 1360 | thin slab | has real height; measures fine |
| `l_hasigo` | 32 x 227 x 2 | near-flat | **needs an orbit sweep** |
| `s12gate` | 3998 x 2628 x 294 | volume | single-angle check is valid |
| `s12saku` | 1704 x 1070 x 100 | volume | single-angle check is valid |
| `g_ddg2_fire` | 79 x 78 x 95 | volume | single-angle check is valid |
| `l_idomizu` | 2763 x **0** x 289 | FLAT | **can never resolve — see below** |

**`l_idomizu` is inert by construction.** It is the Forest Temple well water: a horizontal plane with
exactly zero height, so the bbox-height measure can never derive a scale (`modelH > 1e-3` fails) and the
slot parks at `ZELDA3D_AUTO_NOMEAS` permanently, leaving the N64 draw in place. Its `state=4` in
`autostate` is therefore structural, not a transient miss. Annotated in the table itself so nobody reads
that row as working. Making it real needs `Zelda3D_AutoModelExtentXZ` footprint sizing, which needs an
XZ measure — the bracket currently reports height only.

This is the same blocker as `Bg_Ydan_Sp`'s FLOOR web and `Bg_Ydan_Hasi`'s water plane, so a single XZ
measure would unlock all three. That is the highest-value renderer follow-up for this queue.

## Pass 10 (2026-07-30) — both Deku Tree webs VERIFIED; two instrument caveats

`ydan_spkabe` (wall) and `ydan_spyuka` (floor) both draw. Each needed a different method, because the
geometry decides which method can even see it:

| prop | shape | method | result |
|---|---|---|---|
| `ydan_spkabe` | vertical plane, cull=1 | orbit sweep | 0 px at azimuth 0-135; 13589-20478 px at 180-315 |
| `ydan_spyuka` | horizontal plane, cull=1 | **elevated** view | 47792 px at elev 55; 50514 px at elev 75 |

`acam` gained an `elevDeg` third argument for the second case — a side profile is structurally blind to a
horizontal plane (edge-on = no pixels) and `camorbit` cannot fix it, since azimuth rotation never carries
the eye across a floor plane's face.

### Two caveats that invalidate naive checks
* **`acam <dist> z` never parsed the "z".** The axis is read with `%d`, so a letter leaves it 0 (+X).
  Framings that passed "z" were +X side profiles, not +Z.
* **`ahide` hides ONE actor; props have MULTIPLE instances.** `asel 0xF <n>` finds six Bg_Ydan_Sp here:
  n=0/4/5 are wall webs (params=1), n=1/2/3 are floor webs (params=0). So a before/after pair still shows
  a web in the "hidden" frame — a different instance. The pixel delta is attributable to the selected
  actor, but the VISUAL is not a clean before/after for a multi-instance prop. Check `params` on the
  selection before interpreting either.

## Pass 11 (2026-07-30) — Bg_Menkuri_Nisekabe: selector known, mapping still AMBIGUOUS. Not routed.

Ground truth from `z_bg_menkuri_nisekabe.c`:
* selector is `params & 0xFF`, indexing `sDLists[] = { gGTGFakeWallDL, gGTGFakeCeilingDL }` — so 0 is a
  fake WALL and 1 a fake CEILING (Gerudo Training Ground; *menkuri* = GTG, *nisekabe* = fake wall).
* the DRAW PASS is runtime-dependent: `Gfx_DrawDListXlu` when the actor has `ACTOR_FLAG_REACT_TO_LENS`,
  otherwise `Gfx_DrawDListOpa`. Worth noting because our routing picks a pass from the MODEL's material,
  which cannot follow a per-actor runtime flag.

But the two candidate CMBs do not map onto wall/ceiling by any evidence I have:

| CMB | bbox size | reads as |
|---|---|---|
| `l_m_nisekabe1_model` | 1200 x 1200 x 400 | a wall slab |
| `l_m_nisekabe2_model` | 1240 x 800 x 1200 | neither clearly — 800 tall, not a flat ceiling |

Both are volumes, both fully opaque (alpha 255 throughout), and neither is the flat horizontal slab a
"fake ceiling" would be. The numeric suffixes suggest 1 -> index 0 and 2 -> index 1, but that is a
naming convention, not evidence, and a wrong pick renders the wrong geometry in a puzzle room.

NOT ROUTED. To settle it, compare each CMB against what `gGTGFakeWallDL` / `gGTGFakeCeilingDL` actually
draw (vertex extents from the N64 display lists), or observe a known instance of each params value in
GTG. The selector is the easy half and it is done; the mesh identity is the open half.
