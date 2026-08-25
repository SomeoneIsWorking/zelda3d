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
        for observation in ("printf(", "snprintf(", "Zelda3D_PlayerActionName", '"Player_Action_'):
            self.assertNotIn(observation, source, f"force owner absorbed diagnostic output: {observation}")

    def test_action_identity_observation_has_one_focused_owner(self) -> None:
        diagnostic = (MM / "mm3d_link_state.c").read_text()
        self.assertIn("player->actionFunc == Player_Action_86", diagnostic)
        self.assertIn("player->actionFunc == Player_Action_96", diagnostic)

        definition = re.compile(r"^const char\* Zelda3D_PlayerActionName\(", re.MULTILINE)
        owners = [path for path in MM.rglob("*.c") if definition.search(path.read_text())]
        self.assertEqual(owners, [MM / "mm3d_link_state.c"])

    def test_overlay_exports_have_one_contract_header(self) -> None:
        contract = (PLAYER_OVERLAY / "z_player_overlay.h").read_text()
        for symbol in ("func_80836B3C", "Player_UseItem", "Player_Action_86", "Player_Action_96"):
            self.assertEqual(contract.count(f"{symbol}("), 1, symbol)

        overlay = (PLAYER_OVERLAY / "z_player.c").read_text()
        self.assertIn('#include "z_player_overlay.h"', overlay)
        self.assertNotRegex(overlay, r"^void Player_UseItem\([^\n]+\);$", "overlay restored a local declaration")

        easy_mask = (REPO / "2ship" / "2s2h" / "Enhancements" / "Masks" / "EasyMaskEquip.cpp").read_text()
        self.assertIn('#include "overlays/actors/ovl_player_actor/z_player_overlay.h"', easy_mask)
        self.assertNotRegex(easy_mask, r"^void Player_UseItem\([^\n]+\);$", "enhancement owns a duplicate declaration")

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
            declaration = re.compile(rf"^(?:void|int) {symbol}\([^\n]*\);", re.MULTILINE)
            self.assertNotRegex(lifecycle, declaration, f"lifecycle restored a local declaration for {symbol}")

    def test_focused_owners_stay_below_source_ceiling(self) -> None:
        owners = (
            MM / "mm3d_player_force.c",
            MM / "mm3d_player_force.h",
            MM / "mm3d_link_state.c",
            MM / "mm3d_link_state.h",
            MM / "mm3d_core_lifecycle.c",
            MM / "repl" / "mm3d_link_repl.c",
            MM / "repl" / "mm3d_link_repl.h",
            PLAYER_OVERLAY / "z_player_overlay.h",
        )
        for owner in owners:
            self.assertLessEqual(len(owner.read_text().splitlines()), 1200, owner)


if __name__ == "__main__":
    unittest.main()
