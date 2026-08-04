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
| ~~OBJECT_HIDAN_OBJECTS~~ **14 routed, 1 withdrawn, 6 excluded** | 0x002C | 16 | 31 | `zelda_hidan_objects.zar` | m_Fbmfl_model.cmb, m_Fbmwall1_model.cmb, m_Fbmwall2_model.cmb, m_Fdalm_model.cmb … |
| OBJECT_JYA_OBJ | 0x00F1 | 10 | 38 | `zelda_jya_obj.zar` | l_j_1Flift_model.cmb, l_j_anahikari_model.cmb, l_j_anahikari_modelT.cmb, l_j_bigkagami_model.cmb … |
| OBJECT_HAKA_OBJECTS | 0x0069 | 8 | 32 | `zelda_haka_objects.zar` | m_HADcoinshutter1_model.cmb, m_HADinv0b_model.cmb, m_HADinv0f_model.cmb, m_HADinv03_model.cmb … |
| ~~OBJECT_MIZU_OBJECTS~~ **2 of 5 DONE** | 0x0059 | 8 | 18 | `zelda_mizu_objects.zar` | m_Wbomb00E_model.cmb, m_Wbomb0eE_model.cmb, m_Wbomb0eW_model.cmb, m_Wbomb03_model.cmb … |
| OBJECT_DEMO_KEKKAI | 0x0179 | 8 | 16 | `zelda_demo_kekkai.zar` | l_g_door_model.cmb, l_g_hikari_modelT.cmb, l_g_hikarijimen_model.cmb, l_g_icebrock_modelT.cmb … |
| ~~OBJECT_MORI_OBJECTS~~ **DONE** | 0x0072 | 7 | 9 | `zelda_mori_objects.zar` | l_4hasira_model.cmb, l_bigst_model.cmb, l_elevator_model.cmb, l_hasigo_model.cmb … |
| ~~OBJECT_ICE_OBJECTS~~ **DONE (5 verified, 4 inert)** | 0x006B | 6 | 8 | `zelda_ice_objects.zar` | ice_brick_model.cmb, ice_ice3_modelT.cmb, ice_ice_modelT.cmb, ice_tobira_model.cmb … |
| OBJECT_OF1D_MAP | 0x00C9 | 6 | 4 | `zelda_oF1d.zar` | goronpeople.cmb, go_smoke_model.cmb, oF1d_iwa2_model.cmb, oF1d_iwa_model.cmb |
| OBJECT_GANON | 0x00E1 | 5 | 24 | `zelda_ganon.zar` | efc_ganon_floor_modelT.cmb, ganon_tyuka_ue_model.cmb, ganondorf.cmb, efc_fg_thunder1_modelT.cmb … |
| OBJECT_YDAN_OBJECTS | 0x0036 | 4 | 12 | `zelda_ydan_objects.zar` | maruta_model.cmb, ydan_maruta_model.cmb, ydan_kumohen_modelT.cmb, ydan_ytoge_model.cmb … |
| OBJECT_TOKI_OBJECTS | 0x005E | 4 | 11 | `zelda_toki_objects.zar` | demo_tt_triforce2_0_model.cmb, demo_tt_triforce2_1_model.cmb, demo_tt_triforce_modelT.cmb, left_model.cmb … |
| ~~OBJECT_HAKACH_OBJECTS~~ **PARTIAL: 2 of 8 (6 blocked by mechanism)** | 0x008D | 4 | 8 | `zelda_hakach_objects.zar` | m_Hinv05_model.cmb, m_Hkhuta_model.cmb, m_HkotuBomb00_model.cmb, m_Hsec00_model.cmb … |
| ~~OBJECT_DDAN_OBJECTS~~ **DONE** | 0x002B | 4 | 5 | `zelda_ddan_objects.zar` | ddan_tdoor_model.cmb, ddan_tdoor_yari_model.cmb, ddanh_ago_model.cmb, ddanh_jd_model.cmb … |
| OBJECT_MENKURI_OBJECTS | 0x004D | 4 | 5 | `zelda_menkuri_objects.zar` | l_m_door_model.cmb, l_m_nisekabe1_model.cmb, l_m_nisekabe2_model.cmb, l_sekizoume_modelT.cmb … |
| ~~OBJECT_SPOT18_OBJ~~ **DONE (5 routed, no visual)** | 0x00AF | 4 | 5 | `zelda_spot18_obj.zar` | obj_185_model.cmb, obj_186_model.cmb, obj_s18tubo_model.cmb, obj_s18yari_model.cmb … |
| OBJECT_NIW | 0x0013 | 4 | 2 | `zelda_nw.zar` | chicken.cmb, nw_hane_model.cmb |
| OBJECT_SD | 0x0097 | 4 | 2 | `zelda_sd.zar` | soldier.cmb, soldier2.cmb |
| ~~OBJECT_BDAN_OBJECTS~~ **DONE (2 verified, 2 inert)** | 0x0096 | 3 | 17 | `zelda_bdan_objects.zar` | a_by_door0_model.cmb, a_by_door1_model.cmb, a_by_door2_model.cmb, a_by_door3_model.cmb … |
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

## Pass 12 (2026-07-30) — THE IDENTIFICATION METHOD: three-axis ratio agreement

The remaining queue rows are blocked on identifying WHICH CMB an actor draws. Names are not evidence
(the `hasi`/`hasigo` mismatch) and shapes alone were not enough (the nisekabe pair). This is:

**Measure the actor's N64 draw, then keep the candidate CMB whose HEIGHT, X and Z ratios all agree.**

> **The candidate-side half of this is now one command** (`tools/zar_extents.py`, added 2026-08-04),
> instead of being re-derived by hand every row:
> ```
> tools/zar_extents.py zelda_hidan_objects                 # every CMB with its local bbox + minY
> tools/zar_extents.py zelda_haka_objects --n64 407.2,120,6  # rank candidates against an N64 draw
> ```
> Feed it the `n64h` / `n64foot` that `autostate` reports for the slot. It ranks by three-axis spread
> and flags "all three agree" (<1.05x) versus "modest — possible re-authoring" (<1.30x). Validated by
> reproducing every number earlier passes derived by hand: `m_Hgiro` 0.03004/0.10000/0.10714,
> `m_Hfofo` 576.1 x 536.7 x 193.1, `m_Hsyarin` minY −1327.7. It exits non-zero on a missing ROM or an
> unknown ZAR rather than printing an empty table.

Height, X and Z are three independent estimates of the same scale. The correct mesh makes them agree;
a wrong one will not. Worked example, measured live:

| | N64 measured | `l_bigst` CMB | ratio |
|---|---|---|---|
| height | 90 | 90 | 1.00 |
| X | 300 | 300 | 1.00 |
| Z | 300 | 300 | 1.00 |

Tooling now in place for it:
* `autostate` prints `n64foot=WxH` per slot — the measured N64 extents.
* `scratch/bin/cmb_tex_alpha <ROM_ENV> <archive> <substr>` prints each candidate CMB's geometry bbox
  (and its decoded texture alpha).
* The renderer logs a warning when a height-derived scale disagrees with the footprint past 25% on
  either axis, and an axis-spread warning past 8% for footprint-derived scales.

