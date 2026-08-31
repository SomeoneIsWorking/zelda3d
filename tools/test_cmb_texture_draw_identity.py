#!/usr/bin/env python3
from __future__ import annotations

import unittest

from cmb_texture_draw_identity import (
    LoggedTexture,
    SourceTexture,
    descriptor_candidates,
    logged_texture_descriptors,
    match_guest_payloads,
)


class CmbTextureDrawIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SourceTexture(
            label="/actor/test.zar:Model/test.cmb",
            texture_name="test",
            width=32,
            height=64,
            pica_format=12,
            payload=b"abcd",
        )

    def test_parses_draw_texture_descriptor(self) -> None:
        parsed = logged_texture_descriptors(
            [
                "draw n=7 idx=1 tex0=18451200/32x64/f12 en=1",
                "not a draw",
            ]
        )
        self.assertEqual(parsed, [LoggedTexture(7, 0x18451200, 32, 64, 12)])

    def test_rejects_log_without_texture_descriptors(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "scanned 0"):
            logged_texture_descriptors(["draw n=1 no-texture=1"])

    def test_candidates_require_the_complete_descriptor(self) -> None:
        logged = [
            LoggedTexture(1, 0x1000, 32, 64, 12),
            LoggedTexture(2, 0x2000, 32, 64, 4),
            LoggedTexture(3, 0x3000, 64, 64, 12),
        ]
        self.assertEqual(descriptor_candidates(logged, [self.source]), logged[:1])

    def test_match_requires_exact_guest_payload(self) -> None:
        candidate = LoggedTexture(1, 0x1000, 32, 64, 12)
        self.assertEqual(
            match_guest_payloads([candidate], [self.source], {0x1000: b"abcd"}),
            [(candidate, self.source)],
        )
        self.assertEqual(
            match_guest_payloads([candidate], [self.source], {0x1000: b"abce"}), []
        )


if __name__ == "__main__":
    unittest.main()
