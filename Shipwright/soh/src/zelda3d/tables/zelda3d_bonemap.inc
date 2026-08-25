// HAND-MAINTAINED per-character map: N64<->OoT3D bone correspondence + scale (+ optional custom
// overrides). Seeded by tools/zelda3d_skel_export.py (which writes tools/skeldata/zelda3d_bonemap.seed.inc
// for reference) but THIS FILE IS EDITED BY HAND and never overwritten by the generator — fix
// partial/wrong rigs here directly. boneToLimb[oot3d bone id] = N64 limb driving that bone
// (-1 = keep the CMB rest pose). Add custom mapping types by extending Zelda3DBoneMap + a new macro.
// Tagged (not anonymous) so focused renderer contracts can forward-declare `struct Zelda3DBoneMap`
// without importing this table.
typedef struct Zelda3DBoneMap {
    const char* zar;
    float scaleRatio;           // * actor->scale = OoT3D->world scale (0 = derive at runtime)
    int boneCount;              // valid boneToLimb entries (= OoT3D bone count)
    signed char boneToLimb[64]; // [oot3d bone id] -> N64 limb index (-1 = rest)
    float groundOffsetOverride; // optional: extra model-space Y (0 = auto). Example custom field.
} Zelda3DBoneMap;

// Common case (no custom overrides). For a custom entry, write the full initializer instead.
#define ZELDA3D_BONEMAP(zar_, scale_, count_, ...) { zar_, scale_, count_, { __VA_ARGS__ }, 0.0f }

