"""Tests for tools/re_frontier.py — the loss guards added for docs/issues/0026.

The bug these pin: `set <id> notes=PROBE` on docs/re-frontier.md re-serialized the
file from a schema narrower than the document, dropped 149 lines of recorded
measurements, and exited 0. Three checks here map onto the issue's Fix section:

  1. a `set` on an entry holding a field shape the schema does not model refuses
     and leaves the file byte-identical (`LossRefusalTests`),
  2. a fixture entry carrying a qualified sub-field and a multi-line value with a
     markdown table survives a `set` of a DIFFERENT field byte for byte
     (`RoundTripSetTests`),
  3. `check` fails on a document holding an unmodelled field shape
     (`test_check_reports_the_unowned_shape`).

Each of those asserts a POSITIVE CONTROL alongside it — that the edit landed, or
that `check` passes on an owned document — because a tool that refuses every
write, or writes nothing, would otherwise pass a naive "nothing was lost"
assertion. `RoundTripTests.test_a_narrowed_model_is_caught_not_written` is the
direct negative control: it narrows the model exactly as issue 0026's parser did
and requires the guard to see it.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Self

MODULE_PATH = Path(__file__).with_name("re_frontier.py")

# A roadmap shaped like docs/re-frontier.md itself: a project header, an area
# prelude, an entry carrying a QUALIFIED sub-field and a MULTI-LINE value holding
# a markdown table, a second entry, and prose trailing the last entry.
FIXTURE = """# RE Frontier — fixture project

Header prose the tool does not own.

## Render

Area prelude prose.

### render.one — first step
- status: re-verified
- deps:
- evidence: binary
- where: source.cpp
- gap: measured A/B follows.

 | route | fog | mae |
 |---|---|---|
 | unified | off | 48.14 |
 | native | on | 43.56 |

 The unified route's -9.91 and the native route's -9.63 are the same quantity.
- notes (2026-09-28, dated correction): the measurement above was re-run.
- notes: plain note.

### render.two — second step
- status: todo
- deps: render.one
- evidence:
- where:
- gap:
- notes:

