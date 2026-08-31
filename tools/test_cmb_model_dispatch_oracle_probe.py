#!/usr/bin/env python3
from __future__ import annotations

import unittest
from unittest import mock

import cmb_model_dispatch_oracle_probe as probe


class FakeCache:
    key = "fake"

    def __init__(self, result: dict[str, object] | None = None):
        self.results = {} if result is None else {"cmb-model-dispatch": result}
        self.puts: list[tuple[str, object]] = []

    def get_probe(self, name, frame, args):
        del frame, args
        return self.results.get(name)

    def put_probe(self, name, frame, args, data):
        del frame, args
        self.puts.append((name, data))


class CmbModelDispatchOracleProbeTests(unittest.TestCase):
    def test_parses_dispatch_register_record(self) -> None:
        records = probe.parse_pc_hits(
            [
                "ok pchits 1",
                "  pc=0x004c7ab0 lr=0x00123456 ticks=3 r0=0x1 r1=0x08001234 r2=0x3 r3=0x4 sp=0x5",
                "ok end",
            ]
        )
        self.assertEqual(len(records), 1)
        self.assertEqual(probe.REGISTER_RE.search(records[0]).group("r1"), "08001234")

    def test_rejects_empty_dispatch_trace(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "was not reached"):
            probe.parse_pc_hits(["ok pchits 0", "ok end"])

    def test_cache_hit_never_launches_oracle(self) -> None:
        cached = {"draw_method": "0x00123456"}
        with mock.patch.object(probe, "capture_live", side_effect=AssertionError):
            result, hit = probe.capture_probe(FakeCache(cached))
        self.assertTrue(hit)
        self.assertIs(result, cached)

    def test_failure_is_cached_and_not_retried(self) -> None:
        cache = FakeCache()
        with mock.patch.object(probe, "capture_live", side_effect=RuntimeError("no dispatch")) as live:
            with self.assertRaisesRegex(RuntimeError, "no dispatch"):
                probe.capture_probe(cache)
            cache.results[cache.puts[0][0]] = cache.puts[0][1]
            with self.assertRaisesRegex(RuntimeError, "cached oracle failure"):
                probe.capture_probe(cache)
        live.assert_called_once()


if __name__ == "__main__":
    unittest.main()
