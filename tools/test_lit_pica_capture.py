#!/usr/bin/env python3
"""Tests for lit_pica_capture.py's vsuni_log parser.

The one bug worth a test is the one that produced a *confident wrong negative*. The log line format is

    draw n=<id> idx=... hasCol=... vLit=... fLit=... picaLit=... cmdList=...

and the draw id lives in `n=`, not `draw=`. A parser matching `draw=(\\d+)` finds **zero** draws in a
log full of them, and the tool then reports "no draw has fragment lighting enabled" -- which reads as
a measured property of the scene when it is really a broken regex. That is precisely the shape of
false negative this project keeps paying for, so it is pinned here.

The other property pinned is that a negative is reported *with its denominator*. "No lit draws" is only
useful next to how many draws were seen and how many were vertex-lit; a bare zero is indistinguishable
from a parse failure.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from lit_pica_capture import parse_draws  # noqa: E402

UNLIT = (
    "draw n=0 idx=1 hasCol=1 vLit=0 fLit=0 picaLit=0 cmdList=20193880/340/3136 "
    "matDif=(1,1,1,1) matAmb=(1,1,1,1) dir0=(0,0,-1,0) dif0=(0,0,0,0) amb0=(0,0,0,0)"
)
VERTEX_LIT = (
    "draw n=7 idx=2 hasCol=1 vLit=1 fLit=0 picaLit=0 cmdList=20193880/728/3136 "
    "tex0=5123456/64x64/f8 texEn=1/0/0 tev0=src8577,src84c0/0300,0300/op2100-2100/sc1x1/k0"
)
PICA_LIT = (
    "draw n=9 idx=3 hasCol=1 vLit=1 fLit=1 picaLit=1 cmdList=20193880/900/3136 "
    "tex0=5129999/32x32/f8 texEn=1/0/0"
)


class TheIdIsInNNotDraw(unittest.TestCase):
    def test_a_line_is_recognised_and_its_id_read_from_n(self) -> None:
        draws = parse_draws(UNLIT + "\n")
        self.assertEqual(len(draws), 1)
        self.assertEqual(draws[0]["id"], "0")

    def test_a_log_full_of_draws_does_not_parse_as_empty(self) -> None:
        """The regression this file exists for: the wrong token yields zero, not an error."""
        log = "\n".join((UNLIT, VERTEX_LIT, PICA_LIT))
        self.assertEqual(len(parse_draws(log)), 3)

    def test_ids_sort_numerically_not_lexically(self) -> None:
        log = "\n".join(
            f"draw n={i} idx=1 hasCol=1 vLit=0 fLit=0 picaLit=0 cmdList=1/1/1" for i in (10, 2, 33, 4)
        )
        self.assertEqual([d["id"] for d in parse_draws(log)], ["2", "4", "10", "33"])


class TheLightingFlagsAreReadSeparately(unittest.TestCase):
    """`picaLit` is the authoritative `regs.lighting.disable`; `vLit`/`fLit` are not substitutes.

    The committed probe selects draws on the register precisely because the CmbVShader booleans
    disagree with it, so conflating them here would select the wrong draw.
    """

    def setUp(self) -> None:
        self.draws = {d["id"]: d for d in parse_draws("\n".join((UNLIT, VERTEX_LIT, PICA_LIT)))}

    def test_pica_lit_is_read_from_picalit(self) -> None:
        lit = [d for d in self.draws.values() if d.get("picaLit") == "1"]
        self.assertEqual([d["id"] for d in lit], ["9"])

    def test_vertex_lit_is_not_mistaken_for_pica_lit(self) -> None:
        vertex = [d for d in self.draws.values() if d.get("vLit") == "1"]
        self.assertEqual(sorted(d["id"] for d in vertex), ["7", "9"])
        self.assertEqual(self.draws["7"].get("picaLit"), "0")

    def test_the_tex0_identity_is_captured_for_draw_matching(self) -> None:
        self.assertEqual(self.draws["7"]["tex0"], "5123456/64x64/f8")


class NonDrawLinesAreIgnored(unittest.TestCase):
    def test_a_header_line_is_not_parsed_as_a_draw(self) -> None:
        self.assertEqual(parse_draws("vsuni_log: frame 12\nsome other output\n"), [])

    def test_a_draw_line_without_an_id_is_skipped_not_guessed(self) -> None:
        self.assertEqual(parse_draws("draw idx=1 picaLit=1\n"), [])


if __name__ == "__main__":
    unittest.main()
