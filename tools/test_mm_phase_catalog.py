#!/usr/bin/env python3
"""Catalog invariants for deterministic MM phase-tour scenes."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_phase_catalog import SCENES, SceneSelectionError, select_scenes


class SceneCatalogTests(unittest.TestCase):
    def test_catalog_is_unique_and_entrances_are_spawn_zero(self) -> None:
        self.assertGreater(len(SCENES), 3)
        self.assertEqual(len({scene.name for scene in SCENES}), len(SCENES))
        self.assertEqual(len({scene.entrance for scene in SCENES}), len(SCENES))
        for scene in SCENES:
            self.assertEqual(scene.entrance & 0x1F0, 0)

    def test_scene_selection_is_ordered_and_rejects_unknown_names(self) -> None:
        selected = select_scenes("woodfall,south_clock_town,woodfall")
        self.assertEqual(
            [scene.name for scene in selected], ["woodfall", "south_clock_town"]
        )
        with self.assertRaisesRegex(SceneSelectionError, "unknown scene"):
            select_scenes("clock_town_typo")


if __name__ == "__main__":
    unittest.main()