static const Zelda3DBoneMap kZelda3dBoneMaps[] = {
    // --- HAND-FIXED (partial/divergent rigs corrected by hand) ---
    // Saria: hand-derived correspondence is CORRECT (OoT3D(19) adds a chest bone b2 above the arms
    // + a 4th bone per leg the N64 rig(16) lacks -> -1/rest; arms b3-5/b6-8, legs b11-13/b15-17,
    // head b10). DISABLED: with the current "replace" retarget her divergent rest pose skins to
    // exploded geometry (correct indices aren't sufficient -> needs the world-orientation retarget).
    // Re-enable once that lands; falls back to N64 meanwhile.
    // ZELDA3D_BONEMAP("/actor/zelda_sa.zar", 1.04848f, 19, -1,1,-1,2,3,4,5,6,7,8,15,9,10,11,-1,12,13,14,-1), // Saria
    // --- AUTO-SEEDED (clean matches; safe to keep, re-seed via the tool) ---
    ZELDA3D_BONEMAP("/actor/zelda_am.zar", 1.00000f, 5, -1, 3, 1, 4, 7), // object_am
    ZELDA3D_BONEMAP("/actor/zelda_ane.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_ane
    ZELDA3D_BONEMAP("/actor/zelda_ani.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_ani
    ZELDA3D_BONEMAP("/actor/zelda_bg.zar", 1.00000f, 10, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9), // object_bg
    ZELDA3D_BONEMAP("/actor/zelda_bob.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_bob
    ZELDA3D_BONEMAP("/actor/zelda_boj.zar", 1.01667f, 16, -1, 0, 1, 2, 3, 4, 5, 6, 7, 14, 8, 9, 10, 11, 12, 13), // object_boj
    ZELDA3D_BONEMAP("/actor/zelda_brob.zar", 1.00000f, 6, -1, 1, 3, 5, 6, 8), // object_brob
    ZELDA3D_BONEMAP("/actor/zelda_bw.zar", 1.00000f, 5, -1, 1, 3, 6, 8), // object_bw
    ZELDA3D_BONEMAP("/actor/zelda_cob.zar", 1.23047f, 13, -1, 1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14), // object_cob
    ZELDA3D_BONEMAP("/actor/zelda_crow.zar", 1.00000f, 8, -1, 1, 2, 3, 4, 5, 6, 7), // object_crow
    ZELDA3D_BONEMAP("/actor/zelda_cs.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_cs
    ZELDA3D_BONEMAP("/actor/zelda_dekubaba.zar", 0.99999f, 3, -1, 4, 1), // object_dekubaba
    ZELDA3D_BONEMAP("/actor/zelda_dekunuts.zar", 1.00000f, 10, -1, 1, 2, 7, 10, 4, 14, 16, 19, 21), // object_dekunuts
    ZELDA3D_BONEMAP("/actor/zelda_dnk.zar", 1.00000f, 9, -1, 8, 9, 1, 5, 4, 2, 3, 6), // object_dnk
    ZELDA3D_BONEMAP("/actor/zelda_dns.zar", 1.00000f, 11, -1, 10, 11, 1, 2, 3, 4, 5, 6, 7, 8), // object_dns
    ZELDA3D_BONEMAP("/actor/zelda_dodojr.zar", 1.00000f, 6, -1, 7, 9, 10, 1, 4), // object_dodojr
    ZELDA3D_BONEMAP("/actor/zelda_dodongo.zar", 1.00000f, 18, -1, 7, 8, 16, 17, 18, 19, 23, 24, 25, 26, 9, 10, 11, 1, 2, 3, 5), // object_dodongo
    ZELDA3D_BONEMAP("/actor/zelda_dog.zar", 1.00000f, 9, -1, 1, 10, 9, 2, 8, 7, 3, 11), // object_dog
    ZELDA3D_BONEMAP("/actor/zelda_ds.zar", 1.00000f, 5, -1, 2, 3, 4, 1), // object_ds
    ZELDA3D_BONEMAP("/actor/zelda_ds2.zar", 1.00000f, 8, -1, 1, 2, 3, 4, 5, 6, 7), // object_ds2
    ZELDA3D_BONEMAP("/actor/zelda_dy_obj.zar", 1.00898f, 27, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26), // object_dy_obj
    ZELDA3D_BONEMAP("/actor/zelda_efc_tw.zar", 0.00000f, 3, -1, 1, 0), // object_efc_tw
    ZELDA3D_BONEMAP("/actor/zelda_ei.zar", 1.00000f, 14, -1, 0, 1, 3, 4, 5, 6, 9, 10, 12, 14, 15, 17, -1), // object_ei
    ZELDA3D_BONEMAP("/actor/zelda_fr.zar", 1.00000f, 20, -1, 19, 20, 21, 22, 15, 16, 17, 18, 1, 9, 10, 11, 12, 13, 14, 2, 7, 6, 5), // object_fr
    ZELDA3D_BONEMAP("/actor/zelda_fu.zar", 1.08496f, 14, -1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14), // object_fu
    ZELDA3D_BONEMAP("/actor/zelda_fw.zar", 1.00000f, 20, -1, 10, 13, 14, 15, 16, 17, 18, 11, 21, 22, 23, 24, 25, 12, 1, 2, 3, 6, 7), // object_fw
    ZELDA3D_BONEMAP("/actor/zelda_ge1.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_ge1
    ZELDA3D_BONEMAP("/actor/zelda_gla.zar", 1.00000f, 17, -1, 14, 15, 16, 17, 18, 19, 20, 1, 2, 3, 6, 7, 8, 11, 12, 13), // object_gla
    ZELDA3D_BONEMAP("/actor/zelda_gnd.zar", 1.00000f, 18, -1, 1, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15, 17, 19, 20, 21, 23, 24), // object_gnd
    ZELDA3D_BONEMAP("/actor/zelda_gndd.zar", 1.00000f, 22, -1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 13, 9, 12, 15, 16, 17, 18, 19, 20, 21, 22), // object_gndd
    ZELDA3D_BONEMAP("/actor/zelda_gr.zar", 1.00000f, 16, -1, 10, 11, 12, 13, 14, 15, 1, 2, 3, 4, 5, 6, 7, 8, 9), // object_gr
    ZELDA3D_BONEMAP("/actor/zelda_hata.zar", 1.00000f, 20, -1, 1, 11, 12, 19, 13, 18, 14, 17, 15, 16, 2, 3, 10, 4, 9, 5, 8, 6, 7), // object_hata
    ZELDA3D_BONEMAP("/actor/zelda_hintnuts.zar", 1.00000f, 9, -1, 1, 2, 3, 4, 5, 6, 7, 8), // object_hintnuts
    ZELDA3D_BONEMAP("/actor/zelda_jj.zar", 1.00000f, 12, -1, 1, 9, 10, 12, 4, 6, 8, 14, 16, 18, 19), // object_jj
    ZELDA3D_BONEMAP("/actor/zelda_ka.zar", 1.00000f, 8, -1, 1, 2, 3, 11, 12, 17, 18), // object_ka
    ZELDA3D_BONEMAP("/actor/zelda_mb.zar", 0.79419f, 16, -1, 1, 2, 6, 7, 9, 11, 12, 14, 16, 17, 20, 21, 22, 25, 26), // object_mb
    ZELDA3D_BONEMAP("/actor/zelda_mm.zar", 1.00000f, 15, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), // object_mm
    ZELDA3D_BONEMAP("/actor/zelda_mu.zar", 1.00000f, 8, -1, 1, 2, 3, 7, 8, 9, 14), // object_mu
    ZELDA3D_BONEMAP("/actor/zelda_os.zar", 1.00000f, 8, -1, 1, 2, 3, 4, 5, 6, 7), // object_os
    ZELDA3D_BONEMAP("/actor/zelda_owl.zar", 1.00000f, 20, -1, 1, 2, 3, 4, 13, 14, 15, 16, 17, 18, 19, 9, 10, 11, 12, 5, 6, 7, 8), // object_owl
    ZELDA3D_BONEMAP("/actor/zelda_ps.zar", 1.00000f, 10, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9), // object_ps
    ZELDA3D_BONEMAP("/actor/zelda_rd.zar", 1.00000f, 17, -1, 11, 13, 14, 16, 17, 18, 20, 21, 22, 1, 3, 4, 6, 8, 9, 24), // object_rd
    ZELDA3D_BONEMAP("/actor/zelda_rs.zar", 1.00000f, 8, -1, 1, 2, 3, 4, 5, 6, 7), // object_rs
    ZELDA3D_BONEMAP("/actor/zelda_sd.zar", 1.00000f, 16, -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), // object_sd
    ZELDA3D_BONEMAP("/actor/zelda_shopnuts.zar", 1.00000f, 15, -1, 9, 10, 11, 12, 13, 14, 7, 1, 2, 3, 4, 5, 6, 8), // object_shopnuts
    ZELDA3D_BONEMAP("/actor/zelda_skj.zar", 1.00000f, 18, -1, 8, 12, 13, 14, 15, 16, 17, 9, 10, 11, 1, 2, 3, 4, 5, 6, 7), // object_skj
    ZELDA3D_BONEMAP("/actor/zelda_st.zar", 1.00000f, 11, -1, 1, 20, 5, 11, 8, 23, 17, 14, 26, 2), // object_st
    ZELDA3D_BONEMAP("/actor/zelda_tr.zar", 1.00000f, 26, -1, 7, 8, 11, 12, 13, 9, 10, 14, 17, 18, 15, 16, 19, 20, 21, 22, 24, 25, 23, 1, 2, 3, 4, 5, 6), // object_tr
    ZELDA3D_BONEMAP("/actor/zelda_ts.zar", 1.00000f, 9, -1, 1, 2, 3, 4, 5, 6, 7, 8), // object_ts
    ZELDA3D_BONEMAP("/actor/zelda_vali.zar", 1.00000f, 11, -1, 8, 10, 11, 12, 2, 17, 19, 20, 21, 5), // object_vali
    ZELDA3D_BONEMAP("/actor/zelda_vm.zar", 1.00000f, 6, -1, 0, 1, 4, 7, 9), // object_vm
    ZELDA3D_BONEMAP("/actor/zelda_warp1.zar", 1.00001f, 28, -1, 1, 0, 2, 3, 4, 3, 5, 6, 7, 6, 8, 9, 10, 9, 11, 12, 13, 12, 14, 15, 16, 15, 17, 18, 19, 18, 20), // object_warp1
    ZELDA3D_BONEMAP("/actor/zelda_wf.zar", 1.00000f, 21, -1, 1, 2, 3, 4, 7, 8, 9, 10, 11, 15, 16, 17, 12, 13, 14, 18, 19, 20, 5, 6), // object_wf
    ZELDA3D_BONEMAP("/actor/zelda_zf.zar", 0.99451f, 20, -1, 33, 35, 36, 40, 42, 43, 2, 10, 12, 13, 17, 19, 20, 3, 4, 5, 25, 27, 30), // object_zf
};
