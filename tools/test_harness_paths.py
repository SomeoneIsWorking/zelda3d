"""Contract-keyed checkpoint path tests."""

from __future__ import annotations

import unittest

from harness_paths import GAMEPLAY_STATE, TITLE_STATE, TITLE_STATE_METADATA, azahar_render_contract_marker


class HarnessPathTests(unittest.TestCase):
    def test_title_and_gameplay_checkpoints_share_the_active_contract(self) -> None:
        marker = azahar_render_contract_marker()

        self.assertEqual(TITLE_STATE.name, f"title_settled.{marker}.state")
        self.assertEqual(TITLE_STATE_METADATA.name, f"title_settled.{marker}.json")
        self.assertEqual(GAMEPLAY_STATE.name, f"gameplay_settled.{marker}.state")
        self.assertNotEqual(TITLE_STATE, GAMEPLAY_STATE)


if __name__ == "__main__":
    unittest.main()
