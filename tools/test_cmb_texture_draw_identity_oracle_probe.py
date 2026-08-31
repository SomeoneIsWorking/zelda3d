#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import cmb_texture_draw_identity_oracle_probe as probe


class FakeCache:
    key = "fake"

    def __init__(self, result: dict[str, object] | None):
        self.results: dict[str, object] = {}
        if result is not None:
            self.results["cmb-texture-draw-identity"] = result
        self.puts: list[tuple[str, int, dict[str, object], object]] = []

    def get_probe(self, name: str, frame: int, args: dict[str, object]) -> object | None:
        del frame, args
        return self.results.get(name)

    def put_probe(
        self, name: str, frame: int, args: dict[str, object], data: object
    ) -> None:
        self.puts.append((name, frame, args, data))


class CmbTextureDrawIdentityOracleProbeTests(unittest.TestCase):
    def test_memory_reader_accepts_exact_single_line_dump_response(self) -> None:
        harness = mock.Mock()
        destination = Path(self.enterContext(tempfile.TemporaryDirectory())) / "guest.bin"
        destination.write_bytes(b"\x0a\x0b")
        harness.send_multiline.return_value = ["ok dumpphys 0x00001234..0x00001236 (2 bytes) -> guest.bin"]
        self.assertEqual(probe._read_guest_memory(harness, 0x1234, 2, destination), b"\x0a\x0b")
        harness.send_multiline.assert_called_once_with(f"dumpphys 0x00001234 2 {destination}")

    def test_memory_reader_rejects_short_payload(self) -> None:
        harness = mock.Mock()
        destination = Path(self.enterContext(tempfile.TemporaryDirectory())) / "guest.bin"
        destination.write_bytes(b"\x0a")
        harness.send_multiline.return_value = ["ok dumpphys 0x00001234..0x00001235 (1 bytes) -> guest.bin"]
        with self.assertRaisesRegex(RuntimeError, "returned 1 bytes, expected 2"):
            probe._read_guest_memory(harness, 0x1234, 2, destination)

    def test_cache_hit_never_enters_live_capture(self) -> None:
        cached = {"matches": [{"draw": 7}]}
        cache = FakeCache(cached)
        with mock.patch.object(
            probe, "_capture_live", side_effect=AssertionError("oracle was launched")
        ):
            result, hit = probe.capture_probe(cache)
        self.assertTrue(hit)
        self.assertIs(result, cached)
        self.assertEqual(cache.puts, [])

    def test_complete_observation_failure_is_cached_and_not_retried(self) -> None:
        cache = FakeCache(None)
        with mock.patch.object(
            probe,
            "_capture_live",
            side_effect=probe.OracleObservationFailure("no source match"),
        ) as live:
            with self.assertRaisesRegex(RuntimeError, "no source match"):
                probe.capture_probe(cache)
            self.assertEqual(cache.puts[0][0], "cmb-texture-draw-identity-failure")
            cache.results[cache.puts[0][0]] = cache.puts[0][3]
            with self.assertRaisesRegex(RuntimeError, "cached oracle failure"):
                probe.capture_probe(cache)
        live.assert_called_once()

    def test_bootstrap_failure_is_not_cached(self) -> None:
        cache = FakeCache(None)
        with mock.patch.object(
            probe, "_capture_live", side_effect=RuntimeError("harness closed stdout unexpectedly")
        ):
            with self.assertRaisesRegex(RuntimeError, "harness closed stdout unexpectedly"):
                probe.capture_probe(cache)
        self.assertEqual(cache.puts, [])

    def test_source_mode_is_part_of_the_cache_identity(self) -> None:
        enabled = probe.probe_args("/actor/test.zar", "enabled-fragment-primary", 0xEE, 0x6000, 180)
        any_source = probe.probe_args("/actor/test.zar", "any", 0xEE, 0x6000, 180)
        self.assertNotEqual(enabled, any_source)


if __name__ == "__main__":
    unittest.main()
