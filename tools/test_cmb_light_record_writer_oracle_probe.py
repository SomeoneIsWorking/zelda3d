from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from cmb_light_record_writer_oracle_probe import (
    CMB_RENDERER_VTABLE,
    FINALIZER_FUNCTION,
    LIGHT_RECORD_BYTES,
    capture_probe,
    parse_pc_hits,
    parse_watch_hits,
    probe_args,
    resolve_light_records,
)


class FakeHarness:
    def __init__(self, words: dict[int, int]) -> None:
        self.words = words
        self.commands: list[str] = []

    def send(self, command: str) -> str:
        self.commands.append(command)
        _, address = command.split()
        return f"ok 0x{self.words[int(address, 0)]:08x}"


class FakeCache:
    key = "test"

    def __init__(self, cached: dict | None = None) -> None:
        self.cached = cached
        self.written: dict | None = None

    def get_probe(self, _name: str, _frame: int, _args: dict) -> dict | None:
        return self.cached

    def put_probe(self, _name: str, _frame: int, _args: dict, result: dict) -> Path:
        self.written = result
        return Path("cached.json")

    def put_artifact(self, _name: str, _args: dict, _source: Path, suffix: str | None = None) -> Path:
        return Path(f"artifact{suffix or ''}")


def pc_response(r0: int) -> list[str]:
    return [
        "ok pchits 1",
        f"  pc=0x{FINALIZER_FUNCTION:08x} lr=0x00123456 ticks=17 r0=0x{r0:08x} "
        "r1=0x00000000 r2=0x00000000 r3=0x00000000 sp=0x0ffff000",
        "ok end",
    ]


class CmbLightRecordWriterTests(unittest.TestCase):
    def test_resolves_three_record_range_from_finalizer_receiver(self) -> None:
        renderer = 0x081D3AA0
        records = 0x081D434C
        harness = FakeHarness(
            {renderer: CMB_RENDERER_VTABLE, renderer + 0x10: records}
        )
        self.assertEqual(
            resolve_light_records(harness, parse_pc_hits(pc_response(renderer))),
            (renderer, records),
        )
        self.assertEqual(
            harness.commands,
            [f"r32 0x{renderer:08x}", f"r32 0x{renderer + 0x10:08x}"],
        )
        self.assertEqual(LIGHT_RECORD_BYTES, 0x120)

    def test_rejects_non_cmb_finalizer_receiver(self) -> None:
        renderer = 0x081D3AA0
        harness = FakeHarness({renderer: 0x12345678})
        with self.assertRaisesRegex(RuntimeError, "CmbRenderer vtable"):
            resolve_light_records(harness, parse_pc_hits(pc_response(renderer)))

    def test_rejects_watch_cap_to_avoid_truncated_writer_evidence(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "128-record cap"):
            parse_watch_hits(["ok hits 128", "ok end"])

    def test_cache_key_carries_the_record_layout(self) -> None:
        args = probe_args(0x30D, 0x6000, 180)
        self.assertEqual(args["finalizer_function"], "0x003fa34c")
        self.assertEqual(args["light_record_stride"], "0x60")
        self.assertEqual(args["light_record_count"], 3)

    def test_cache_hit_never_requires_a_current_gameplay_state(self) -> None:
        cached = {"light_records": "0x081d434c"}
        cache = FakeCache(cached)
        with TemporaryDirectory() as directory:
            result, hit = capture_probe(cache, gameplay_state=Path(directory) / "missing.state")
        self.assertTrue(hit)
        self.assertEqual(result, cached)
        self.assertIsNone(cache.written)


if __name__ == "__main__":
    unittest.main()
