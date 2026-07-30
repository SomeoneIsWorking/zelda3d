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
| OBJECT_DDAN_OBJECTS | 0x002B | 4 | 5 | `zelda_ddan_objects.zar` | ddan_tdoor_model.cmb, ddan_tdoor_yari_model.cmb, ddanh_ago_model.cmb, ddanh_jd_model.cmb … |
| OBJECT_MENKURI_OBJECTS | 0x004D | 4 | 5 | `zelda_menkuri_objects.zar` | l_m_door_model.cmb, l_m_nisekabe1_model.cmb, l_m_nisekabe2_model.cmb, l_sekizoume_modelT.cmb … |
| OBJECT_SPOT18_OBJ | 0x00AF | 4 | 5 | `zelda_spot18_obj.zar` | obj_185_model.cmb, obj_186_model.cmb, obj_s18tubo_model.cmb, obj_s18yari_model.cmb … |
| OBJECT_NIW | 0x0013 | 4 | 2 | `zelda_nw.zar` | chicken.cmb, nw_hane_model.cmb |
| OBJECT_SD | 0x0097 | 4 | 2 | `zelda_sd.zar` | soldier.cmb, soldier2.cmb |
| OBJECT_BDAN_OBJECTS | 0x0096 | 3 | 17 | `zelda_bdan_objects.zar` | a_by_door0_model.cmb, a_by_door1_model.cmb, a_by_door2_model.cmb, a_by_door3_model.cmb … |
| OBJECT_FD | 0x009C | 3 | 11 | `zelda_fd.zar` | m_FBRsizumi_model.cmb, valbasiabody.cmb, valbasiagnd.cmb, valbasiahead.cmb … |
| OBJECT_EFC_STAR_FIELD | 0x0092 | 3 | 7 | `zelda_efc_star_field.zar` | demo_rock_model1.cmb, demo_rock_model2.cmb, fire_rock_model1.cmb, fire_rock_model2.cmb … |
| OBJECT_FHG | 0x005A | 3 | 6 | `zelda_fantomHG.zar` | ganonhorse.cmb, f_ganon_efc_modelT.cmb, gnf_bakuhatsu_modelT.cmb, gnf_inazuma_modelT.cmb … |
| OBJECT_KINGDODONGO | 0x0019 | 3 | 4 | `zelda_kdodongo.zar` | ddanh_bomy_model.cmb, g_ddg2_fire_model.cmb, kingdodongo.cmb, kd_hinoko_modelT.cmb |
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
| OBJECT_WALLMASTER | 0x000B | 2 | 3 | `zelda_wm2.zar` | floormaster.cmb, fallmaster.cmb, shadow_f_model.cmb |
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
| OBJECT_SPOT12_OBJ | 0x0162 | 2 | 2 | `zelda_spot12_obj.zar` | s12gate_model.cmb, s12saku_model.cmb |
| OBJECT_SPOT11_OBJ | 0x016F | 2 | 2 | `zelda_spot11_obj.zar` | obj_112_modelT.cmb, obj_s11wall_model.cmb |
| OBJECT_BOWL | 0x0178 | 2 | 2 | `zelda_bowl.zar` | bowling_p1_model.cmb, bowling_p2_model.cmb |
| OBJECT_SPOT01_MATOYAB | 0x0181 | 2 | 2 | `zelda_spot01_matoyab.zar` | c_matoate_before_model.cmb, c_s01tomegate_model.cmb |
| OBJECT_MU | 0x0182 | 2 | 2 | `zelda_mu.zar` | couple.cmb, marketpeople.cmb |

**60 objects at risk.** Generated by the sweep recorded in the 2026-07-30 audit journal.
