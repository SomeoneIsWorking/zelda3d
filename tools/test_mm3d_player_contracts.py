#!/usr/bin/env python3
"""Ownership regressions for MM player controls and run lifecycle contracts."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MM = REPO / "2ship" / "2s2h" / "zelda3d"
PLAYER_OVERLAY = REPO / "2ship" / "src" / "overlays" / "actors" / "ovl_player_actor"


class MmPlayerContractTests(unittest.TestCase):
    def test_force_owner_contains_mutations_not_observation(self) -> None:
        source = (MM / "mm3d_player_force.c").read_text()
        self.assertIn("func_80836B3C(play, player, 0.0f);", source)
        self.assertIn("Player_UseItem(play, player, maskItem);", source)
        for observation in (
            "printf(",
            "snprintf(",
            "Zelda3D_PlayerActionName",
            '"Player_Action_',
        ):
            self.assertNotIn(
                observation,
                source,
                f"force owner absorbed diagnostic output: {observation}",
            )

    def test_action_identity_observation_has_one_focused_owner(self) -> None:
        diagnostic = (MM / "mm3d_link_state.c").read_text()
        self.assertIn("player->actionFunc == Player_Action_86", diagnostic)
        self.assertIn("player->actionFunc == Player_Action_96", diagnostic)

        definition = re.compile(
            r"^const char\* Zelda3D_PlayerActionName\(", re.MULTILINE
        )
        owners = [
            path for path in MM.rglob("*.c") if definition.search(path.read_text())
        ]
        self.assertEqual(owners, [MM / "mm3d_link_state.c"])

    def test_overlay_exports_have_one_contract_header(self) -> None:
        contract = (PLAYER_OVERLAY / "z_player_overlay.h").read_text()
        for symbol in (
            "func_80836B3C",
            "Player_UseItem",
            "Player_Action_86",
            "Player_Action_96",
        ):
            self.assertEqual(contract.count(f"{symbol}("), 1, symbol)

        overlay = (PLAYER_OVERLAY / "z_player.c").read_text()
        self.assertIn('#include "z_player_overlay.h"', overlay)
        self.assertNotRegex(
            overlay,
            r"^void Player_UseItem\([^\n]+\);$",
            "overlay restored a local declaration",
        )

        easy_mask = (
            REPO / "2ship" / "2s2h" / "Enhancements" / "Masks" / "EasyMaskEquip.cpp"
        ).read_text()
        self.assertIn(
            '#include "overlays/actors/ovl_player_actor/z_player_overlay.h"', easy_mask
        )
        self.assertNotRegex(
            easy_mask,
            r"^void Player_UseItem\([^\n]+\);$",
            "enhancement owns a duplicate declaration",
        )

    def test_run_lifecycle_uses_owner_headers(self) -> None:
        lifecycle = (MM / "mm3d_core_lifecycle.c").read_text()
        required_headers = (
            '"mm3d_model_lifecycle.h"',
            '"2s2h/BenPortLifecycle.h"',
            '"2s2h/zelda3d/repl/mm3d_repl.h"',
            '"src/code/cutscene_manager_lifecycle.h"',
            '"src/code/graph_lifecycle.h"',
            '"object/ObjectExtension.h"',
        )
        for header in required_headers:
            self.assertIn(f"#include {header}", lifecycle)

        reset_symbols = (
            "Zelda3D_FreePreviousOTRGlobals",
            "Graph_ResetRunState",
            "Zelda3D_MmReplResetRunState",
            "Zelda3D_MM_ModelResetRunState",
            "ObjectExtension_ResetRunState",
            "CutsceneManager_ResetRunState",
        )
        for symbol in reset_symbols:
            declaration = re.compile(
                rf"^(?:void|int) {symbol}\([^\n]*\);", re.MULTILINE
            )
            self.assertNotRegex(
                lifecycle,
                declaration,
                f"lifecycle restored a local declaration for {symbol}",
            )

    def test_player_model_selection_reaches_lod_skeleton_seam(self) -> None:
        player = (MM / "mm3d_player.c").read_text()
        self.assertIn("Zelda3D_MM_LookupPlayerModel(player->transformation", player)
        self.assertIn("meshMask |= Zelda3D_MM_PlayerSheathMeshMask(", player)
        self.assertIn(
            "Zelda3D_MM_PlayerRightHandMeshMask(player, &rightHandMask)", player
        )
        self.assertIn("Zelda3D_MM_PlayerLeftHandDrawState(", player)
        self.assertLess(
            player.index("Zelda3D_MM_PlayerRightHandMeshMask"),
            player.index("Zelda3D_GL_SetMidMask"),
        )
        self.assertIn("meshMask |= rightHandMask;", player)
        self.assertIn("meshMask |= leftHandMask;", player)
        for typed_input in (
            "player->sheathType",
            "player->currentShield",
            "player->currentMask",
        ):
            self.assertIn(typed_input, player)
        self.assertIn("GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD)", player)
        self.assertIn("Zelda3D_GL_SetMidMask(modelId, meshMask);", player)
        self.assertIn(
            "Zelda3D_MM_SetPending(actor, modelId, worldScale, groundOffset);", player
        )

        skelanime = (REPO / "2ship" / "src" / "code" / "z_skelanime.c").read_text()
        lod_walker = re.search(
            r"void SkelAnime_DrawFlexLod\(.*?\n}\n\n",
            skelanime,
            re.DOTALL,
        )
        self.assertIsNotNone(lod_walker)
        self.assertIn(
            "Zelda3D_MM_InterceptSkelAnime(play, actor, skeleton, jointTable)",
            lod_walker.group(),
        )
        self.assertIn("gZelda3dMmColliderPass = 1;", lod_walker.group())
        self.assertIn("play->state.gfxCtx->polyOpa.p = opaP;", lod_walker.group())
        self.assertIn("play->state.gfxCtx->polyXlu.p = xluP;", lod_walker.group())

    def test_player_animation_uses_exact_shared_archive_members(self) -> None:
        adapter = (MM / "mm3d_player_model.cpp").read_text()
        self.assertIn("RegisterPlayerAnimationModel(resolved, form);", adapter)

        animation = (MM / "mm3d_animation.cpp").read_text()
        self.assertIn("ResolvePlayerAnimationPath(modelId, animationOtr)", animation)
        self.assertIn("playerModel ? file.path == key : file.name == key", animation)

        owner = (MM / "mm3d_player_animation.cpp").read_text()
        self.assertIn('"/actors/zelda2_link_new.gar.lzs"', owner)
        self.assertIn("g_playerAnimationMembers.contains(candidate)", owner)
        self.assertNotIn("firstWithSuffix", owner)

    def test_mm_draw_snapshots_pose_and_material_state_before_deferred_submit(
        self,
    ) -> None:
        draw = (MM / "mm3d_draw.c").read_text()
        emitter = re.search(
            r"static void Zelda3D_EmitModelDraw\(.*?\n}\n",
            draw,
            re.DOTALL,
        )
        self.assertIsNotNone(emitter)
        body = emitter.group()
        self.assertLess(
            body.index("Zelda3D_GL_EmitPose(modelId);"),
            body.index("gSPZelda3DDraw("),
        )

    def test_player_bottle_material_uses_the_deferred_override_seam(self) -> None:
        player = (MM / "mm3d_player.c").read_text()
        self.assertIn("Zelda3D_MM_PlayerLeftHandDrawState(", player)
        self.assertIn("Zelda3D_GL_SetMatConstOverride(", player)
        self.assertLess(
            player.index("Zelda3D_GL_SetMatConstOverride("),
            player.index("Zelda3D_GL_SetMidMask("),
        )

    def test_focused_owners_stay_below_source_ceiling(self) -> None:
        owners = (
            MM / "mm3d_player_force.c",
            MM / "mm3d_player_force.h",
            MM / "mm3d_link_state.c",
            MM / "mm3d_link_state.h",
            MM / "mm3d_core_lifecycle.c",
            MM / "mm3d_player.c",
            MM / "mm3d_player.h",
            MM / "mm3d_player_model.cpp",
            MM / "mm3d_player_model.h",
            MM / "mm3d_player_model_policy.cpp",
            MM / "mm3d_player_model_policy.h",
            MM / "mm3d_player_sheath.cpp",
            MM / "mm3d_player_sheath.h",
            MM / "mm3d_player_sheath_policy.cpp",
            MM / "mm3d_player_sheath_policy.h",
            MM / "mm3d_player_right_hand.cpp",
            MM / "mm3d_player_right_hand.h",
            MM / "mm3d_player_right_hand_policy.cpp",
            MM / "mm3d_player_right_hand_policy.h",
            MM / "mm3d_player_left_hand.cpp",
            MM / "mm3d_player_left_hand.h",
            MM / "mm3d_player_left_hand_policy.cpp",
            MM / "mm3d_player_left_hand_policy.h",
            MM / "mm3d_player_bottle_material_policy.cpp",
            MM / "mm3d_player_bottle_material_policy.h",
            MM / "mm3d_player_animation.cpp",
            MM / "mm3d_player_animation.h",
            MM / "mm3d_animation_playhead.h",
            MM / "mm3d_player_animation_policy.cpp",
            MM / "mm3d_player_animation_policy.h",
            MM / "repl" / "mm3d_link_repl.cpp",
            MM / "repl" / "mm3d_link_repl.h",
            PLAYER_OVERLAY / "z_player_overlay.h",
        )
        for owner in owners:
            self.assertLessEqual(len(owner.read_text().splitlines()), 1200, owner)


if __name__ == "__main__":
    unittest.main()
