#!/usr/bin/env python3
"""Pin the CmbVShader ShaderMode correlation reader.

`oot3d-decomp/docs/cmb_texcoord_mapping.md` recovers that the shader entry point
chooses between the mapping-capable vertex body and the plain one with
`ShaderMode.w`, and the oracle's `vsuni_log` prints that value as `texSlotMap.w`
beside the same line's `vLit`/`fLit`. `tools/cmb_shader_mode_correlation.py` reads
that correlation off a capture, and its answer decided whether the host may gate
the mapping on lit-ness (it may not — a real capture has unlit draws at mode 0).

The tests use the cached title capture as the positive control and synthetic lines
as the negative controls, because a log reader that silently matched nothing would
otherwise report an empty, confident-looking table.
"""

from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

from cmb_shader_mode_correlation import report, rows

CACHE = REPO / "scratch/oracle_cache/36768624141dc421_6510135ae6c38599_p45-00401070_tpoff"
LOG = CACHE / "artifacts/title-vsuni_1ad0f79d0a3d.log"


def _line(draw: int, *, mode: str, vlit: int, flit: int, mapping: str = "(1,1,1,1)") -> str:
    return (f"draw n={draw} idx=1 hasCol=0 vLit={vlit} fLit={flit} picaLit=0 "
            f"cmdList=20196b30/1432/9788 matDif=(1,1,1,1) vtxScl0=(1,1,1,1) "
            f"texSlotMap=(0,0,0,{mode}) texMappingMethod={mapping} tex0=184bbb80/32x64/f3\n")


class ShaderModeCorrelationTests(unittest.TestCase):
    def test_reads_mode_slot_mapping_and_lighting_per_draw(self) -> None:
        parsed = rows(io.StringIO(_line(7, mode="0", vlit=1, flit=0, mapping="(3,4,0,1)")))
        self.assertEqual(len(parsed), 1)
        row = parsed[0]
        self.assertEqual(row["draw"], 7)
        self.assertEqual(row["shader_mode"], "0")
        self.assertEqual(row["tex_coord_slot"], ("0", "0", "0"))
        self.assertEqual(row["mapping"], ("3", "4", "0", "1"))
        self.assertTrue(row["vertex_lit"])
        self.assertFalse(row["fragment_lit"])
        self.assertTrue(row["lit"])

    def test_a_line_missing_the_mode_is_an_error_not_a_default_zero(self) -> None:
        broken = _line(1, mode="0", vlit=1, flit=0).replace("texSlotMap=(0,0,0,0) ", "")
        with self.assertRaises(ValueError):
            rows(io.StringIO(broken))

    def test_fragment_lighting_alone_counts_as_lit(self) -> None:
        parsed = rows(io.StringIO(_line(2, mode="0", vlit=0, flit=1)))
        self.assertTrue(parsed[0]["lit"])

    def test_correlation_table_separates_lit_from_unlit_per_mode(self) -> None:
        stream = io.StringIO(
            _line(1, mode="0", vlit=1, flit=0) + _line(2, mode="0", vlit=0, flit=0)
            + _line(3, mode="2", vlit=0, flit=0) + _line(4, mode="2", vlit=0, flit=0)
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(report(rows(stream)), 0)
        text = output.getvalue()
        self.assertIn("draws=4", text)
        # The decisive shape: mode 0 holds both lit and unlit draws, so the mode is
        # NOT a synonym for "lit"; mode 2 in this capture holds only unlit ones.
        self.assertIn("mode=0: 1 lit / 1 unlit", text)
        self.assertIn("mode=2: 0 lit / 2 unlit", text)

    def test_empty_input_is_reported_not_silently_passed(self) -> None:
        self.assertEqual(report([]), 1)

    @unittest.skipUnless(LOG.is_file(), "cached title vsuni log is absent")
    def test_real_title_capture_refutes_the_lit_gate(self) -> None:
        with LOG.open(encoding="utf-8", errors="replace") as stream:
            parsed = rows(stream)
        self.assertGreater(len(parsed), 50, "capture is too small to be the title frame")
        by_mode: dict[str, list[dict[str, object]]] = {}
        for row in parsed:
            by_mode.setdefault(str(row["shader_mode"]), []).append(row)
        self.assertEqual(sorted(by_mode), ["0", "2"])
        # Mode 0 is not "lit": it must contain at least one unlit draw, otherwise
        # gating the mapping on lit-ness would look supported by this capture.
        unlit_at_mode_zero = [row for row in by_mode["0"] if not row["lit"]]
        self.assertTrue(unlit_at_mode_zero, "expected unlit draws at ShaderMode 0")
        # Every draw that samples a mapping 3/4 coordinator took mode 0, so the
        # host's unconditional mapping on the model path is the observed behaviour.
        mapped = [row for row in parsed
                  if any(m in ("3", "4") for m in row["mapping"][:3])]
        self.assertTrue(mapped)
        self.assertEqual({str(row["shader_mode"]) for row in mapped}, {"0"})


if __name__ == "__main__":
    unittest.main()
