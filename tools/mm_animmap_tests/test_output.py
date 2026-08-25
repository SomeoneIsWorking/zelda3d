"""Tests for MM animation-map code and report emission."""

from __future__ import annotations

import re
import unittest

from mm_animmap_c_tables import emit_default_inc, emit_inc
from mm_animmap_coverage import classify_coverage
from mm_animmap_matching import Match
from mm_animmap_report import build_report
from mm_animmap_types import ActorResult


class OutputTests(unittest.TestCase):
    def setUp(self) -> None:
        self.results = [
            ActorResult(
                "object_dog",
                "zelda2_dog",
                ("dog_walk", "dog_wait"),
                (
                    Match(
                        "objects/object_dog/gDogWalkAnim",
                        "dog_walk",
                        1.0,
                        "exact token match",
                    ),
                ),
            )
        ]
        self.meta = {
            "rom": "MM3D",
            "actor_gars_in_rom": 1,
            "object_dirs_total": 1,
            "objects_with_anims": 1,
            "min_confidence": 0.7,
        }

    def test_code_emission_contains_resolved_mapping_and_idle(self) -> None:
        include = emit_inc(self.results, self.meta)
        default_include, unknown = emit_default_inc(self.results)

        self.assertIn('"objects/object_dog/gDogWalkAnim", "dog_walk"', include)
        self.assertIn('{ "zelda2_dog", "dog_wait" }', default_include)
        self.assertEqual(unknown, [])

    def test_report_counts_resolved_symbol(self) -> None:
        markdown, report = build_report(self.results, self.meta)

        self.assertEqual(report["meta"]["symbols_total"], 1)
        self.assertEqual(report["meta"]["symbols_matched"], 1)
        self.assertIn("## Per actor", markdown)

    def test_annotated_absent_is_full_in_json_and_markdown(self) -> None:
        result = ActorResult(
            "object_ka",
            "zelda2_ka",
            ("ka_wait",),
            (
                Match("objects/object_ka/gKaWaitAnim", "ka_wait", 1.0, "exact"),
                Match(
                    "objects/object_ka/gKaUnusedAnim",
                    None,
                    0.0,
                    "XML annotates this animation as absent from MM3D",
                ),
            ),
        )

        coverage = classify_coverage([result])
        markdown, report = build_report([result], self.meta)

        self.assertEqual(coverage.full, ("object_ka",))
        self.assertEqual(report["ungating_coverage"]["full"], ["object_ka"])
        self.assertEqual(report["ungating_coverage"]["partial"], [])
        full_count = re.search(r"\| FULL \| (\d+) \|", markdown)
        partial_count = re.search(r"\| PARTIAL \| (\d+) \|", markdown)
        self.assertIsNotNone(full_count)
        self.assertIsNotNone(partial_count)
        self.assertEqual(full_count.group(1), "1")
        self.assertEqual(partial_count.group(1), "0")


if __name__ == "__main__":
    unittest.main()