Trailing prose after the last entry.
"""

# The same roadmap with one hand-annotated bullet the schema cannot name, which is
# the shape class issue 0026 lost 149 lines to.
UNOWNED = FIXTURE.replace(
    "- notes: plain note.",
    "- notes: plain note.\n- resolved 2026-09-28, **item (3)**: a hand-annotated bullet "
    "the schema cannot name.")

SURVIVORS = (
    " | route | fog | mae |",
    " | unified | off | 48.14 |",
    " | native | on | 43.56 |",
    " The unified route's -9.91 and the native route's -9.63 are the same quantity.",
    "- notes (2026-09-28, dated correction): the measurement above was re-run.",
    "Area prelude prose.",
    "Trailing prose after the last entry.",
    "### render.two — second step",
    "- gap: measured A/B follows.",
    "Header prose the tool does not own.",
)


def load_module():
    """Import the tool under test the way its CLI runs it."""
    spec = importlib.util.spec_from_file_location("re_frontier_under_test", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RoadmapFixture:
    """A roadmap on disk plus the module pointed at it."""

    def __init__(self, text: str = FIXTURE) -> None:
        self.module = load_module()
        self._temp = tempfile.TemporaryDirectory()
        self.path = Path(self._temp.name) / "re-frontier.md"
        self.path.write_text(text, encoding="utf-8")
        self.output = ""

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *exception: object) -> None:
        self._temp.cleanup()

    @property
    def text(self) -> str:
        return self.path.read_text(encoding="utf-8")

    def load(self) -> tuple:
        return self.module.load(str(self.path))

    def _args(self, command: str, positional: list[str], flags: dict) -> argparse.Namespace:
        args = argparse.Namespace(cmd=command, dry_run=bool(flags.get("dry_run")))
        for name in ("title", "area", "status", "deps", "evidence", "where", "gap", "notes"):
            setattr(args, name, None)
        if command == "set":
            args.id, args.assignments = positional[0], positional[1:]
        elif command == "show":
            args.id = positional[0]
        elif command == "add":
            args.id, args.title, args.area = positional[0], positional[1], positional[2]
        return args

    def run(self, command: str, *positional: str, **flags) -> int:
        """Invoke the shipping command handler against the fixture, capturing output."""
        document, entries, order = self.load()
        args = self._args(command, list(positional), flags)
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
            code = self.module.HANDLERS[command](document, entries, order, args) or 0
        self.output = stream.getvalue()
        return code


class RoundTripTests(unittest.TestCase):
    """The pre-write guard: the model must rebuild the file it read."""

    def test_model_reproduces_every_line_of_the_fixture(self) -> None:
        with RoadmapFixture() as fixture:
            document, _entries, _order = fixture.load()
            self.assertEqual(fixture.module.round_trip_diff(document), [])
            self.assertEqual(document.render(), document.lines)

    def test_a_narrowed_model_is_caught_not_written(self) -> None:
        """Negative control: the guard is non-vacuous.

        Reproduces issue 0026's mechanism directly — drop the sub-fields and the
        multi-line value from the model, as the old parser did — and requires both
        the byte diff and the multiset check to see it.
        """
        with RoadmapFixture() as fixture:
            document, _entries, _order = fixture.load()
            entry = document.entries()[0]
            before = len(document.render())
            for span in entry.subfields():
                entry.spans.remove(span)
            for span in entry.spans:
                span.continuation.clear()
            self.assertLess(len(document.render()), before)
            self.assertTrue(fixture.module.round_trip_diff(document))
            self.assertTrue(fixture.module.dropped_lines(document.lines, document.render()))


class LossRefusalTests(unittest.TestCase):
    """Check 1: a `set` on an entry holding an unowned bullet changes nothing."""

    def test_set_refuses_and_changes_nothing(self) -> None:
        with RoadmapFixture(UNOWNED) as fixture:
            before = fixture.text
            code = fixture.run("set", "render.one", "notes=PROBE")
            self.assertEqual(code, 2)
            self.assertEqual(fixture.text, before)
            self.assertIn("REFUSING TO WRITE", self_output := fixture.output)
            self.assertIn("[render.one]", self_output)
            self.assertIn("schema cannot name", self_output)
            self.assertNotIn("PROBE", self_output)

    def test_check_reports_the_unowned_shape(self) -> None:
        """Check 3: `check` fails when the document holds an unmodelled shape."""
        with RoadmapFixture(UNOWNED) as fixture:
            before = fixture.text
            self.assertEqual(fixture.run("check"), 1)
            self.assertIn("schema does not own", fixture.output)
            self.assertEqual(fixture.text, before)

    def test_check_passes_on_a_fully_owned_document(self) -> None:
        """Positive control for the two above: the guards are not simply refusing."""
        with RoadmapFixture() as fixture:
            self.assertEqual(fixture.run("check"), 0)
            self.assertIn("re-frontier OK", fixture.output)


class RoundTripSetTests(unittest.TestCase):
    """Check 2: a sub-field and a multi-line table survive a `set` byte for byte."""

    def test_subfield_and_table_survive_a_set_of_a_different_field(self) -> None:
        with RoadmapFixture() as fixture:
            before = fixture.text
            self.assertEqual(fixture.run("set", "render.one", "notes=PROBE"), 0)
            after = fixture.text

            # POSITIVE CONTROL: the edit landed. Without it, a tool that wrote
            # nothing would pass every "nothing was lost" assertion below.
            self.assertIn("- notes: PROBE\n", after)
            # Exactly one line changed, and it is the line the edit named.
            self.assertEqual(fixture.module.dropped_lines(before.splitlines(),
                                                          after.splitlines()),
                             [("- notes: plain note.", 1)])
            for survivor in SURVIVORS:
                with self.subTest(survivor=survivor):
                    self.assertIn(survivor, after)

    def test_set_of_a_field_with_a_multi_line_value_keeps_the_continuation(self) -> None:
        """Replacing one field must not consume the paragraphs that continue another."""
        with RoadmapFixture() as fixture:
            self.assertEqual(fixture.run("set", "render.one", "gap=none now"), 0)
            after = fixture.text
            self.assertIn("- gap: none now\n", after)
            self.assertIn("- notes: plain note.", after)
            self.assertEqual(fixture.module.dropped_lines(FIXTURE.splitlines(),
                                                          after.splitlines()),
                             [("- gap: measured A/B follows.", 1)])

    def test_dry_run_reports_without_writing(self) -> None:
        with RoadmapFixture() as fixture:
            before = fixture.text
            self.assertEqual(fixture.run("set", "render.one", "notes=PROBE",
                                         dry_run=True), 0)
            self.assertIn("would update", fixture.output)
            self.assertEqual(fixture.text, before)


class SerializationTests(unittest.TestCase):
    def test_emitted_document_has_no_trailing_whitespace_or_extra_eof_line(self) -> None:
        with RoadmapFixture() as fixture:
            document, _entries, _order = fixture.load()
            text = document.text()
            self.assertTrue(text.endswith("\n"))
            self.assertFalse(text.endswith("\n\n"))
            self.assertFalse(
                any(line.endswith((" ", "\t")) for line in text.splitlines()))


if __name__ == "__main__":
    unittest.main()