So the per-row procedure is now mechanical: get the N64 extents from `autostate`, list the archive's
candidate CMB bboxes, and pick the one with three consistent ratios. No name matching, no guessing.

## Pass 13 (2026-07-30) — the method's first routing, and two suspect scales it exposed

`Bg_Ydan_Maruta` -> `ydan_t_hasigo`, identified by MEASUREMENT:

| candidate | bbox | h ratio | x ratio | z ratio | verdict |
|---|---|---|---|---|---|
| `ydan_t_hasigo` | 323 x 1360 x 20 | 0.0993 | 0.0991 | 0.100 | **all three agree** |
| `ydan_maruta` | 4000 x 1243 x 3900 | 0.109 | 0.008 | 0.0005 | inconsistent |
| `ydan_ytoge` | 4816 x 440 x 414 | 0.307 | 0.0066 | 0.0048 | inconsistent |

(N64 draw measured h=135, foot=32x2. The actor branches on params: 0 = rolling spiked log, non-zero =
falling ladder; all Deku Tree instances are params=1.)

Name matching would have picked `ydan_maruta` — *maruta* means log and the trap IS a spiked log — and
been **wrong by 200x on Z**. It had also already assigned `ydan_t_hasigo` to `Bg_Ydan_Hasi`, which draws
the water plane. Two rounds of name reasoning, both wrong; one measurement, settled.

Verified: scale 0.09926, zero cross-check warnings for this archive, draws 6799 px.

### Two pre-existing suspect scales the cross-check immediately found
* **`zelda_syokudai` (0xa4)** — height scale 0.99363 disagrees with the footprint by 1.36x/1.37x. This is
  the wooden-torch routing shipped earlier the same day from a height-only derivation. It renders the
  right MESH, so this is a scale error rather than a misidentification, and it should be re-derived from
  the footprint.
* **`zelda_d_lift` (0x11d)** — 2.08x/2.25x off.

Neither was visible before the cross-check existed.

## Pass 14 (2026-07-30) — d_lift investigated: NO ACTION, and that is the finding

