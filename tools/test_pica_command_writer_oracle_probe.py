from __future__ import annotations

import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from pica_command_writer_oracle_probe import (
    last_register_write,
    linear_virtual_address,
    parse_command_writes,
    parse_memlog,
)


def packet(value: int, register: int, extra_count: int = 0, grouped: bool = False) -> list[int]:
    header = register | (extra_count << 20) | (int(grouped) << 31)
    return [value, header]


class CommandWriterTests(unittest.TestCase):
    def test_decodes_grouped_registers(self) -> None:
        words = packet(0x10, 0x140, extra_count=1, grouped=True) + [0x20, 0]
        payload = struct.pack("<4I", *words)
        self.assertEqual(parse_command_writes(payload, 3), [(0, 0x140, 0x10), (2, 0x141, 0x20)])

    def test_selects_last_write_before_draw(self) -> None:
        words = packet(0x1111, 0x1C3) + packet(0x2222, 0x1C3)
        payload = struct.pack("<4I", *words)
        self.assertEqual(last_register_write(payload, 4, 0x1C3), (2, 0x2222))

    def test_translates_fcram_to_linear_virtual_memory(self) -> None:
        self.assertEqual(linear_virtual_address(0x204AF360, 1622), 0x144B0CB8)

    def test_rejects_non_fcram_command_list(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside FCRAM"):
            linear_virtual_address(0x18000000, 0)

    def test_accepts_word_inside_multibyte_memory_write(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "memory.log"
            path.write_text("MW pc=0x00300000 lr=0x00000000 va=0x144b0cb4 sz=8 data=0x0\n")
            self.assertEqual(len(parse_memlog(path, 0x144B0CB8)), 1)
