#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

import cmb_fragment_lighting_oracle_probe as probe


class FakeCache:
    key = "fake"

    def __init__(self, result: dict[str, object] | None):
        self.results = {}
        if result is not None:
            self.results["cmb-fragment-lighting-state"] = result
        self.puts: list[tuple[str, int, dict[str, object], object]] = []
        self.artifacts: list[tuple[str, Path, str | None]] = []

    def get_probe(
        self, name: str, frame: int, args: dict[str, object]
    ) -> dict[str, object] | None:
        del frame, args
        return self.results.get(name)

    def put_probe(
        self, name: str, frame: int, args: dict[str, object], data: object
    ) -> None:
        self.puts.append((name, frame, args, data))

    def put_artifact(
        self, name: str, args: dict[str, object], source: Path, suffix: str | None = None
    ) -> Path:
        del args
        self.artifacts.append((name, source, suffix))
        return Path(f"/cache/{name}{suffix or ''}")


class FragmentLightingOracleProbeTests(unittest.TestCase):
    def test_selects_lowest_fragment_lit_draw(self) -> None:
        draw, line = probe.choose_enabled_lighting_draw(
            [
                "draw n=8 idx=1 hasCol=0 vLit=1 fLit=0 picaLit=1 tex0=1234",
                "draw n=2 idx=1 hasCol=0 vLit=0 fLit=1 picaLit=0 tex0=5678",
                "draw n=5 idx=1 hasCol=0 vLit=0 fLit=0 picaLit=1 tex0=9abc",
            ]
        )
        self.assertEqual(draw, 5)
        self.assertIn("picaLit=1", line)

    def test_rejects_uniform_log_without_enabled_draw(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "scanned 1 draws and matched 0"):
            probe.choose_enabled_lighting_draw(
                ["draw n=0 idx=1 hasCol=0 vLit=1 fLit=1 picaLit=0"]
            )

    def test_rejects_empty_uniform_log_as_invalid_capture(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "scanned 0 draws"):
            probe.choose_enabled_lighting_draw([])

    def test_requires_positive_pica_logger_selftest(self) -> None:
        probe.require_lighting_log_positive(["draw n=0 picaLit=1"])
        with self.assertRaisesRegex(RuntimeError, "scanned 1 draws and logged no"):
            probe.require_lighting_log_positive(["draw n=0 picaLit=0"])

    def test_pause_fixture_uses_short_start_tap(self) -> None:
        harness = mock.Mock()
        with mock.patch.object(probe, "tap") as tap:
            probe.prepare_fixture(harness, "kokiri-save-overlay")
        tap.assert_called_once_with(harness, probe.BTN_START, hold=4, release=60)

        with self.assertRaisesRegex(ValueError, "unsupported"):
            probe.prepare_fixture(harness, "unknown")

    def test_gameplay_fixture_preserves_the_loaded_scene(self) -> None:
        harness = mock.Mock()
        with mock.patch.object(probe, "tap") as tap:
            probe.prepare_fixture(harness, "kokiri-gameplay")
            probe.prepare_fixture(harness, "gameplay")
        tap.assert_not_called()

    def test_finds_exact_draw_line_without_reparsing_callers(self) -> None:
        lines = ["draw n=4 picaLit=1", "draw n=9 picaLit=0"]
        self.assertEqual(probe._draw_line(lines, 9), lines[1])
        self.assertIsNone(probe._draw_line(lines, 8))

    def test_parses_pc_hits_with_explicit_negative_count(self) -> None:
        self.assertEqual(probe._parse_pc_hits(["ok pchits 0", "ok end"]), (0, []))
        with self.assertRaisesRegex(RuntimeError, "count mismatch"):
            probe._parse_pc_hits(["ok pchits 1", "ok end"])

    def test_caches_raw_lighting_before_rejecting_empty_luts(self) -> None:
        cache = FakeCache(None)
        path = Path("lighting.json")
        with (
            mock.patch.object(Path, "read_text", return_value='{"draw": 4, "disable": 0, "luts": []}'),
            self.assertRaisesRegex(RuntimeError, "no activated LUT"),
        ):
            probe.cache_lighting_state(cache, {}, path, 4)

        self.assertEqual(cache.artifacts, [("cmb-fragment-lighting-state", path, ".json")])

    def test_cache_hit_never_enters_live_capture(self) -> None:
        cached = {"draw": 17, "lighting": {"disable": 0}}
        cache = FakeCache(cached)
        with mock.patch.object(
            probe, "_capture_live", side_effect=AssertionError("oracle was launched")
        ):
            result, hit = probe.capture_probe(cache)

        self.assertTrue(hit)
        self.assertIs(result, cached)
        self.assertEqual(cache.puts, [])

    def test_cache_miss_persists_structured_probe_once(self) -> None:
        cache = FakeCache(None)
        captured = {"draw": 5, "lighting": {"disable": 0}}
        with mock.patch.object(probe, "_capture_live", return_value=captured):
            result, hit = probe.capture_probe(cache, settle_frames=12)

        self.assertFalse(hit)
        self.assertIs(result, captured)
        self.assertEqual(len(cache.puts), 1)
        self.assertEqual(cache.puts[0][0], "cmb-fragment-lighting-state")
        self.assertEqual(cache.puts[0][1], probe.capture_frame(12))

    def test_cache_miss_persists_os_failure(self) -> None:
        cache = FakeCache(None)
        with (
            mock.patch.object(probe, "_capture_live", side_effect=OSError("harness closed")),
            self.assertRaisesRegex(OSError, "harness closed"),
        ):
            probe.capture_probe(cache, settle_frames=12)

        self.assertEqual(len(cache.puts), 1)
        self.assertEqual(cache.puts[0][0], "cmb-fragment-lighting-failure")
        self.assertEqual(cache.puts[0][3], {"capture_version": probe.CAPTURE_VERSION, "error": "harness closed"})

    def test_live_failure_is_cached_and_not_retried(self) -> None:
        cache = FakeCache(None)
        with mock.patch.object(
            probe, "_capture_live", side_effect=RuntimeError("no enabled draw")
        ) as live:
            with self.assertRaisesRegex(RuntimeError, "no enabled draw"):
                probe.capture_probe(cache)

            self.assertEqual(cache.puts[0][0], "cmb-fragment-lighting-failure")
            cache.results[cache.puts[0][0]] = cache.puts[0][3]
            with self.assertRaisesRegex(RuntimeError, "cached oracle failure"):
                probe.capture_probe(cache)

        live.assert_called_once()


if __name__ == "__main__":
    unittest.main()
