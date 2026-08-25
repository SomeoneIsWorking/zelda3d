include_guard(GLOBAL)

include("${ZELDA3D_SHARED_DIR}/cmake/LegacySourceContracts.cmake")

# Keep the imported OoT decomp sources at their generated include boundary.
# Each entry declares only the authored interfaces consumed by that source.
function(soh_apply_zelda3d_legacy_source_contracts source_root)
    set(zelda3d_root "${source_root}/src/zelda3d")
    set(host_root "${source_root}/soh/host")

    function(soh_legacy_host_source_contract relative_source)
        set(contract_headers "")
        foreach(relative_header IN LISTS ARGN)
            list(APPEND contract_headers "${host_root}/${relative_header}")
        endforeach()
        zelda3d_legacy_c_source_contract(
            SOURCE "${source_root}/${relative_source}"
            HEADERS ${contract_headers})
    endfunction()

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/graph.c"
        HEADERS
            "${host_root}/core_lifecycle.h"
            "${host_root}/frame_input.h"
            "${host_root}/frame_render_bridge.h"
            "${host_root}/frame_timing.h"
            "${zelda3d_root}/core/zelda3d_runtime.h"
            "${zelda3d_root}/diagnostics/boot_fixture.h"
            "${zelda3d_root}/launcher/zelda3d_launcher.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_actor.c"
        HEADERS
            "${host_root}/viewport_dimensions.h"
            "${zelda3d_root}/behaviors/actor_behavior_bridge.h"
            "${zelda3d_root}/anim/skeleton_draw_bridge.h"
            "${zelda3d_root}/behaviors/actor/actor_draw.h"
            "${zelda3d_root}/render/terrain_alignment_render.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_camera.c"
        HEADERS
            "${zelda3d_root}/behaviors/camera/at_default.h"
            "${zelda3d_root}/behaviors/camera_behavior.h"
            "${zelda3d_root}/core/zelda3d_runtime.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_parameter.c"
        HEADERS
            "${host_root}/viewport_dimensions.h"
            "${host_root}/item_randomizer_bridge.h"
            "${zelda3d_root}/behaviors/title/title_activity.h"
            "${zelda3d_root}/hud/navi_prompt_control.h"
            "${zelda3d_root}/hud/zelda3d_hud_assets.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_play.c"
        HEADERS
            "${host_root}/frame_timing.h"
            "${host_root}/viewport_dimensions.h"
            "${host_root}/item_randomizer_bridge.h"
            "${zelda3d_root}/control/cutscene_skip_control.h"
            "${zelda3d_root}/control/frame_step_control.h"
            "${zelda3d_root}/core/zelda3d_runtime.h"
            "${zelda3d_root}/diagnostics/collectible_probe.h"
            "${zelda3d_root}/diagnostics/crate_probe.h"
            "${zelda3d_root}/diagnostics/get_item_probe.h"
            "${zelda3d_root}/diagnostics/gossip_stone_probe.h"
            "${zelda3d_root}/diagnostics/pot_probe.h"
            "${zelda3d_root}/render/celestial_render.h"
            "${zelda3d_root}/render/room_render.h"
            "${zelda3d_root}/render/sky_render.h"
            "${zelda3d_root}/repl/zelda3d_repl.h"
            "${zelda3d_root}/scene/scene_draw.h"
            "${zelda3d_root}/scene/scene_time.h"
            "${zelda3d_root}/behaviors/title/title_atmosphere.h"
            "${zelda3d_root}/behaviors/title/title_overlay.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_room.c"
        HEADERS
            "${host_root}/viewport_dimensions.h"
            "${zelda3d_root}/core/zelda3d_runtime.h"
            "${zelda3d_root}/scene/scene_draw.h"
            "${zelda3d_root}/render/room_render.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/code/z_skin.c"
        HEADERS
            "${zelda3d_root}/anim/automatic_playback.h"
            "${zelda3d_root}/anim/skeleton_draw_bridge.h"
            "${zelda3d_root}/render/actor_auto_replacement.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/overlays/actors/ovl_En_Horse/z_en_horse.c"
        HEADERS
            "${zelda3d_root}/anim/skeleton_draw_bridge.h"
            "${zelda3d_root}/behaviors/actor/en_horse.h"
            "${zelda3d_root}/behaviors/title/title_activity.h"
            "${zelda3d_root}/core/zelda3d_math.h")

    zelda3d_legacy_c_source_contract(
        SOURCE "${source_root}/src/overlays/actors/ovl_player_actor/z_player.c"
        HEADERS
            "${host_root}/controller_buttons.h"
            "${host_root}/item_randomizer_bridge.h"
            "${zelda3d_root}/behaviors/camera/at_default.h"
            "${zelda3d_root}/anim/skeleton_draw_bridge.h"
            "${zelda3d_root}/player/player_draw_bridge.h")

    soh_legacy_host_source_contract("src/code/audio_load.c" "error_dialog.h")
    soh_legacy_host_source_contract("src/code/code_800EC960.c" "controller_buttons.h")
    soh_legacy_host_source_contract("src/code/core_entry.c" "core_lifecycle.h")
    soh_legacy_host_source_contract("src/code/gfxprint.c" "text_rendering.h")
    soh_legacy_host_source_contract("src/code/main.c" "core_lifecycle.h" "audio_lifecycle.h")
    soh_legacy_host_source_contract("src/code/padmgr.c" "controller_feedback.h")
    soh_legacy_host_source_contract("src/code/z_demo.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_elf_message.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_en_item00.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_fbdemo_circle.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract("src/code/z_kankyo.c" "viewport_dimensions.h" "pixel_depth.h")
    soh_legacy_host_source_contract("src/code/z_lifemeter.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract("src/code/z_lights.c" "pixel_depth.h")
    soh_legacy_host_source_contract("src/code/z_map_exp.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract("src/code/z_map_mark.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract("src/code/z_message_PAL.c" "message_lifecycle.h" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_rcp.c" "frame_timing.h" "viewport_dimensions.h")
    soh_legacy_host_source_contract("src/code/z_scene_table.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_sram.c" "save_file.h" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract("src/code/z_ss_sram.c" "save_file.h")

    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Bg_Gate_Shutter/z_bg_gate_shutter.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Bg_Jya_Lift/z_bg_jya_lift.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Bg_Spot01_Idosoko/z_bg_spot01_idosoko.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Bg_Toki_Swd/z_bg_toki_swd.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Dodongo/z_boss_dodongo.c" "texture_cache_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.c" "texture_cache_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Ganondrof/z_boss_ganondrof.c" "texture_cache_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Goma/z_boss_goma.c" "texture_cache_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Boss_Va/z_boss_va.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Demo_Effect/z_demo_effect.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Demo_Im/z_demo_im.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Door_Ana/z_door_ana.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Bom_Bowl_Man/z_en_bom_bowl_man.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Box/z_en_box.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Dns/z_en_dns.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ds/z_en_ds.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ganon_Mant/z_en_ganon_mant.c" "texture_cache_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_GirlA/z_en_girla.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Heishi4/z_en_heishi4.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ishi/z_en_ishi.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Js/z_en_js.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ossan/z_en_ossan.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Partner/z_en_partner.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Sa/z_en_sa.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ssh/z_en_ssh.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Ta/z_en_ta.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_En_Takara_Man/z_en_takara_man.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Item_Ocarina/z_item_ocarina.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Obj_Mure2/z_obj_mure2.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract(
        "src/overlays/actors/ovl_Oceff_Storm/z_oceff_storm.c" "viewport_dimensions.h")
    soh_legacy_host_source_contract(
        "src/overlays/gamestates/ovl_file_choose/z_file_choose.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/gamestates/ovl_select/z_select.c" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/misc/ovl_kaleido_scope/z_kaleido_item.c"
        "viewport_dimensions.h" "item_randomizer_bridge.h")
    soh_legacy_host_source_contract(
        "src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c" "texture_cache_bridge.h")
endfunction()
