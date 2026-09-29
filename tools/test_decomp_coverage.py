"""Tests for decomp_coverage.py.

The failure this project keeps paying for is an instrument that reports a clean number while
being unable to report anything else, so the centre of this file is the control: a fixture that
MUST come out positive. If the coverage counter cannot see a planted recovery, every number it
prints is untrustworthy and the test fails loudly.
"""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest

TOOL = pathlib.Path(__file__).resolve().parent / "decomp_coverage.py"
spec = importlib.util.spec_from_file_location("decomp_coverage", TOOL)
dc = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(dc)


def write_fixture(root: pathlib.Path, inventory: list[tuple[int, str, int]],
                  files: dict[str, str]) -> pathlib.Path:
    """Build a fake decomp dir: an inventory CSV plus .c files of chosen sizes."""
    decomp = root / "build" / "decomp"
    decomp.mkdir(parents=True, exist_ok=True)
    lines = ["vaddr,size,name"]
    for addr, name, size in inventory:
        lines.append(f"{addr:08x},{size},{name}")
    (decomp / "functions.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    for name, body in files.items():
        (decomp / name).write_text(body, encoding="utf-8")
    return decomp


BIG = "// header comment\n" + "int x = 1;\n" * 80
STUB = "// Function at VA 0x0 - FUN_00000000\n\n\n"


class CoverageCounting(unittest.TestCase):
    def test_counts_recovered_and_unrecovered(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40), (0x2000, "FUN_00002000", 40),
                                 (0x3000, "FUN_00003000", 40)],
                          {"00001000.c": BIG})
            got = dc.measure("oot3d", root)
        self.assertEqual(got["total"], 3)
        self.assertEqual(got["recovered"], 1)
        self.assertAlmostEqual(got["coverage"], 1 / 3)

    def test_control_must_come_out_positive(self):
        """A planted recovery MUST be counted. This is the whole point of the file."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "mm3d-decomp"
            write_fixture(root, [(0xDEAD0000, "FUN_DEAD0000", 40)], {"fn_0xdead0000.c": BIG})
            got = dc.measure("mm3d", root)
        self.assertEqual(got["recovered"], 1, "instrument cannot see a planted recovery")
        self.assertEqual(got["named"], 0)
        self.assertGreater(got["coverage"], 0.0)

    def test_stub_output_is_not_counted(self):
        """Ghidra emitting a near-empty file is a failure, not progress."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40)], {"00001000.c": STUB})
            got = dc.measure("oot3d", root)
        self.assertEqual(got["recovered"], 0)
        self.assertEqual(got["stubs"], 1)

    def test_both_naming_conventions_count(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            a = pathlib.Path(tmp) / "a" / "oot3d-decomp"
            b = pathlib.Path(tmp) / "b" / "mm3d-decomp"
            write_fixture(a, [(0x1000, "FUN_00001000", 40)], {"00001000.c": BIG})
            write_fixture(b, [(0x1000, "FUN_00001000", 40)], {"fn_0x00001000.c": BIG})
            ra, rb = dc.measure("oot3d", a), dc.measure("mm3d", b)
        self.assertEqual(ra["recovered"], 1)
        self.assertEqual(rb["recovered"], 1)

    def test_named_functions_count_as_readable(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            write_fixture(root, [(0x1000, "Actor_Update", 40)], {"00001000.c": BIG})
            got = dc.measure("oot3d", root)
        self.assertEqual(got["named"], 1)
        self.assertEqual(got["recovered"], 1)

    def test_generated_names_are_not_readable(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40), (0x2000, "sub_1234", 40)],
                          {"00001000.c": BIG, "00002000.c": BIG})
            got = dc.measure("oot3d", root)
        self.assertEqual(got["named"], 0, "FUN_/sub_ names must not count as readable")

    def test_orphan_output_is_surfaced_not_hidden(self):
        """A recovered file outside the inventory is work, but must not inflate the ratio."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40)],
                          {"00001000.c": BIG, "00abcdef.c": BIG})
            got = dc.measure("oot3d", root)
        self.assertEqual(got["recovered"], 1)
        self.assertEqual(got["orphans"], 1)
        self.assertAlmostEqual(got["coverage"], 1.0)

    def test_missing_inventory_is_reported_not_silently_zero(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "oot3d-decomp"
            (root / "build" / "decomp").mkdir(parents=True)
            got = dc.measure("oot3d", root)
        self.assertEqual(got["status"], "NO INVENTORY")
        self.assertIn("functions.csv", got["detail"])


class GateBehaviour(unittest.TestCase):
    def test_gate_fails_below_floor(self):
        import io
        import contextlib
        import tempfile
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "mm3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40)], {})
            results = [dc.measure("mm3d", root)]
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = dc.report(results, 0.10)
        self.assertEqual(rc, 1, "a game below the floor must fail the gate")
        self.assertIn("FAIL", buf.getvalue())

    def test_gate_passes_at_or_above_floor(self):
        import io
        import contextlib
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "mm3d-decomp"
            write_fixture(root, [(0x1000, "FUN_00001000", 40)], {"00001000.c": BIG})
            results = [dc.measure("mm3d", root)]
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = dc.report(results, 0.10)
        self.assertEqual(rc, 0)
        self.assertIn("OK", buf.getvalue())


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=2).result.wasSuccessful() else 1)
