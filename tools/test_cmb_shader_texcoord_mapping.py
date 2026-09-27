#!/usr/bin/env python3
"""Pin the CmbVShader texture-mapping recovery against the retail shader binary.

`oot3d-decomp/docs/cmb_texcoord_mapping.md` is derived from the decoded program, and its
decoding is only trustworthy if the disassembler decodes PICA's `cmp` and flow control the way
the hardware does. This test reads the REAL `/CmbVShader.shbin` out of the user ROM, decodes it
with the decomp tool, and asserts the specific words the recovery rests on.

The assertions are the falsifier for the whole document: if the tool's `cmp`/flow decode ever
regresses, or the retail program changes, these fail. It also pins the two structural facts a
straight-line reading gets wrong (the `call`-selected second body, and `ifc` ignoring bools),
because those are what produced the "the mapping switch is dead code" false conclusion.

ROM-free CI skips it; the ROM is a player-owned input that CI must not have.
"""

from __future__ import annotations

import importlib.util
import os
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DECOMP = REPO / "oot3d-decomp"
sys.path.insert(0, str(REPO / "tools"))

TOOL = DECOMP / "tools" / "shbin_disasm.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("cmb_shbin_disasm", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _rom() -> Path | None:
    configured = os.environ.get("ZELDA3D_OOT3D_ROM")
    if not configured:
        return None
    path = Path(configured)
    return path if path.is_file() else None


class CmbShaderMappingDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        rom = _rom()
        if rom is None:
            raise unittest.SkipTest(
                "ZELDA3D_OOT3D_ROM is unset; the mapping recovery is only checkable against the "
                "retail /CmbVShader.shbin, and CI must not have game data"
            )
        from ctr_romfs import CtrRom

        cls._tool = _load_tool()
        image = CtrRom(str(rom))
        try:
            shader = image.read(image.get("/CmbVShader.shbin"))
        finally:
            image.fp.close()
        cls._parsed = cls._tool.parse_shbin(shader)
        cls._words = cls._parsed["words"]
        cls._dvle = cls._parsed["dvles"][0]
        # The tool pads mnemonics to a fixed column; compare on single-spaced text.
        cls._text = [
            re.sub(r"\s+", " ", cls._tool.disasm_one(word, cls._parsed["opdescs"], index)).strip()
            for index, word in enumerate(cls._words)
        ]

    def test_uses_the_exact_recovery_document(self) -> None:
        self.assertEqual(len(self._words), 310, "retail program length changed")
        self.assertEqual((self._dvle["main_off"], self._dvle["endmain_off"]), (0, 14))
        self.assertTrue((DECOMP / "docs" / "cmb_texcoord_mapping.md").is_file())

    def test_cmp_decodes_as_a_comparison_not_arithmetic(self) -> None:
        # Word 123 is the mapping switch's own test: c95.xy is (3, 4) and r0 is c92.x.
        self.assertRegex(self._text[123], r"^cmp\s+cc\[0\] = \(c95\.x\) == \(r0\.x\); "
                                           r"cc\[1\] = \(c95\.y\) == \(r0\.y\)$")

    def test_flow_blocks_are_siblings_not_a_single_run(self) -> None:
        # `then` is [pc+1, dest_offset) and `else` is [dest_offset, dest_offset+num).
        self.assertIn("then 125..129 else 130..144 -> 145", self._text[124])
        self.assertIn("then 131..134 else 135..143 -> 144", self._text[130])
        self.assertIn("then 153..155 else 156..177 -> 178", self._text[152])
        self.assertIn("then 188..190 else 191..200 -> 201", self._text[187])

    def test_entry_point_calls_two_mutually_exclusive_bodies(self) -> None:
        # words 0..13 are `main`; 14 and 214 are CALL targets, not fall-through continuations.
        self.assertEqual(self._words[12], 0x88000000, "expected `end` at word 12")
        self.assertIn("call", self._text[3])
        self.assertIn("dest_off=14", self._text[3])
        self.assertIn("call", self._text[8])
        self.assertIn("dest_off=214", self._text[8])

    def test_ifc_conditions_ignore_the_bool_field(self) -> None:
        # Word 2 carries bool=10 (IsFragmentLighting) and word 168 carries bool=14, but `ifc`
        # combines conditional_code with op/refx/refy only. Only `ifu` reads a bool.
        for index, expected in ((2, "if cc[0]==1 "), (5, "if cc[1]==1 "), (168, "if cc[0]==1 ")):
            with self.subTest(word=index):
                self.assertTrue(self._text[index].startswith("ifc"), self._text[index])
                self.assertIn(expected, self._text[index])
        for index in (14, 19, 63, 76, 108, 113, 121, 222, 242, 279, 284, 288):
            with self.subTest(word=index):
                self.assertRegex(self._text[index], r"^ifu\s+if b\d+ ")

    def test_mapping_switch_arms_are_where_the_document_says(self) -> None:
        for word, expected in {
            135: "dp4 r10.x___, c76.xyzw, r15.xyzw",
            138: "mov r10.___w, c93.yyyy",
            141: "dp4 r3.__z_, c12.xyzw, r10.xyzw",
            142: "mul r1.xy__, c94.zzzz, r3.zzzz",
            143: "add r3.xy__, r3.xyyy, r1.xyyy",
            167: "dp4 r4.__z_, c16.xyzw, r10.xyzw",
            177: "add r4.xy__, c94.zzzz, r4.xyyy",
            197: "dp4 r5.__z_, c19.xyzw, r10.xyzw",
            198: "rcp r10.___w, r5.zzzz",
            199: "mul r5.xy__, r5.xyyy, r10.-wwww",
            200: "add r5.xy__, c94.zzzz, r5.xyyy",
        }.items():
            with self.subTest(word=word):
                self.assertTrue(self._text[word].endswith(expected), self._text[word])

    def test_sphere_source_is_half_the_view_normal_plus_half(self) -> None:
        # sub@295: r10 = viewNormal * (0.5,0.5,0,0) + (0.5,0.5,0,0), then r10.zw = (1,1).
        self.assertTrue(self._text[295].endswith("mov r1.xy__, c94.zzzz"), self._text[295])
        self.assertTrue(self._text[297].endswith("mad r10.xyzw, r14.xyzw, r1.xyzw, r1.xyzw"),
                        self._text[297])
        self.assertTrue(self._text[298].endswith("mov r10.__zw, c93.yyyy"), self._text[298])

    def test_output_w_channels_differ_between_the_two_bodies(self) -> None:
        # body@14 writes o2.w = 1; body@214 writes o2.w = 0, and both write the same registers.
        self.assertTrue(self._text[208].endswith("mov o2.___w, c93.yyyy"), self._text[208])
        self.assertTrue(self._text[260].endswith("mov o2.___w, c93.xxxx"), self._text[260])
        for word in (206, 207, 208, 258, 259, 260):
            with self.subTest(word=word):
                self.assertRegex(self._text[word], r"^mov\s+o2\.")


if __name__ == "__main__":
    unittest.main()
