#!/usr/bin/env python3
"""Pin the PICA vertex-uniform decoder against a real OoT3D command list.

`tools/pica_shader_uniforms.py` reconstructs a draw's float-uniform array from a
captured command list, which is what makes `CmbVShader`'s inputs readable
offline — `uInvView` (c76..c78) among them. The packing has several ways to be
subtly wrong (float24 word order, float32's reversed components, the incomplete
trailing queue, an index past the array), and each produces plausible numbers
rather than an error, so the positive control has to be an exact agreement with a
different mechanism: the oracle's own `vs_setup.uniforms`, logged per draw.

Fifteen uniforms are compared for one cached title draw. They were captured by
two unrelated paths — the emulator's live uniform state and this decode of the
raw command words — so agreement is evidence about the decode, not a tautology.
"""

from __future__ import annotations

import json
import re
import struct
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

from pica_command_list import parse_command_writes
from pica_shader_uniforms import (
    FLOAT24_MODE,
    FLOAT32_MODE,
    VERTEX_UNIFORM_INDEX,
    VERTEX_UNIFORM_VALUE,
    decode_vertex_uniforms,
    float24_from_raw,
)

CACHE = REPO / "scratch/oracle_cache/a647213f0ded0038_6510135ae6c38599_p45-00401070_tpoff"
VSUNI = (REPO / "scratch/oracle_cache/36768624141dc421_6510135ae6c38599_p45-00401070_tpoff"
         / "artifacts/title-vsuni_1ad0f79d0a3d.log")
PROBE = CACHE / "probes/title-pica-command-list_2010_a6a521c6e0.json"

# The `vsuni_log` field names and the shader register each one prints.
LOGGED_UNIFORMS = {
    "matDif": 8, "matAmb": 9, "texSlotMap": 89,
    "modelView0": 4, "modelView1": 5, "modelView2": 6, "modelView3": 7,
    "texMtx0_0": 10, "texMtx0_1": 11, "texMtx0_2": 12,
    "texMtx1_0": 14, "texMtx1_1": 15, "texMtx1_2": 16,
    "texMappingMethod": 92, "vtxScl0": 90,
}
TOLERANCE = 2e-3


def _f24(sign: int, exponent: int, mantissa: int) -> int:
    return (sign << 23) | (exponent << 16) | mantissa


