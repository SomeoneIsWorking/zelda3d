"""Tests for MM animation token matching and annotation policy."""

from __future__ import annotations

import unittest

from mm_animmap_matching import match_anims


class MatchingTests(unittest.TestCase):
    def test_token_synonym_match(self) -> None:
        matches = match_anims(
            ["objects/object_dog/gDogWalkAnim"],
            ["dog_aruki"],
            "object_dog",
        )

        self.assertEqual(matches[0].clip, "dog_aruki")

    def test_authoritative_absent_annotation_suppresses_guess(self) -> None:
        symbol = "objects/object_dog/gDogWalkAnim"

        matches = match_anims(
            [symbol],
            ["dog_walk"],
            "object_dog",
            orig={"gDogWalkAnim": None},
        )

        self.assertIsNone(matches[0].clip)
        self.assertIn("absent from MM3D", matches[0].why)


if __name__ == "__main__":
    unittest.main()
