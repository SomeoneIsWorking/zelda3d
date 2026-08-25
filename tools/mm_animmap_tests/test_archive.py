"""Tests for MM LzS and GAR decoding."""

from __future__ import annotations

import struct
import unittest

from mm_animmap_archive import Gar, lzs_decompress, lzs_is_compressed


class ArchiveTests(unittest.TestCase):
    def test_literal_lzs_packet(self) -> None:
        payload = b"\x07abc"
        archive = b"LzS\x01" + b"\0" * 4 + struct.pack("<II", 3, len(payload)) + payload

        self.assertTrue(lzs_is_compressed(archive))
        self.assertEqual(lzs_decompress(archive), b"abc")

    def test_empty_gar_archive(self) -> None:
        archive = bytearray(0x20)
        archive[:4] = b"GAR\x02"
        struct.pack_into("<III", archive, 0x0C, 0x20, 0x20, 0x20)

        gar = Gar(bytes(archive))

        self.assertEqual(gar.entries, [])
        self.assertEqual(gar.clip_names(), [])


if __name__ == "__main__":
    unittest.main()