def _f32_word(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _pack_float24_raw(x: int, y: int, z: int, w: int) -> tuple[int, int, int]:
    """Pack four float24s into PICA's three-word order (PackedAttribute::AsFloat24).

    Inverted from the reader: x = b2 & 0xffffff, y = ((b1 & 0xffff) << 8) | (b2 >> 24),
    z = ((b0 & 0xff) << 16) | (b1 >> 16), w = b0 >> 8.
    """
    return (
        ((z >> 16) & 0xFF) | ((w & 0xFFFFFF) << 8),
        ((z & 0xFFFF) << 16) | ((y >> 8) & 0xFFFF),
        (x & 0xFFFFFF) | ((y & 0xFF) << 24),
    )


def _pack_float24(x: int, y: int, z: int, w: int) -> tuple[float, ...]:
    from pica_shader_uniforms import float24_from_raw as unpack
    raw = _pack_float24_raw(x, y, z, w)
    return (unpack(raw[2] & 0xFFFFFF),
            unpack(((raw[1] & 0xFFFF) << 8) | ((raw[2] >> 24) & 0xFF)),
            unpack(((raw[0] & 0xFF) << 16) | ((raw[1] >> 16) & 0xFFFF)),
            unpack(raw[0] >> 8))


class VertexUniformDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not PROBE.is_file() or not VSUNI.is_file():
            raise unittest.SkipTest(
                "cached title command list is absent; run tools/title_oracle_probe.py "
                "pica-command-list 1093 77 and uniforms 1093 to produce it"
            )
        provenance = json.loads(PROBE.read_text())
        payload = next((CACHE / "artifacts").glob("title-pica-command-list-data_*.bin"))
        cls._end = provenance["command_list_word_index"]
        cls._draw = provenance["draw"]
        cls._uniforms = decode_vertex_uniforms(
            parse_command_writes(payload.read_bytes(), cls._end), cls._end)
        line = next(line for line in VSUNI.read_text(encoding="utf-8",
                                                      errors="replace").splitlines()
                    if line.startswith(f"draw n={cls._draw} "))
        cls._line = line

    def test_every_logged_uniform_agrees_with_the_oracle(self) -> None:
        for name, index in LOGGED_UNIFORMS.items():
            with self.subTest(uniform=name):
                match = re.search(re.escape(name) + r"=\(([^)]*)\)", self._line)
                self.assertIsNotNone(match, f"{name} is not in the captured log line")
                logged = [float(part) for part in match.group(1).split(",")]
                decoded = self._uniforms.vector(index)
                self.assertEqual(len(decoded), 4)
                for axis, (mine, theirs) in enumerate(zip(decoded, logged)):
                    self.assertAlmostEqual(
                        mine, theirs, delta=max(TOLERANCE, abs(theirs) * TOLERANCE),
                        msg=f"{name} c{index}[{axis}]: decoded {mine} vs logged {theirs}")

    def test_inverseview_is_readable_and_is_identity_for_this_draw(self) -> None:
        # c76..c78 is the mapping-4 arms' `uInvView`. It was unreadable before this
        # decoder existed, and the title capture is the first evidence for its value.
        for index in (76, 77, 78):
            self.assertIsNotNone(self._uniforms.get(index))
        rows = [tuple(round(v, 6) for v in self._uniforms.vector(index)) for index in (76, 77, 78)]
        self.assertEqual(rows, [(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0)])

    def test_texmtx_row2_is_the_unit_z_row(self) -> None:
        # The mapping-4 arm needs `TexMtx` row 2, which the host has never carried.
        for base in (10, 14, 17):
            with self.subTest(texmtx=base):
                self.assertEqual(self._uniforms.vector(base + 2), (0.0, 0.0, 1.0, 0.0))

    def test_uniform_never_written_is_absent_not_zero(self) -> None:
        # A silent 0 would read as "the game set it to zero", which is a different claim.
        unwritten = next(index for index in range(len(self._uniforms))
                        if self._uniforms.get(index) is None)
        self.assertIsNone(self._uniforms.get(unwritten))
        with self.assertRaises(KeyError):
            self._uniforms.vector(unwritten)
        with self.assertRaises(IndexError):
            self._uniforms.vector(len(self._uniforms))
        with self.assertRaises(IndexError):
            self._uniforms.vector(-1)

    def test_float24_packing_matches_its_documented_word_order(self) -> None:
        # 1.0 is exponent 63, mantissa 0, sign 0.
        self.assertEqual(float24_from_raw(_f24(0, 63, 0)), 1.0)
        self.assertEqual(float24_from_raw(_f24(1, 63, 0)), -1.0)
        self.assertEqual(float24_from_raw(0), 0.0)
        self.assertEqual(float24_from_raw(_f24(0, 62, 0)), 0.5)
        # Mantissa bit 16 is the 2^-1 place, so mantissa 0x8000 is halfway to 2.0.
        self.assertAlmostEqual(float24_from_raw(_f24(0, 63, 0x8000)), 1.5, places=6)
        # A whole vec4 packed the way PackedAttribute::AsFloat24 does, so the decoder's
        # own word order is under test and not the float helpers alone.
        one, two = _f24(0, 63, 0), _f24(0, 64, 0)  # 1.0 and 2.0 as float24
        self.assertEqual(_pack_float24(one, two, one, two), (1.0, 2.0, 1.0, 2.0))
        decoded = decode_vertex_uniforms([
            (0, VERTEX_UNIFORM_INDEX, 3),
            (1, VERTEX_UNIFORM_VALUE, _pack_float24_raw(one, two, one, two)[0]),
            (2, VERTEX_UNIFORM_VALUE, _pack_float24_raw(one, two, one, two)[1]),
            (3, VERTEX_UNIFORM_VALUE, _pack_float24_raw(one, two, one, two)[2]),
        ])
        self.assertEqual(decoded.vector(3), (1.0, 2.0, 1.0, 2.0))

    def test_float32_mode_is_reversed_and_needs_four_words(self) -> None:
        one = _f32_word(1.0)
        two = _f32_word(2.0)
        index_write = (0, VERTEX_UNIFORM_INDEX, 2 | (FLOAT32_MODE << 31))
        two_words = decode_vertex_uniforms([index_write, (1, VERTEX_UNIFORM_VALUE, one),
                                            (2, VERTEX_UNIFORM_VALUE, two)])
        self.assertIsNone(two_words.get(2), "two words must not complete a float32 vector")
        four = decode_vertex_uniforms([index_write,
                                      (1, VERTEX_UNIFORM_VALUE, one),
                                      (2, VERTEX_UNIFORM_VALUE, two),
                                      (3, VERTEX_UNIFORM_VALUE, one),
                                      (4, VERTEX_UNIFORM_VALUE, two)])
        # AsFloat32 stores word i into component 3 - i.
        self.assertEqual(four.vector(2), (2.0, 1.0, 2.0, 1.0))

    def test_index_past_the_array_is_dropped_not_wrapped(self) -> None:
        one = _f32_word(1.0)
        format_bits = FLOAT32_MODE << 31
        out_of_range = [(0, VERTEX_UNIFORM_INDEX, 4095 | format_bits)]
        out_of_range += [(slot, VERTEX_UNIFORM_VALUE, one) for slot in range(1, 5)]
        decoded = decode_vertex_uniforms(out_of_range)
        self.assertEqual(decoded.written, [], "an out-of-range index must not write slot 0")
        # ...but the next in-range write still lands, because the index keeps counting.
        writes = out_of_range + [(5, VERTEX_UNIFORM_INDEX, 7 | format_bits)]
        writes += [(6 + slot, VERTEX_UNIFORM_VALUE, one) for slot in range(4)]
        self.assertEqual(decode_vertex_uniforms(writes).vector(7), (1.0, 1.0, 1.0, 1.0))

    def test_end_word_stops_the_replay_at_the_draw_cursor(self) -> None:
        writes = parse_command_writes(
            next((CACHE / "artifacts").glob("title-pica-command-list-data_*.bin")).read_bytes(),
            self._end)
        at_cursor = decode_vertex_uniforms(writes, self._end)
        later = decode_vertex_uniforms(writes, self._end + 40)
        self.assertGreaterEqual(len(later.written), len(at_cursor.written))
        # The uniforms already present at the cursor must be identical either way.
        for index in at_cursor.written:
            self.assertEqual(at_cursor.vector(index), later.vector(index))


if __name__ == "__main__":
    unittest.main()
