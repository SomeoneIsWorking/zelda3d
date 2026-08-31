#!/usr/bin/env python3
from __future__ import annotations

import unittest
from unittest import mock

import cmb_material_state_oracle_probe as probe


class FakeCache:
    key = "fake"

    def __init__(self, result: dict[str, object] | None = None):
        self.results = {} if result is None else {"cmb-material-state": result}
        self.puts: list[tuple[str, object]] = []

    def get_probe(self, name, frame, args):
        del frame, args
        return self.results.get(name)

    def put_probe(self, name, frame, args, data):
        del frame, args
        self.puts.append((name, data))


class CmbMaterialStateOracleProbeTests(unittest.TestCase):
    def test_parses_nonempty_material_state_trace(self) -> None:
        records = probe.parse_pc_hits(
            [
                "ok pchits 1",
                "  pc=0x003fbba8 lr=0x00123456 ticks=3 r0=0x1 r1=0x2 r2=0x3 r3=0x4 sp=0x5",
                "ok end",
            ]
        )
        self.assertEqual(len(records), 1)

    def test_rejects_empty_material_state_trace(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "was not reached"):
            probe.parse_pc_hits(["ok pchits 0", "ok end"])

    def test_labels_empty_indirect_dispatch_trace(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "indirect CMB material dispatch"):
            probe.parse_pc_hits(["ok pchits 0", "ok end"], "indirect CMB material dispatch")

    def test_extracts_virtual_dispatch_context_register(self) -> None:
        record = (
            "pc=0x003fcc70 lr=0x00123456 ticks=3 r0=0x08001234 r1=0x2 "
            "r2=0x3 r3=0x4 sp=0x5"
        )
        self.assertEqual(probe.register_value(record, 0), 0x08001234)

    def test_cache_identity_includes_selected_dispatch_target(self) -> None:
        direct = probe.probe_args(0xEE, 0x6000, 180, "material-state")
        virtual = probe.probe_args(0xEE, 0x6000, 180, "virtual-dispatch")
        self.assertNotEqual(direct, virtual)
        self.assertEqual(virtual["target_function"], probe.VIRTUAL_DISPATCH_FUNCTION)

    def test_cache_hit_never_launches_oracle(self) -> None:
        cached = {"pc_records": ["cached"]}
        with mock.patch.object(probe, "capture_live", side_effect=AssertionError):
            result, hit = probe.capture_probe(FakeCache(cached))
        self.assertTrue(hit)
        self.assertIs(result, cached)

    def test_failure_is_cached_and_not_retried(self) -> None:
        cache = FakeCache()
        with mock.patch.object(
            probe, "capture_live", side_effect=RuntimeError("no material state")
        ) as live:
            with self.assertRaisesRegex(RuntimeError, "no material state"):
                probe.capture_probe(cache)
            cache.results[cache.puts[0][0]] = cache.puts[0][1]
            with self.assertRaisesRegex(RuntimeError, "cached oracle failure"):
                probe.capture_probe(cache)
        live.assert_called_once()


if __name__ == "__main__":
    unittest.main()
