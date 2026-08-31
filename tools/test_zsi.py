#!/usr/bin/env python3
from __future__ import annotations

import struct
import unittest

from zsi import Actor, Zsi


def scene_with_actor_list(offset: int, records: list[tuple[int, ...]]) -> bytes:
    data = bytearray(max(offset + 16 * len(records), 0x40))
    data[:4] = b"ZSI\x01"
    data[0x10:0x18] = bytes((0x01, len(records), 0, 0)) + struct.pack("<I", offset)
    data[0x18:0x20] = bytes((0x14, 0, 0, 0, 0, 0, 0, 0))
    for index, record in enumerate(records):
        struct.pack_into("<8h", data, offset + 16 * index, *record)
    return bytes(data)


class ZsiActorListTests(unittest.TestCase):
    def test_parses_little_endian_16_byte_actor_records(self) -> None:
        zsi = Zsi(scene_with_actor_list(0x20, [(59, -2862, -315, -500, 0, 0, 0, 1)]))
        self.assertTrue(zsi.ok)
        self.assertEqual(zsi.actors, [Actor(59, -2862, -315, -500, 0, 0, 0, 1)])

    def test_rejects_a_truncated_actor_table(self) -> None:
        zsi = Zsi(scene_with_actor_list(0x30, []))
        zsi.data = zsi.data[:0x30]
        zsi._parse_actor_list(1, 0x30)
        self.assertIn("exceeds file size", zsi.error)


if __name__ == "__main__":
    unittest.main()