The cross-check flagged `zelda_d_lift` (obj 0x11d, Obj_Lift's collapsing platform) at 2.08x/2.25x.
Investigated; there is nothing to change.

Derived from the warning: measured N64 draw h~36, foot~254x275. The archive has exactly two candidates:

| candidate | bbox | h ratio | x ratio | z ratio |
|---|---|---|---|---|
| `lift_l_model` (AUTO's pick, largest) | 1205 x 355 x 1205 | 0.101 | 0.211 | 0.228 |
| `lift_l_model_h` | 570 x 355 x 527 | 0.101 | 0.446 | 0.522 |

Neither is three-axis consistent, and the alternative is WORSE (4.4x/5.2x off versus 2.08x/2.25x), so
AUTO's existing pick is already the better of the two. There is no third candidate.

The two footprint axes agree closely with each other (2.08 vs 2.25, 8% apart), which by the calibration
in Pass 13/14 points at a proportional re-authoring rather than a misidentification — the 3DS lift is
relatively wider for its height than the N64 one. A partial measurement is the other possibility (a
COLLAPSING platform may be measured mid-collapse, when only some of it draws).

**Recorded as a deliberate non-action.** The check did its job by raising it; the correct response to a
flag is to investigate and then to leave things alone when investigation says so. Two of the first three
flags (torch, lift) turned out to need no change, which is itself worth knowing before someone treats
every warning as a defect queue.

## Pass 15 (2026-07-30) — a whole CATEGORY of false risk: EFFECT BILLBOARDS

Not every extra CMB in an archive corresponds to another actor. Some are effect billboards drawn by
effect code *inside* an actor, which the per-actor routing mechanism can never target — so counting them
as multi-CMB risk overstates this queue.

Measured signature: **very few vertices and a zero-thickness axis.**

| CMB | verts | bbox | what it is |
|---|---|---|---|
| `nw_hane_model` | 3 | 800 x 655 x **0** | cucco feather effect |
| `kd_hinoko_modelT` | 3 | 1000 x 900 x **0** | King Dodongo ember (*hinoko* = spark) |
| `ddanh_bomy_model` | 6 | 1600 x **0** x 1600 | flat floor decal |

**`OBJECT_NIW` is therefore NOT a routing problem.** Three actors own it (`en_niw`,
`en_syateki_niw`, `en_attack_niw`) and the archive holds `chicken.cmb` plus `nw_hane_model`. But the
feather is not a separate actor's model — `en_niw` draws it ITSELF via `gCuccoEffectFeatherModelDL`
alongside its own skinned body. One actor, two draws: our per-actor routing replaces the body (correct)
and the feather stays N64. Nothing to fix.

### Row count, honestly
* 60 rows when ownership is a grep for `OBJECT_*` in actor sources.
* **46** once ownership comes from each actor's `ActorInit` (the grep over-counted; generic actors like
  `door_shutter` merely *mention* dungeon objects).
* At least **3** more drop out as effect-only: `OBJECT_NIW`, `OBJECT_SKJ`, `OBJECT_SHOPNUTS`.

That last number came from a file-SIZE proxy (<4KB) and was WRONG in both directions — see Pass 16, which
ran the proper vert-count sweep. `SKJ` and `SHOPNUTS` do NOT drop out under the real test.

## Pass 16 (2026-07-30) — the proper sweep: the effect-billboard category does NOT re-scope the queue

Ran per-CMB vert counts + flatness over all 46 risk archives (369 CMBs), classifying a CMB as an effect
billboard when it has <= 12 verts AND a zero-thickness axis.

| | count |
|---|---|
| risk rows examined | 46 |
| CMBs classified as effect billboards | **35 of 369** |
| rows that DROP OUT (<= 1 real mesh) | **1** |
| rows genuinely still at risk | **45** |

**My hypothesis that this category would materially shrink the queue is FALSIFIED.** The category is
real — 35 CMBs are effect billboards and cannot be routed per-actor — but they are spread thinly across
archives, so removing them almost never leaves a row with only one real mesh. Only `OBJECT_NIW` qualifies.

It also corrects Pass 15: `OBJECT_SKJ` and `OBJECT_SHOPNUTS` dropped out under the file-size proxy but do
NOT under the vert-count test. The proxy was wrong in both directions, which is why the stricter test was
worth running rather than quoting the cheap number.

### So the real remaining shape of this queue
45 rows, and the largest are dungeon prop archives where many actors share many meshes:

| object | real meshes | actors |
|---|---|---|
| `OBJECT_JYA_OBJ` | 38 | 10 |
| `OBJECT_HAKA_OBJECTS` | 30 | 5 |
| `OBJECT_HIDAN_OBJECTS` | 28 | 13 |
| `OBJECT_GANON` | 19 | 3 |
| `OBJECT_MIZU_OBJECTS` | 18 | 5 |

Each of those is a per-actor identification job of the kind Pass 13 did for one actor: measure the N64
draw, then keep the candidate whose height/X/Z ratios agree. The method works and is mechanical, but it
is one measurement per actor in the scene that holds it — there is no shortcut left to find.

## Pass 17 (2026-07-30) — Water Temple worked by measurement, and TWO limits of the method found

`OBJECT_MIZU_OBJECTS`: 3 of 5 actors routed, each by measuring its N64 draw.

| actor | N64 measured | chosen CMB | evidence |
|---|---|---|---|
| `bg_mizu_water` | h=0, foot=1920x1900 | `m_Wsea00_Mov_modelT` | both axes to 0.4%, scale 1.00407 |
| `bg_mizu_movebg` | h=85, foot=120x120 | `m_WFloat00W_model` | 0.0996/0.100/0.100, spread 1.00x |
| `bg_mizu_shutter` | h=160, foot=160x0 | `m_Wshutter1_model` | **NAME ONLY — see limit 1** |
| `bg_mizu_bwall` | h=0, foot=0x0 | not routed | **skinned — see limit 2** |
| `bg_mizu_uzu` | — | not routed | draws no display list |

The movebg result is the method at its best: it not only picked a mesh but **discriminated against the
near-identical sibling** `m_WFloat00S` (1200x800x1200 vs 1200x853x1200), which is 6% off on height. Name
matching could never separate those two.

### Limit 1 — a FLAT prop yields only two ratios, so it may not discriminate
`bg_mizu_shutter` is a flat plane (`foot=160x0`). With Z unusable there are only two ratios, and
`m_WFloat01`, `m_Wbomb00E`, `m_Wbomb0eE` and `m_Wbomb0eW` all share the same 1200x1200 X/Y and tie at
spread 1.00x. The method is strictly weaker for flat geometry. Kept on the name and FLAGGED as such.

### Limit 2 — the method cannot see SKINNED actors at all
`bg_mizu_bwall`'s model resolves as skinned, so it takes the bone-length scale path and never produces a
bbox measurement (`n64h=0 foot=0x0`). Any skinned actor in this queue is outside this method's reach and
needs a different identification route.

## Pass 18 (2026-07-30) — Water Temple closes at 2 of 5; the shutter is reverted

Verified the three Pass-17 routings by pixel contribution. Two draw, one does not:

| actor | evidence | outcome |
|---|---|---|
| `bg_mizu_water` | 210734 / 212109 px from elevated views | **CONFIRMED** |
| `bg_mizu_movebg` | 47032 px on **instance 2** (0 px on instances 0,1,3,4) | **CONFIRMED** |
| `bg_mizu_shutter` | 0 px across 5 instances x 2 distances x 2 elevations x 4 azimuths | **REVERTED** |

The shutter was the one routing identified on NAME alone — measurement could not decide it (a flat plane
gives only two ratios and four candidates tie). It then showed no contribution anywhere while its slot sat
at `state=2`, meaning we were drawing it and nothing appeared. Either the mesh is wrong, or a closed
shutter is flush inside its wall and never visible. With only a name behind it and a DOOR at stake, it
goes back to the N64 draw. Weakest evidence, first to be cut.

### `movebg` is why the checker had to change
Instances 0, 1, 3 and 4 all read **0 px**; only instance 2 contributed. With 16 instances live, hiding one
routinely reads zero because that instance is occluded or off-screen — so a single-instance test condemns
correct routings. `tools/ahide_check.sh` now sweeps instances, accepts an elevation for flat props, and
prints its own precondition. This is the same failure that reverted a WORKING web routing twice.

## Pass 19 (2026-07-30) — Jabu-Jabu: full ground truth derived, NOTHING routed, and that is correct

`OBJECT_BDAN_OBJECTS` (15 real meshes, 2 actors). The mapping is now fully understood and still nothing
ships, because the evidence does not reach the bar.

**Ground truth (durable — do not re-derive).** Both actors select on `params & 0xFF`, and the CMB names
line up with the N64 display lists — the one case so far where TWO INDEPENDENT naming systems agree:

| N64 display list | CMB | note |
|---|---|---|
| `gJabuBlueFloorSwitchDL` | `bdan_switch_b_model` | b = blue |
| `gJabuYellowFloorSwitchDL` | `bdan_switch_y_model` | y = yellow |
| `gJabuElevatorPlatformDL` | `bdan_ere_model` | *erebeeta* = elevator |
| `gJabuFallingPlatformDL` | `bdan_fdai_model` | *dai* = stand/platform |
| `gJabuObjectsLargeRotatingSpikePlatformDL` | `bdan_toge_model` | *toge* = spike |
| `gJabuWaterDL` | `bdan_bmizu_modelT` | *mizu* = water |

`Bg_Bdan_Switch` types: 0 BLUE, 1 YELLOW_HEAVY, 2 YELLOW, 3 YELLOW_TALL_1, 4 YELLOW_TALL_2.

**Why nothing is routed.**
1. The two switch CMBs are **geometrically identical** (921 x 191 x 921, 300 verts) and differ only in
   TEXTURE, so the three-axis test cannot discriminate between them at all.
2. It can still REJECT: type 4 measures h=39.8 foot=37x37 — a narrow PILLAR — against this wide flat pad,
   a 5.2x gap. So the TALL variants are definitely not this mesh. Type 0 BLUE is a plausible 1.35x, which
   is re-authoring-shaped but is the weakest kind of positive evidence.
3. **No pixel contribution** across 6 instances at elevation with the slot at `state=2`.

### A THIRD limit of the pixel check: props flush with the floor
A floor switch is flush with the ground, so hiding it may change nothing visible — the check is
structurally unable to confirm it, which is inconclusive rather than passing. That joins the two limits
from Pass 17 (flat props give only two ratios; skinned actors produce no bbox measure). The missing piece
for this row is a verification route that can see a flush floor prop, not more analysis.

## Pass 20 (2026-07-30) — full routing audit by SUBMISSION count: clean bill of health

Re-checked every routing with the new `submitted` counter, which answers "did the renderer draw it?"
independently of visibility. This was necessary because 0-pixel readings had already caused THREE reverts
of working code.

| routing | state | submissions | verdict |
|---|---|---|---|
| `ddanh_jd` | 2 | 7992 | drawing |
| `l_elevator` | 2 | 1581 | drawing |
| `l_bigst` | 2 | 879 | drawing |
| `ddanh_kaidan` | 2 | 792 | drawing |
| `l_idomizu` | 2 | 699 | drawing — the FLAT well water, enabled by the footprint measure |
| `syokudai_ki` | 2 | 141 | drawing |
| `m_WFloat00W` | 2 | 9222 | drawing |
| `m_Wsea00` | 2 | 4857 | drawing |
| `m_Wshutter1` | 2 | 4584 | drawing (0 px was a false negative) |
| `bdan_switch_b` | 2 | 708 | drawing (0 px was a false negative) |
| `ddanh_ago` | **4** | 0 | consistent — state 4 means we are NOT drawing it, the N64 is |

**No slot anywhere shows `state=2` with `submits=0`**, which is the only real-failure signature. Every
resolved routing draws; every zero is either an unresolved slot (inert, N64 in control) or a visibility
artefact.

### The reading rule
| state | submits | meaning |
|---|---|---|
| 2 | > 0 | we draw it — the routing works (says nothing about mesh CORRECTNESS) |
| 2 | 0 | **real failure** — resolved but never submitted |
| 0 or 4 | 0 | inert; the N64 draw is in control, so nothing can be broken |

`ddanh_ago` sitting at state 4 with 0 submissions is the benign case, and it is exactly what I earlier
(correctly) called inconclusive from pixels alone. The counter makes that unambiguous rather than a
judgement call.

## Pass 21 (2026-07-30) — Jabu-Jabu platforms: the toolchain runs clean

`OBJECT_BDAN_OBJECTS` finished. `z_bg_bdan_objects.c` carries an explicit DL table indexed by params, and
each entry is corroborated by its CMB name:

| params | N64 display list | CMB | verified |
|---|---|---|---|
| 0 | `LargeRotatingSpikePlatform` | `bdan_toge` | inert (no instance swept) |
| 1 | `ElevatorPlatform` | `bdan_ere` | **scale 0.09999, submits=993** |
| 2 | `Water` (XLU) | `bdan_bmizu_modelT` | inert (no instance swept) |
| 3 | `FallingPlatform` | `bdan_fdai` | **scale 0.10007, submits=993** |

Plus `Bg_Bdan_Switch` type 0 -> `bdan_switch_b` (submits=708, 1.35x re-authoring gap).

**This is the first row that went through without a correction.** Every earlier row produced a revert, a
false negative, or a falsified claim. What changed is not the method but the instruments: identify from the
actor's own DL table, corroborate with the CMB name, verify it DRAWS with `submitted`, cross-check the
scale on three axes, and know the documented limits of each. A row is now routine rather than an
investigation.

## Pass 22 (2026-07-30) — Ice Cavern: the per-slot scale limit, and a diagnostic that was lying

`OBJECT_ICE_OBJECTS` finished. Six actors reach the ZAR and it holds eight CMBs against exactly eight
N64 display lists, so the split is readable straight off each actor's draw:

| actor | N64 DL | CMB | verified |
|---|---|---|---|
| `Bg_Ice_Turara` | `DL_0023D0` | `ice_turara` (tsurara = icicle) | **0.07936, submits=40326** |
| `Bg_Ice_Objects` | `DL_000190` | `ice_brick` | **0.09999, submits=738** |
| `Bg_Ice_Shutter` | `DL_002740` | `ice_wall2` | **0.08962, submits=3633** |
| `Bg_Haka_Sgami` (params 2) | `DL_0021F0` | `ice_trap` | no instance in the rooms swept |
| `Bg_Ice_Shelter` | `gRedIce{Block,Platform,Wall}DL` | see below | 2 of 4 verified |
| `Door_Shutter` | `DL_001D10` | `ice_tobira` | already routed (`door_shutter.cpp`) |

AUTO's largest-CMB pick was `ice_wall_modelT`, so five of the six were rendering a sheet of red ice.

`Bg_Ice_Shelter`'s three DLs are all `POLY_XLU`, and the ZAR holds **exactly three `modelT` meshes**
while every other CMB is a plain `model` — the translucent set and the XLU draw set have the same size
and membership, which is the corroboration. Within the set, shape separates them: `ice_ice` is
near-cubic (950 x 1005 x 945) like the block, `ice_ice3` is wide and 3-group (1499 x 1027 x 1507) like
the "complex structure that can be climbed", `ice_wall` is the sheet.

### The new finding: ONE SLOT CANNOT SERVE AN ACTOR THAT SCALES ITSELF PER TYPE

A slot stores one derived scale, and that scale is measured off the N64 draw **including the actor's
own scale** — `Zelda3D_DrawModelGL` applies `worldScale` alone and never multiplies `actor->scale`.
`Bg_Ice_Shelter` sets `sRedIceScales[] = { 0.1, 0.06, 0.1, 0.1, 0.25 }`, so a single slot for
LARGE/SMALL/KING_ZORA would have rendered all three at whichever size was measured first — a **4.2x**
error between the extremes. Split into one slot per type, and the measurements prove the mechanism:

| type | derived scale | n64h | foot | three-axis |
|---|---|---|---|---|
| LARGE | 0.09999 | 100.5 | 94 x 89 | 0.0999 / 0.0989 / 0.0942 — agree |
| SMALL | 0.06000 | 60.3 | 56 x 54 | 0.0600 / 0.0589 / 0.0571 — agree |

SMALL landing on **exactly** `sRedIceScales[RED_ICE_SMALL]` against a 1005-unit-tall CMB also proves
the CMB is dimensionally 1:1 with the N64 display list — everything in that number is actor scale.

KING_ZORA is routed but **knowingly imperfect**: it is the one type with a non-uniform scale
(`kzIceScale = { 0.18, 0.27, 0.24 }`), which a single `worldScale` cannot express, so it comes out
~1.5x too wide in X. Routed anyway because unrouted means AUTO's wall mesh — right mesh at a wrong
aspect beats the wrong mesh. The proper fix is per-axis scale, and the data already exists: the
measure reports `n64h`, `measFootX` and `measFootZ` as three independent numbers, and only the draw
path is uniform. Not done here because it would change the transform of all ten verified routings at once.

### The `submitted` counter had a false-failure mode, and this row found it

`ice_wall_modelT` reported `state=2 ... submits=0` — the exact signature Pass 20 defined as the ONLY
real failure. It is not one. `ice_wall_modelT` is a **skinned** CMB, and `Zelda3D_TryAuto` sends any
skinned model straight to state 2 with no measurement, deferring to the SkelAnime hook for scale and
draw. `Bg_Ice_Shelter` is a static `Bg_` actor with no SkelAnime, so the hook never fires and the N64
wall keeps drawing — benign, and indistinguishable from a real failure in the old output.

Fixed by printing the discriminator instead of reasoning about it: `autostate` and `submitted` now
carry a `skin=` column. The corrected rule:

| state | skin | submits | meaning |
|---|---|---|---|
| 2 | 0 | > 0 | we draw it — routing works (says nothing about mesh CORRECTNESS) |
| 2 | 0 | 0 | **real failure** — resolved, unskinned, never submitted |
| 2 | 1 | 0 | deferred to the SkelAnime hook; if the actor has no skeleton, N64 stays. Benign |
| 0 or 4 | — | 0 | inert; the N64 draw is in control |

This is the second time an instrument in this arc could not show the other answer, and the second time
the fix was to print the denominator rather than to think harder about the output.

Inert-but-kept: `ice_ice3_modelT` (RED_ICE_PLATFORM is MQ-only, no vanilla instance), `ice_wall_modelT`
(skinned, above), `ice_trap` (no Sgami instance in the rooms swept), KING_ZORA (not an Ice Cavern actor).

## Pass 23 (2026-07-30) — Goron City: every name agrees, and a reminder to use the tool

`OBJECT_SPOT18_OBJ` finished. Four actors, five CMBs, five N64 display lists, and this is the row where
**both naming systems agree on every single entry** — the strongest corroboration this method produces:

| actor | N64 DL | CMB | derived scale | three-axis |
|---|---|---|---|---|
| `Bg_Spot18_Basket` | `gGoronCityVaseDL` | `obj_s18tubo` (tsubo = pot) | 0.10009 | 0.1001 / 0.1008 / 0.1015 — agree |
| `Bg_Spot18_Futa` | `gGoronCityVaseLidDL` | `obj_185` (flat 713 x 84 disc) | 0.09374 | 0.0937 / 0.0898 / 0.0785 — flat-prop spread |
| `Bg_Spot18_Shutter` | `gGoronCityDoorDL` | `obj_186` (12 verts, slab) | 0.09999 | 0.0999 / 0.1003 / 0.0970 — agree |
| `Bg_Spot18_Obj` params 0 | `gGoronCityStatueDL` | `obj_s18zou` (zou = statue) | 0.11157 | 0.1116 / 0.0973 / 0.1066 — 15% spread |
| `Bg_Spot18_Obj` params 1 | `gGoronCityStatueSpearDL` | `obj_s18yari` (yari = spear) | 0.10050 | 0.1006 / 0.0581 / 0.0930 — thin prop |

All five read `state=2 skin=0` with submissions (1146 / 525), so all five draw.

### minY SIGN decides base-anchoring, and it is measurable

Two of these need `noBaseAnchor`. The generic anchor applies `goff = -AutoModelMinY`, so it is a
**no-op whenever minY == 0** and only ever moves a model when minY != 0 — which splits by sign:

* `minY < 0` → centre-origin prop (En_Goroiwa's sphere). The anchor is what stops it sinking.
* `minY > 0` → authored ABOVE its actor's origin on purpose, in the same space as the N64 display
  list it replaces. Anchoring drags it back down to the origin.

`obj_185` is the extreme case at minY = 2041, and it was settled by MEASUREMENT rather than reasoning:
the lid actor and the vase actor occupy the **same position**, `pos=(3,-3,20)` for both, so the lid's
entire height comes from its display list being authored at the vase's rim. Anchoring would have
dropped it ~191 units onto the floor inside the vase. `obj_s18yari` is the same class at minY = 162,
with its whole bbox off-origin (`x[189..361] z[111..1026]`) because the spear is authored in the
STATUE's space exactly as its N64 DL is.

This is stated as a rule about *this row's two entries*, not as a global change — the sign test looks
general, but every existing routing has minY == 0 or a verified anchor, so nothing was re-derived from it.

### NO visual confirmation was obtained for this row, and that is recorded rather than papered over

The Goron City pot could not be got on screen from the entrances tried (`0x14D`, plus a teleport to
the actor's own coordinates): the actor exists and measures, but its room does not render from there.
`tools/ahide_check.sh 0x15c 450` correctly returns **INCONCLUSIVE**. The row rests on the submission
counts, the three-axis agreement, the dual-name corroboration and the position measurement — not on pixels.

> **CORRECTION (2026-08-04): "correctly returns INCONCLUSIVE" is VOID.** `ahide_check.sh` was broken
> for this entire window — it cd'd to the repo's *parent*, so every REPL call failed silently and it
> printed INCONCLUSIVE for **every** input, including inputs that do draw. It was not testing anything
> here. The row's other evidence (submission counts, three-axis agreement, position measurement) is
> unaffected and still stands; only the sentence about the pixel check is withdrawn. See instrument
> I022. Any `ahide_check` result recorded between 2026-07-30 and 2026-08-04 must be re-run before it
> is believed.

### Use `ahide_check.sh`; do not hand-roll `ahide` + `isolate`

Before running the tool I hand-rolled the same pair and got four confident-looking numbers — 221 px,
158 px, 906 px, 57 px, each with a plausible bbox — **for an object that was not in the frame at all**.
Only opening the screenshot revealed an empty room. The raw pair cannot distinguish "hiding it changed
nothing" from "it was never on screen"; `ahide_check.sh` exists precisely because that distinction is
invisible in the diff, and it said INCONCLUSIVE where my hand-rolled version said 906.

That is the same failure this arc keeps producing, in a new place: not a broken tool this time, but a
GOOD tool bypassed. The rule earns a sharper form — when a wrapper exists for a check, the wrapper's
refusals are the point of it.

## Pass 24 (2026-07-30) — Bottom of the Well: a complete mapping the MECHANISM cannot mostly use

`OBJECT_HAKACH_OBJECTS`: eight CMBs against exactly eight `gBotw*` display lists, and all eight map,
each name backed by an independent geometric signature:

| N64 DL | CMB | signature |
|---|---|---|
| `gBotwCoffinLidDL` | `m_Hkhuta` | huta = lid; a 500 x 118 x 1200 slab |
| `gBotwBombSpotDL` | `m_HkotuBomb00` | name-exact |
| `gBotwWaterFallDL` | `m_Hwat00_FO_modelT` | FO = fall; the one water mesh WITH height |
| `gBotwWaterRingDL` | `m_Hwat00_Down_modelT` | flat 22000 x 0 x 19600 surface |
| `gBotwBloodSplatterDL` | `m_Hsec00_modelT` | the only remaining `modelT`: a flat 6-vert decal at y=20 |
| `gBotwFakeWallsAndFloorsDL` | `m_Hsec00` | 3 groups, height 2000 — walls need it |
| `gBotwThreeFakeFloorsDL` | `m_Hsec03` | flat, 54 verts: three floor planes |
| `gBotwHoleTrap2DL` | `m_Hinv05` | bbox lies ENTIRELY BELOW the origin, y[-2400..0] — a pit |

**Only two of the eight are routed, and the mapping is not what blocks the other six.** Three distinct
mechanism limits do, and they are recorded here so the next pass does not re-derive this mapping only
to hit the same walls:

1. **A forced slot replaces the actor's WHOLE draw.** `Zelda3D_TryAuto` returns 1 and the caller skips
   the N64 draw entirely, so an actor emitting more than one display list loses every list but the
   substituted one. That rules out `Bg_Haka_Water` (ring **plus** fall, the latter at its own
   `Matrix_Translate(0,92,-1680)` + `Scale(0.1)`) and `Bg_Haka_Megane` params 0, which draws
   `sDLists[0]` **and** `gBotwBloodSplatterDL`. Routing either would delete geometry that currently
   renders — a regression, not a partial win. This limit has been latent all along; every prior row
   happened to use single-DL actors.
2. **The ZAR comes from the actor's object BANK slot, not from where its geometry lives.**
   `Bg_Haka_Zou`'s `ActorInit` declares `OBJECT_GAMEPLAY_KEEP` and it fetches HAKACH or HAKA at runtime
   into a private index, so `Zelda3D_ActorObjectId` reports GAMEPLAY_KEEP and the lookup can never
   reach this ZAR — its name-exact `gBotwBombSpotDL -> m_HkotuBomb00` is unreachable.
3. `Bg_Haka_Megane` params >= 3 uses `OBJECT_HAKA_OBJECTS`, so only params 0/1/2 were ever candidates.

### The two that landed

| slot | scale | n64h | foot | reading |
|---|---|---|---|---|
| `m_Hkhuta` | 0.08463 | 10.0 | 50 x 120 | submits=5340 |
| `m_Hsec03` | 0.09999 | **0.0** | 340 x 400 | submits=885 |

`m_Hsec03` is the **footprint fallback working exactly as designed**: the mesh is flat (y extent 0) so
the height measure yields nothing, and X and Z independently give 340/3400 = 0.100 and 400/4000 = 0.100.
This is the path `l_idomizu` needs; `l_idomizu`'s problem was never getting measured at all, not the
flatness per se.

`m_Hkhuta` is the clearest case yet of the **height-primary choice being the worse estimate**: X and Z
agree at *exactly* 0.100 (50/500, 120/1200) while height dissents at 0.0847, i.e. Grezzo authored the
lid 18% thicker. Two axes agreeing exactly against one outlier still passes the identity test — a wrong
mesh disagrees on all three — but it means the lid renders ~15% small in plan. Noted, not acted on: the
same single-`worldScale` limitation as Pass 22's King Zora block, and changing the scale source would
move every verified routing at once.

`m_Hinv05` (params 2, the hole trap) never resolved — no instance in the rooms swept. Inert.

### The visual leg is now 0 for 2 on dungeon props

> **CORRECTION (2026-08-04): this whole section's premise was an artefact of a BROKEN TOOL.** The
> "visual leg is 0 for 2" was not a property of dungeon props at all — `ahide_check.sh` cd'd to the
> repo's parent and returned INCONCLUSIVE for *every* input in this window, so both data points are
> void and the pattern they suggested never existed. After the fix the tool returns DRAWS (1333 px)
> on a live routed prop. The reasoning below about flat props, occlusion and off-screen instances
> remains sound as a description of the check's real limits — it just was not what happened here.
> See instrument I022.

`ahide_check.sh` returned INCONCLUSIVE for the coffin lid (elev 25, all instances) as it did for the
Goron City pot. Both rows rest on submission counts and three-axis agreement instead. That is two
consecutive rows where the pixel check could not confirm what the counter did — consistent with its
documented limits (flat props, occlusion, off-screen instances), but worth stating plainly: for
dungeon interior props the pixel check is currently the weakest of the three instruments, and the
submit counter plus the three-axis ratio are carrying the verification.

## Pass 25 (2026-07-30) — AUDIT of every shipped routing against the multi-DL limit

Pass 24 found that a forced slot replaces the actor's **whole** draw, so an actor emitting more than
one display list loses all but the substituted one. That limit had been latent since the first row, so
this pass re-checked **all 44 routed actors** rather than only the new ones. Denominator stated because
the first attempt at this audit resolved 0 of 44 (a name-mangling bug) and printed `FILE_NOT_FOUND`
per row — which is the only reason it was not mistaken for a clean result.

**44 of 44 resolved.** 13 had more than one DL reference; skinned actors are exempt by construction
(the skinned branch returns 0 and defers to the actor's own Draw), leaving 10 to read:

| actor | verdict |
|---|---|
| `Bg_Ice_Shelter`, `Bg_Haka_Sgami`, `Bg_Bdan_Objects`, `En_Kusa`, `Bg_Ydan_Maruta`, `Bg_Ydan_Hasi`, `Bg_Mori_Hashigo` | alternative branches — SAFE |
| `En_Ishi` | two separate Draw *functions*, one list each — SAFE |
| `Bg_Ydan_Sp` | one list normally; the *breaking* animation adds 8x `gDTUnknownWebDL` — transient loss only |
| **`Obj_Syokudai`** | **SIMULTANEOUS — stand + `gEffFire1DL` whenever `litTimer != 0`** |

### Defect 1: routed torches lost their flame (shipped)

`ObjSyokudai_Draw` emits the stand and then the flame at its own billboarded matrix. Both the forced
wooden-torch entry **and the generic per-object AUTO slot** replaced the whole draw, so this applied to
every torch variant, not just the routed one. Withdrawing the forced entry alone was not enough — that
only demotes wooden torches to the generic AUTO pick, which suppresses the draw just the same. Fixed by
skipping `ACTOR_OBJ_SYOKUDAI` in `Zelda3D_TryAuto` next to the existing `OBJECT_KANBAN` /
`ACTORCAT_DOOR` skips. Confirmed: `autostate` now reports **zero** syokudai slots.

**Evidence honesty.** The two steps are each read directly from source — the double emission, and the
`if (!Zelda3D_TryDrawActor(...)) actor->draw(...)` call site — but *the flame loss itself was never
observed*. Every torch found is unlit, so `auto 0` vs `auto 1` renders identically and cannot
discriminate; the A/B produced 2733 changed px that are the torch MESH swapping, not fire. THE TEST
is a LIT torch A/B'd on `auto`. The skip is correct regardless, because a multi-list actor cannot be
faithfully replaced by a one-mesh substitution — but the causal claim stays untested and is filed that way.

### Defect 2: the Forest Temple ladder CLASP was rendering as a ladder

`Bg_Mori_Hashigo` is two props: `BgMoriHashigo_Draw` switches on params between `gMoriHashigoClaspDL`
(`HASHIGO_CLASP = -1`, i.e. 0xFFFF as u16) and `gMoriHashigoLadderDL` (`HASHIGO_LADDER = 0`). The slot
was params-agnostic, so the clasp got the ladder mesh. Split into two entries.

The clasp's CMB had been sitting in the same ZAR the whole time, on this document's own
"unrouted leftovers" list described as *"a ladder variant/stop"*: `l_hasigotome` — **"tome" is Japanese
for catch/clasp** — and the geometry settles it, a 20 x 46 x 40 bracket against the ladder's
32 x 227 x 2 rail. **A leftover is a lead, not a dead end.** Reading the actor's DL *switch* found its
owner where reading CMB names alone had not; the remaining leftover (`l_tikaori`) deserves the same
treatment before being written off.

Not verified live: no `Bg_Mori_Hashigo` instance spawned in any of the 21 Forest Temple rooms swept
(60 actors listed, zero of id 0xE2), so the split is a code-level correction only. It is strictly
better than a params-agnostic slot that provably gave the clasp the wrong mesh.

### The rule this adds to the row checklist

Before routing an actor, read its Draw for a SECOND display list, not just for which list it picks.
`grep -c "gSPDisplayList\|Gfx_DrawDList"` over the actor's file is the cheap screen; a count above 1
needs eyes on the code to tell alternative branches from simultaneous emission.

## Pass 26 (2026-07-30) — Shadow Temple: the multi-DL screen pays for itself, and a name loses to a measurement

`OBJECT_HAKA_OBJECTS` — 32 CMBs, 8 actors, the third-largest archive in the queue. Entered from the
opposite end than usual: the crossed-white-planes object flagged at the end of Pass 25 turned out not to
be a separate bug but *this row*, with AUTO handing every actor the same largest CMB.

### The multi-DL screen FIRST, which is what Pass 25 added to the checklist

| actor | lists per draw | routable? |
|---|---|---|
| `Bg_Haka_Trap` | 1 (`sDLists[params]`) | **yes — the only clean one** |
| `Bg_Haka_Gate` | gate + `gEffFire1DL` for SKULL; **two** halves for FLOOR | only STATUE / GATE |
| `Bg_Haka_Ship` | params 0 draws hull **+ two oars**; params != 0 single | only params != 0 |
| `Bg_Haka_Tubo` | pot **+** `BgHakaTubo_DrawFlameCircle` | **no** |
| `Bg_Haka_Megane`, `Bg_Haka_MeganeBG` | alternatives | yes in principle |
| `Bg_Haka_Sgami` | alternatives (ice variant already routed) | — |
| `Bg_Haka_Zou` | 1, but object comes from `GAMEPLAY_KEEP` | unreachable (Pass 24) |

**Most of this archive is structurally out of reach**, and knowing that before doing 32 identifications
is the entire value of running the screen first. `Bg_Haka_Tubo` would have deleted a flame circle
exactly as `Obj_Syokudai` deleted its flame.

### Then the measurement overruled the names — twice

The first pass picked three "obvious" mappings from Japanese alone. The three-axis check rejected two:

| trap | name-based pick | measured | verdict |
|---|---|---|---|
| SPIKED_BOX | `m_Hkenzan` (kenzan = spike bed) | 0.0938 / 0.100 / 0.100 | **agrees — routed** |
| PROPELLER | `m_Hsyarin` (syarin = wheel) | 0.0200 / 0.0218 / **0.0033** | **REJECTED** |
| GUILLOTINE_SLOW | `m_Hgiro` (giro = guillotine) | **0.0300** / 0.100 / 0.107 | right mesh, withdrawn here — **RESTORED in Pass 27** |

**PROPELLER was simply the wrong mesh.** A 6x spread is the wrong-mesh signature, not a re-authoring
gap. The N64 propeller measures 53 x 58 x 16 — about 530 x 580 x 160 at the usual 0.1 — and the ZAR
mesh with that shape is `m_Hfofo` (576 x 537 x 193), which measures 0.099 / 0.101 / 0.083 and now
derives a sane scale of 0.09875 against `m_Hsyarin`'s absurd 0.01996. The clincher is unfixable by
scaling: `m_Hsyarin`'s LONG axis is Z, while the N64 list's SHORTEST axis is Z. A name that reads
perfectly ("wheel" for a propeller) lost to three numbers.

**GUILLOTINE_SLOW is the right mesh that cannot be drawn correctly yet.** X and Z hit 0.100 and 0.107
exactly, so `m_Hgiro` is certainly it — but height gives 0.0300, because the OoT3D mesh is 13555 tall
against a 407-tall N64 list. Since the derived scale comes from HEIGHT, routing it renders the blade
3.3x TOO NARROW, which is worse than the faithful N64 draw. Withdrawn rather than shipped wrong.

### Height-primary is now 3 for 3 as the wrong choice when the axes disagree

King Zora's ice block, the Bottom of the Well coffin lid, and now the guillotine: every time two
footprint axes agree exactly and height dissents, height is the outlier and the height-derived scale is
the visibly wrong one. That is no longer an observation, it is a pattern with three independent
instances, and it makes **per-axis scale (or at minimum footprint-preferred scale when X and Z agree
and Y does not)** the highest-value mechanism change left in this arc — it would unblock the guillotine,
fix the coffin lid's 15% and King Zora's 1.5x, and cost nothing where all three axes already agree.

## Pass 27 — the scale comes from `actor->scale`, confirmed by measurement (2026-08-04)

Height-primary is gone. The replacement went through a **wrong intermediate that the audit caught**,
which is the more useful half of this entry.

### The root cause

`Zelda3D_EmitModelDraw` applies `worldScale` uniformly and **never multiplies `actor->scale`**, so every
auto-routed prop has had to recover the actor's scale from pixels: `scale = measured N64 extent / CMB
extent`. That inference is only necessary when Grezzo RE-AUTHORED the mesh at different proportions.
When the CMB is dimensionally 1:1 with the N64 display list, the ratio simply **is** `actor->scale` — a
number the engine holds exactly, per-axis, with no measurement noise. So the measurement's job is to
**confirm the 1:1**, not to produce the number. Any axis whose ratio lands on `actor->scale` has done so.

### The wrong intermediate: axis consensus (filed as C046, falsified the same day)

The first cut took "the largest cluster of axes agreeing within 2%", on the reasoning that two
independent measurements landing within 2% is not a coincidence. **They are not independent.** For a
prop with a SQUARE OR ROUND FOOTPRINT, X and Z are one measurement taken twice — they agree
automatically, *including* when the mesh is genuinely re-authored chunkier in plan. The full audit
caught it moving four props it had no business touching:

| prop | h / x / z | actor scale | consensus did | correct |
|---|---|---|---|---|
| `zelda_syokudai` torch | 0.994 / 0.733 / 0.725 | 1.0 | **−26.6%** | height is the 1:1 axis |
| `zelda_d_lift` | 0.101 / 0.210 / 0.228 | 0.1 | **+116.2%** | height is the 1:1 axis |
| `zelda_hidan_objects` | 0.025 / 0.150 / 0.150 | 0.1 | **+500.1%** | nothing matches — leave alone |
| `zelda_jya_iron` | 0.075 / 0.145 / 0.153 | 0.1 | **+98.8%** | nothing matches — leave alone |

The torch is the whole argument in one row: it is the documented re-authored asset, its square footprint
makes X and Z agree by construction, and bending it to the N64 plan size is exactly the parity-diff
chasing this project forbids. `d_lift` had even been investigated and deliberately left alone in an
earlier pass (`007a0fc7`). **Axis agreement is not evidence; agreement with a known ground-truth
quantity is.**

### What shipped

Any axis whose ratio is within 2% of the corresponding `actor->scale` component confirms the 1:1, and
the scale becomes `actor->scale` exactly. No confirming axis → nothing changes. A **non-uniform**
`actor->scale` is refused out loud rather than averaged away — that is King Zora's red ice
(`kzIceScale = {0.18, 0.27, 0.24}`), which still needs a per-axis draw path.

**Audit: all 13 routed scenes, every room swept. 35 of 36 forced slots resolved, 28 confirmed 1:1, and
only 7 move by more than 2%.**

| routing | was | now | axes | note |
|---|---|---|---|---|
| `m_Hgiro` guillotine | 0.03004 | **0.10000** | xz | **+232.9%** — row RESTORED, submits=17289 |
| `m_Hkhuta` coffin lid | 0.08463 | **0.10000** | xz | +18.2% |
| `zelda_bwall` | 0.11665 | **0.10000** | x | −14.3% — **single-axis confirmation** |
| `m_Hkenzan` spiked box | 0.09379 | **0.10000** | xz | +6.6% — shipped silently small in Pass 26 |
| `bdan_toge` | 0.09528 | **0.10000** | xz | +5.0% |
| `zelda_bombiwa` | 0.09583 | **0.10000** | z | +4.3% — **single-axis confirmation** |
| `zelda_spot09_obj` | 0.97662 | **1.00000** | xz | +2.4% |

`m_Hkenzan` is the find nobody was looking for: it shipped last pass with its three axes recorded as
"agrees", while height sat 6.6% under the true scale. The two **single-axis** rows are the weakest
evidence in the set and are named as such in claim C048's falsifier — one confirming axis is a real
signal (landing within 2% of the actor's exact scale by chance is unlikely) but it is one signal.

Not fixed and not claimed fixed: `zelda_hidan_objects`, `zelda_jya_iron`, `zelda_bdan_objects` and
`bdan_switch_b` confirm on **no** axis, so they keep the old derive. That is the honest outcome — these
are AUTO's largest-CMB pick out of large shared archives, i.e. near-certainly the wrong mesh for whatever
actor measured them, and `OBJECT_HIDAN_OBJECTS` / `OBJECT_JYA_OBJ` are still the top two unrouted rows in
this queue. `zelda_bombf` swings 34x on X between identical runs (C047) and is excluded by the same gate.

Slot 29 (`ddanh_ago`) was never reached by any entrance tried and is therefore **not audited** — that is
a gap in the evidence, not a clean bill of health.


## Pass 28 — OBJECT_HIDAN_OBJECTS, the biggest row in the queue (2026-08-04)

31 CMBs and, after the declaration check, **13 actors** rather than the 16 a grep reports —
`Door_Shutter`, `Door_Killer` and `En_Door` reach this archive only through a per-scene secondary-object
table and declare `GAMEPLAY_KEEP` / `DOOR_KILLER` themselves. **14 routed, 1 withdrawn, 6 excluded.**

### The multi-DL screen, run first as usual

Four of the thirteen draw more than one list and can never be routed to a single mesh:

| actor | DLs | why |
|---|---|---|
| `Bg_Hidan_Rock` | 1 or 2 | base block + a conditional vertical flame (`unk_16C`) |
| `Bg_Hidan_Sima` | 1 to 5 | platform + a fireball burst loop |
| `Bg_Hidan_Sekizou` | 1 to 9 | statue + per-direction flame timers |
| `Bg_Hidan_Rsekizou` | **9 always** | spinning flamethrower + 8 fireballs |

**Two more are excluded for a reason the screen does not catch** — both are the `zelda_bombf` failure
mode, an actor whose measured extents are not a constant:

- `Bg_Hidan_Firewall` — the mesh is certain (`m_Ffirewall_modelT` is the only firewall mesh and it draws
  one list), but its scale is **non-uniform and animated**: x=z=0.12 fixed while y sweeps 0.01→0.1 every
  cycle. A single `worldScale` cannot express it, and the bbox measure would sample whichever y the
  animation happened to be at, so the derived scale would be luck of the frame. Same class as King
  Zora's ice — it wants a per-axis draw path, not a routing.
- `Bg_Hidan_Kousi` 1 and 2 — `m_Fkousi2` and `m_Fkousi3` have **identical extents** (1597.4 × 1199.2 ×
  44.6), so the three-axis test cannot separate them. Routed anyway, unlike the mirrored `m_HhasamiN/S`
  spike walls: these are same-size gratings so a swap is at worst a mirrored fence, whereas leaving them
  unrouted hands them AUTO's largest-CMB pick, which in this archive is `m_FOtiBFhead` — a 26154-unit
  pillar. A wrong grating beats a tower.

### Identification: 12 of 14 confirmed 1:1 on first attempt

`tools/zar_extents.py` gave the candidate table and the live sweep gave the verdicts. Every routed slot
resolves, draws, and confirms against `actor->scale`:

| actor / params | CMB | axes confirmed |
|---|---|---|
| Fwbig (any) | `m_FfirewallBIG` | 2/2 (z unmeasurable) |
| Fslift (any) | `m_Fhocklift` — "hocklift" = hookshot elevator | **3/3** |
| Hrock 0 | `m_FOtiBFhead` — 26154 tall, by far the tallest mesh, = "TallestPillarAboveRoomBeforeBoss" | **3/3** |
| Hrock 1, 2 | `m_FOtiMINI` (both params select the SAME N64 DL) | **3/3** |
| Kowarerukabe 0 | `m_Fbmfl` — the only horizontal slab (1601 × 72 × 1700) = cracked stone FLOOR | 1/3 |
| Kowarerukabe 1 | `m_Fbmwall1` | 2/2 |
| Kousi 0, 1, 2 | `m_Fkousi1` / `2` / `3` | 2/2 each |
| Dalm 0 / nonzero | `m_Fdalm_model` / `m_FdalmHEAD` | **3/3** each |
| Hamstep 0 / nonzero | `m_FhamSTEP_model` / `m_FhamSTEP_1` | **3/3** each |
| Syoku (no params) | `m_Fmboss` | **3/3** |

`m_Fmboss` is the satisfying one: `Bg_Hidan_Syoku` draws `gFireTempleFlareDancerPlatformDL`, the Flare
Dancer is the Fire Temple **mini-boss**, and `m_Fmboss` is the only mesh named for one. That was a pure
name argument — the kind this arc has watched lose twice — and the measurement confirmed it 3/3.

Two params traps, both read from source rather than assumed: `Bg_Hidan_Dalm`'s Init does `params &= 0xFF`
after lifting a switch flag out of the high byte, and `Bg_Hidan_Hrock`'s Init does
`params = (params >> 8) & 0xFF`, so at DRAW time — which is when a forced slot matches — Hrock's params
IS the `dlists[]` index.

### The one withdrawal, and the limitation it exposed

`Kowarerukabe` params 2 (LARGE_BOMBABLE_WALL) is **withdrawn**. Its slot measures an N64 draw 120 tall ×
160 wide and **no mesh in the archive is 1200 × 1600**: `m_Fbmwall2` (1600 × 1400) matches only on X,
`m_Fbmwall3` (990 × 1200) only on height.

> **CORRECTION (2026-08-04, same day): the world-vs-local explanation offered here was WRONG.** It was
> implemented — rotate the model footprint by `shape.rot.y` before comparing — and then falsified by the
> interpreter source and by a rotated prop. `fast/interpreter.cpp` does not measure world X/Z: it
> projects each eye-space vertex onto `mv[0]` and `mv[2]`, the columns of the live MODELVIEW matrix,
> which are the **model's own** X and Z axes. The yaw is already divided out, so `measFootX`/`measFootZ`
> are directly comparable to the CMB's LOCAL extents and no correction is needed. The proof is
> `zelda_gs`, which measures at yaw=90°: uncorrected it compares h=0.10033 x=0.10791 z=0.09462 against an
> actor scale of 0.1 (14% spread), and rotating by that same 90° swaps its axes to x=0.15408 z=0.06627 —
> a 2.3× spread. The correction was reverted. It had changed no shipped scale (confirmed routings are
> overwhelmingly yaw=0) but would have mis-scaled any future rotated prop. Claim **C049**.

So LARGE_BOMBABLE_WALL is withdrawn with **no explanation yet** — that is the honest position. Its slot
measures 120 × 160 in the actor's own frame and no mesh in the archive is 1200 × 1600. The remaining
candidates are that the actor draws a mesh from a different archive, that AUTO's measure caught a
partially-culled draw, or that the wall is one of the meshes with a *different* aspect than its N64
counterpart. Re-open it with a fresh measurement, not with the theory above.

Params 1 shows the method working in the small: `m_Fbmwall2` was the first guess and the measurement
rejected it (0.0375 / 0.0714 against an actor scale of 0.1); the mesh that is 0.1 on both is
`m_Fbmwall1` (600 × 1000), corrected by arithmetic and then re-measured to 2/2.
