"""Decode PICA command-list packets shared by deterministic oracle captures."""

from __future__ import annotations

import struct


def parse_command_writes(payload: bytes, end_word: int) -> list[tuple[int, int, int]]:
    """Return ``(word_index, register, value)`` writes before one draw cursor."""
    if len(payload) % 4:
        raise ValueError("PICA command list is not word-aligned")
    words = struct.unpack(f"<{len(payload) // 4}I", payload)
    if end_word > len(words):
        raise ValueError(f"draw cursor {end_word} exceeds {len(words)} command-list words")
    writes: list[tuple[int, int, int]] = []
    index = 0
    while index < end_word:
        if index % 2:
            index += 1
        if index + 1 >= end_word:
            break
        value, header = words[index], words[index + 1]
        register = header & 0xFFFF
        extra_count = (header >> 20) & 0xFF
        grouped = bool(header & 0x80000000)
        writes.append((index, register, value))
        for offset in range(extra_count):
            extra_index = index + 2 + offset
            if extra_index >= end_word:
                raise ValueError("PICA command extends beyond draw cursor")
            writes.append((extra_index, register + offset + 1 if grouped else register, words[extra_index]))
        index += 2 + extra_count
    return writes


def last_register_write(payload: bytes, end_word: int, register: int) -> tuple[int, int]:
    """Return the final write to ``register`` before the selected draw."""
    matches = [write for write in parse_command_writes(payload, end_word) if write[1] == register]
    if not matches:
        raise RuntimeError(f"PICA register 0x{register:03x} has no write before the selected draw")
    word_index, _, value = matches[-1]
    return word_index, value
