#!/usr/bin/env python3
"""re_frontier.py — the RE-frontier progress tracker for Zelda3D.

The codemap (docs/codemap.md) answers "what subsystem is where + coarse status".
The issue-catalog answers "did we hit this symptom before". Neither answers the
question this project keeps tripping on: **for the ordered chain of RE steps
toward a faithful OoT3D/MM3D port, which step is real reverse-engineering vs a
render/behaviour HACK that jumped ahead of the RE?**

This tool tracks exactly that. It operates over a greppable markdown roadmap
(docs/re-frontier.md) — one entry per RE step, each with a status, its
dependencies (the RE that must land first), where the ground-truth evidence
lives, and the honest gap. It is zero-dependency (stdlib only).

Statuses (the core axis — real RE vs jumped-ahead hack):
  re-verified   RE'd from ground truth (exe / cooked data) + implemented + VERIFIED on real data
  re-partial    real RE, but a documented honest gap remains
  in-progress   actively being RE'd/implemented, not yet verified
  hack          a shortcut standing in for absent RE -- DEBT. Must be removed and
                replaced with the real mechanism (no-hacks / no-fallbacks hard rule).
  blocked       cannot start: a dependency's RE isn't done (usually COMPUTED, not stored)
  todo          not started
  skip-by-design deliberately not implemented (e.g. Bink startup movies)

THE ROADMAP IS NOT GENERATED, AND THIS TOOL MUST NEVER MAKE IT ONE.

docs/re-frontier.md is a hand-enriched record. Its entries carry content the
narrow schema this tool used to carry had no field for: QUALIFIED SUB-FIELDS
(`- notes (2026-09-28, ...):`), MULTI-LINE VALUES (a measured host-vs-oracle
table inside one `- gap:`, whole paragraphs of rationale), per-entry prose,
per-area preludes and a project-specific header. The model below therefore OWNS
EVERY LINE: `load()` tiles the file into a header, area sections, and per-entry
field spans (each carrying its own continuation lines) plus prose, and
`render()` rebuilds the document from that tiling. Four properties follow, and
all four exist because of docs/issues/0026 — where `set ... notes=PROBE` on a
no-op edit rewrote the file from that narrower model, dropped 149 lines of
recorded measurements, and exited 0:

  * A NON-VACUOUS ROUND-TRIP GUARD runs before every write and on `check`: the
    document is re-emitted from the parsed model and diffed against the file it
    read. Field bullets are rebuilt from their parsed parts, never copied, so a
    line the tiling does not account for or a bullet the model rebuilds
    differently ABORTS the write naming the lines. This is the same
    compare-before-you-overwrite guard `tools/gen_oot3d_trig_table.py --check`
    makes on its generated table, applied to prose that must survive verbatim.
  * `set` rewrites only the field line it is told to rewrite, in place. Every
    other byte is copied through, including the paragraphs that continue a field
    and the sub-fields qualified with a date.
  * A field-shaped bullet the grammar does not OWN — one whose label is not a
    single token, or which has no `:` separator (`- resolved 2026-09-28, ...`,
    `- current static boundary: ...`) — is reported by `check` and REFUSES every
    write while it exists. An annotation the model cannot represent is exactly
    the shape that lost those 149 lines.
  * Writes go through a temp file plus rename, so an interrupted write cannot
    truncate the only copy, and `--dry-run` reports an edit without touching it.

Commands:
  list [--area A] [--status S]   table of entries
  show <id>                      full entry
  next [--area A]                steps ready to work (all deps satisfied) + hacks to replace
  hacks                          every hack entry -- the debt list (no-hacks rule)
  blocked                        steps whose deps' RE isn't done yet
  tree [--area A]                dependency tree per area
  stats                          counts by status
  check                          integrity + loss awareness; exit 1 on drift
  scaffold [--area A]            create an empty roadmap at $RE_FRONTIER_ROADMAP
  add <id> --title T --area A [--status S] [--deps D] [--evidence E] [--where W]
      [--gap G] [--notes N] [--dry-run]
  set <id> field=value ... [--dry-run]
                                 update fields (status/deps/evidence/where/gap/notes/title/area)

Exit codes: 0 success, 1 usage or integrity failure, 2 REFUSED to write (nothing changed).
"""
from __future__ import annotations

import argparse
import collections
import difflib
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# The roadmap lives at <repo>/docs/re-frontier.md by default (this file at
# <repo>/tools/re_frontier.py). Override with $RE_FRONTIER_ROADMAP so the same
# generic tool can run in-place from a global skill dir against any project.
ROADMAP = os.environ.get(
    "RE_FRONTIER_ROADMAP",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "docs", "re-frontier.md"))

STATUS_EMOJI = {
    "re-verified": "✅",
    "re-partial": "🟡",
    "in-progress": "🔬",
    "hack": "⛔",
    "todo": "⬜",
    "skip-by-design": "➖",
    "blocked": "⏸",  # computed
}
# Statuses that count as "the RE this step depends on is done enough to build on".
SATISFIED = {"re-verified", "re-partial", "skip-by-design"}
# The six body fields `set` rewrites in place. `title` and `area` live in the
# `### id — title` heading and the enclosing `## <area>` heading instead.
BODY_FIELDS = ("status", "deps", "evidence", "where", "gap", "notes")
VALID_STATUS = set(STATUS_EMOJI) - {"blocked"}
SETTABLE_FIELDS = frozenset(BODY_FIELDS) | {"title", "area"}

HEADER = """# RE Frontier — the ordered RE dependency chain toward a faithful port

Tracked by `tools/re_frontier.py` (consult it FIRST; update it in the SAME commit
that changes a step). This is the fine-grained companion to `docs/codemap.md`:
the codemap says *what subsystem exists*, this says *which ordered RE step is
real reverse-engineering vs a hack that jumped ahead*.

**Hard rule (no hacks / no fallbacks):** a `⛔ hack` status is DEBT, never an
acceptable resting state. It marks a shortcut standing in for absent RE and MUST
be removed as its real mechanism lands. `re_frontier.py hacks` is the debt list;
`re_frontier.py next` tells you the next RE-ready step.

**`re-verified` MEANS FAITHFUL to the real target — not "the mechanism runs."**
Internal mechanism checks without an oracle comparison are `re-partial` or
`in-progress`, never `re-verified`. The user observes the running system; that
observation overrides any internal trace.

Statuses: ✅ re-verified · 🟡 re-partial (honest gap) · 🔬 in-progress ·
⛔ hack (debt, must remove) · ⬜ todo · ➖ skip-by-design · ⏸ blocked (computed).

<!-- Machine-edited IN PLACE by tools/re_frontier.py add/set. Entries are
     `## <area>` sections holding `### <id> — <title>` headings followed by
     `- <field>: <value>` lines, optionally qualified (`- notes (date): ...`),
     optionally continued over further lines, and optionally followed by prose.
     Everything else in this file is yours: this tool rewrites only the field
     lines it is told to change, copies everything else through byte for byte,
     and refuses the write if anything else would be lost or if a field-shaped
     bullet it does not own is present. -->
"""

AREA_RE = re.compile(r"^## +(.+?)[ \t]*$")
ENTRY_RE = re.compile(r"^### +(\S+) +(—|--) +(.+?)[ \t]*$")
H1_RE = re.compile(r"^# ")
# A field bullet is `- ` + a single-token label + an optional parenthesised
# qualifier + `:` + the value. The label must be ONE token, so
# `- current static boundary: ...` is prose rather than a field named `current`,
# and is reported as unowned instead of being silently narrowed.
FIELD_BULLET_RE = re.compile(r"^(- +)([A-Za-z][A-Za-z0-9_-]*)(.*)$")
SEPARATOR_RE = re.compile(r"^([ \t]*):([ \t]*)(.*)$")
DASH_BULLET_RE = re.compile(r"^- +(?P<label>\S+)(?P<rest>.*)$")


class RoadmapError(RuntimeError):
    """The roadmap cannot be modelled; a write is refused rather than guessed at."""


@dataclass(frozen=True)
class FieldBullet:
    """One parsed `- <label>[(<qualifier>)]: <value>` line, split into its parts.

    The parts are kept separately and REBUILT by `render()`, so a parse mistake
    appears as a diff in the round-trip guard rather than as a rewritten bullet.
    """

    prefix: str
    label: str
    label_gap: str
    qualifier: str
    gap: str
    value_gap: str
    value: str

    @property
    def qualified(self) -> bool:
        return bool(self.qualifier)

    @property
    def key(self) -> str:
        """The canonical field name this bullet annotates, ignoring its qualifier."""
        return self.label

    def render(self) -> str:
        return (f"{self.prefix}{self.label}{self.label_gap}{self.qualifier}"
                f"{self.gap}:{self.value_gap}{self.value}")


@dataclass
class FieldSpan:
    """A field bullet plus every continuation line of its value.

    A multi-line value is the shape that made the old re-serialising writer delete
    a measured A/B table, so the continuation lines are part of the span and are
    never re-flowed, re-wrapped or dropped.
    """

    line_no: int
    bullet: FieldBullet
    continuation: list[str] = field(default_factory=list)

    @property
    def key(self) -> str:
        return self.bullet.key

    @property
    def qualified(self) -> bool:
        return self.bullet.qualified

    @property
    def value(self) -> str:
        return self.bullet.value

    def render(self) -> list[str]:
        return [self.bullet.render()] + list(self.continuation)

    def size(self) -> int:
        return 1 + len(self.continuation)


@dataclass
class Entry:
    """One `### <id> — <title>` step: the fields it declares and its own prose."""

    id: str
    title: str
    area: str
    line_no: int
    dash: str = "—"
    preamble: list[str] = field(default_factory=list)
    spans: list[FieldSpan] = field(default_factory=list)
    unowned: list[UnownedBullet] = field(default_factory=list)

    def canonical(self) -> dict[str, FieldSpan]:
        """Unqualified field spans, first occurrence wins; later ones are reported."""
        out: dict[str, FieldSpan] = {}
        for span in self.spans:
            if span.qualified or span.key not in BODY_FIELDS:
                continue
            out.setdefault(span.key, span)
        return out

    def subfields(self) -> list[FieldSpan]:
        return [span for span in self.spans if span.qualified]

    def duplicates(self) -> list[str]:
        counts: collections.Counter[str] = collections.Counter(
            span.key for span in self.spans if not span.qualified and span.key in BODY_FIELDS)
        return sorted(key for key, count in counts.items() if count > 1)

    def heading(self) -> str:
        return f"### {self.id} {self.dash} {self.title}"

    def size(self) -> int:
        return 1 + len(self.preamble) + sum(span.size() for span in self.spans)

    def render(self) -> list[str]:
        lines = list(self.preamble)
        for span in self.spans:
            lines.extend(span.render())
        return [self.heading()] + lines

    def value_of(self, key: str) -> str:
        span = self.canonical().get(key)
        return span.value if span is not None else ""

    def status(self) -> str:
        return self.value_of("status")

    def deps(self) -> list[str]:
        return [item.strip() for item in self.value_of("deps").split(",") if item.strip()]

    def prose_lines(self) -> list[str]:
        return [line for span in self.spans for line in span.continuation if line.strip()]


@dataclass
class Section:
    """One `## <area>` heading, its prelude prose, and the entries it holds."""

    heading: str
    area: str
    prelude: list[str] = field(default_factory=list)
    entries: list[Entry] = field(default_factory=list)
    unowned: list[UnownedBullet] = field(default_factory=list)

    def render(self) -> list[str]:
        lines = [self.heading] + list(self.prelude)
        for entry in self.entries:
            lines.extend(entry.render())
        return lines


@dataclass
class UnownedBullet:
    """A `- ...` bullet inside an entry that the field grammar does not own.

    SCOPE, not severity, decides what an unowned bullet blocks. `check` reports
    every one of them, because each is a shape the tool cannot name and a future
    edit cannot be trusted to carry. A `set` on a DIFFERENT entry is unaffected:
    the writer copies this bullet through the untouched entry's span list byte for
    byte, which the round-trip guard proves on every write. Refusing the whole
    document would make `set` a no-op everywhere over one stale line, which is
    the same "fix" as forcing `--force`.
    """

    line_no: int
    entry_id: str
    line: str
    reason: str

    def report(self) -> str:
        text = self.line if len(self.line) <= 150 else self.line[:147] + "..."
        return f"  line {self.line_no} [{self.entry_id}] {text}\n      why: {self.reason}"


@dataclass
class Document:
    """The roadmap as an ordered tiling that owns every line of the file."""

    path: str
    lines: list[str] = field(default_factory=list)
    header: list[str] = field(default_factory=list)
    sections: list[Section] = field(default_factory=list)
    tail: list[str] = field(default_factory=list)
    unowned: list[UnownedBullet] = field(default_factory=list)
    exists: bool = False
    trailing_newline: bool = True

    def entries(self) -> list[Entry]:
        return [entry for section in self.sections for entry in section.entries]

    def by_id(self) -> dict[str, Entry]:
        return {entry.id: entry for entry in self.entries()}

    def render(self) -> list[str]:
        """Rebuild the document from the model. Must reproduce `lines` byte for byte."""
        lines = list(self.header)
        for section in self.sections:
            lines.extend(section.render())
        lines.extend(self.tail)
        return lines

    def text(self) -> str:
        return "\n".join(self.render()) + ("\n" if self.trailing_newline else "")


def document_ids(entries: list[Entry]) -> list[str]:
    return [entry.id for entry in entries]


def _balanced_group_end(text: str) -> int | None:
    """Index just past the balanced `(` group opened at 0, or None.

    Depth is clamped at zero so a stray `)` inside the qualifier's own text cannot
    close it early: this roadmap's qualifiers carry `(1)`/`(3)` enumerations and
    colons of their own.
    """
    if not text.startswith("("):
        return None
    depth = 0
    for index, char in enumerate(text):
        if char == "(":
            depth += 1
        elif char == ")":
            if depth == 0:
                continue
            depth -= 1
            if depth == 0:
                return index + 1
    return None


def parse_field_bullet(line: str) -> FieldBullet | None:
    """Split a `- <label>[(<qualifier>)]: <value>` line, or None when it is not one."""
    match = FIELD_BULLET_RE.match(line)
    if match is None:
        return None
    prefix, label, tail = match.group(1), match.group(2), match.group(3)
    lead = tail.lstrip(" \t")
    label_gap = tail[:len(tail) - len(lead)]
    qualifier = ""
    if lead.startswith("("):
        end = _balanced_group_end(lead)
        if end is None:
            return None
        qualifier = lead[:end]
        lead = lead[end:]
    separator = SEPARATOR_RE.match(lead)
    if separator is None:
        return None
    return FieldBullet(prefix, label, label_gap, qualifier, separator.group(1),
                       separator.group(2), separator.group(3))


def unowned_reason(line: str) -> str:
    """Why a dash bullet inside an entry is not a field bullet this tool owns."""
    match = DASH_BULLET_RE.match(line)
    if match is None:
        return "not a `- <label>:` bullet"
    label, rest = match.group("label"), match.group("rest").lstrip(" \t")
    if rest.startswith("("):
        return f"`{label}` has a qualifier that is not closed before a `:` separator"
    if not rest.startswith(":"):
        if label in BODY_FIELDS:
            return (f"`{label}` is a tracked field but the bullet has no `:` separator, so "
                    f"`set {label}=` could not address it without guessing")
        return (f"`{label}` is followed by more words before any `:`, so it reads as prose "
                f"rather than a field")
    return f"`{label}` is not one of the tracked fields ({', '.join(BODY_FIELDS)})"


def load(path: str | None = None) -> tuple[Document, dict[str, Entry], list[str]]:
    """Parse the roadmap into a Document whose spans tile every line of the file."""
    target = Path(path or ROADMAP)
    document = Document(path=str(target))
    if not target.is_file():
        return document, {}, []
    document.exists = True
    text = target.read_text(encoding="utf-8")
    document.trailing_newline = text.endswith("\n")
    document.lines = text.split("\n")
    if document.trailing_newline:
        document.lines.pop()

    total = len(document.lines)
    index = 0
    section: Section | None = None
    entry: Entry | None = None

    def close_entry(end: int) -> None:
        """Hand the lines after the last field span of `entry` to that span."""
        nonlocal entry
        if entry is None:
            return
        consumed = entry.line_no + entry.size()
        rest = document.lines[consumed:end]
        if entry.spans:
            entry.spans[-1].continuation.extend(rest)
        else:
            entry.preamble.extend(rest)
        entry = None

    while index < total:
        line = document.lines[index]
        if index == 0 and H1_RE.match(line):
            while index < total and not AREA_RE.match(document.lines[index]):
                document.header.append(document.lines[index])
                index += 1
            continue
        area = AREA_RE.match(line)
        if area is not None:
            close_entry(index)
            section = Section(heading=f"## {area.group(1)}", area=area.group(1))
            document.sections.append(section)
            index += 1
            while index < total and not AREA_RE.match(document.lines[index]) \
                    and not ENTRY_RE.match(document.lines[index]) \
                    and not H1_RE.match(document.lines[index]):
                section.prelude.append(document.lines[index])
                index += 1
            continue
        heading = ENTRY_RE.match(line)
        if heading is not None:
            close_entry(index)
            if section is None:
                document.unowned.append(UnownedBullet(
                    index + 1, "<no area>", line,
                    "entry heading appears before any `## <area>` heading"))
                index += 1
                continue
            entry = Entry(heading.group(1), heading.group(3), section.area, index,
                          heading.group(2))
            section.entries.append(entry)
            index += 1
            continue
        if line.startswith("# "):
            close_entry(index)
            section = None
            document.tail.append(line)
            index += 1
            continue
        if section is None:
            document.tail.append(line)
            index += 1
            continue
        if entry is None:
            section.prelude.append(line)
            index += 1
            continue
        bullet = parse_field_bullet(line)
        if bullet is None:
            if line.startswith("- "):
                unowned = UnownedBullet(index + 1, entry.id, line, unowned_reason(line))
                document.unowned.append(unowned)
                entry.unowned.append(unowned)
            if entry.spans:
                entry.spans[-1].continuation.append(line)
            else:
                entry.preamble.append(line)
            index += 1
            continue
        entry.spans.append(FieldSpan(index, bullet))
        index += 1

    close_entry(total)
    while document.tail and not document.tail[-1].strip():
        document.tail.pop()

    entries = document.by_id()
    return document, entries, document_ids(document.entries())


# ---------------------------------------------------------------- writing ----

def round_trip_diff(document: Document) -> list[str]:
    """Re-emit from the parsed model and diff it against the file. Empty means clean.

    This is the non-vacuous guard. The re-emission is rebuilt from the model's
    parts — each field bullet from its label, qualifier and value, every other
    line from the header/prelude/continuation/tail span that owns it — and then
    compared LINE BY LINE with what was read. A line the tiling does not account
    for, or a bullet the model rebuilds differently, shows up here instead of
    being written back over the original.
    """
    return [line.rstrip("\n") for line in difflib.unified_diff(
        document.lines, document.render(),
        fromfile=f"{document.path} (on disk)", tofile=f"{document.path} (re-emitted)",
        lineterm="", n=1)]


def required_counts(before: list[str], intended_drops: list[str]) -> collections.Counter:
    """How many times each non-blank line must still appear after the write.

    A roadmap repeats `- where:` dozens of times, so this is a MULTISET count and
    not a set: replacing one of three identical lines is not a loss.
    """
    counts = collections.Counter(line for line in before if line.strip())
    for line in intended_drops:
        if line.strip() and counts[line] > 0:
            counts[line] -= 1
    return counts


def losses_against(required: collections.Counter, after: list[str]) -> list[tuple[str, int]]:
    """Required lines that survive into `after` fewer times than required: (text, deficit)."""
    have = collections.Counter(line for line in after if line.strip())
    out = [(line, count - have.get(line, 0))
           for line, count in required.items() if count - have.get(line, 0) > 0]
    return sorted(out, key=lambda item: (-item[1], item[0]))


def dropped_lines(before: list[str], after: list[str]) -> list[tuple[str, int]]:
    """Non-blank lines in `before` that survive into `after` fewer times.

    Deliberately INDEPENDENT of how `after` was built, so a bug in the editing
    code surfaces as a refusal rather than as a silent deletion.
    """
    return losses_against(required_counts(before, []), after)


def atomic_write(target: Path, text: str) -> None:
    """Write through a temp file in the same directory plus rename."""
    mode = target.stat().st_mode & 0o777
    handle, temp_name = tempfile.mkstemp(dir=str(target.parent), prefix=f".{target.name}.",
                                         suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_name, mode)
        os.replace(temp_name, target)
    except BaseException:
        Path(temp_name).unlink(missing_ok=True)
        raise


def commit(document: Document, new_lines: list[str], intended_drops: list[str],
           what: str, dry_run: bool) -> int:
    """Refuse any write that loses content the edit did not mean to replace."""
    required = required_counts(document.lines, intended_drops)
    losses = losses_against(required, new_lines)
    if losses:
        report_refusal(document, "the edit would lose "
                       f"{sum(count for _, count in losses)} line(s) it did not intend to "
                       f"change", losses)
        return 2
    verb = "would update" if dry_run else "updated"
    print(f"{verb} {what} ({document.path}: {len(document.lines)} -> {len(new_lines)} lines; "
          f"{sum(required.values())} non-blank lines required to survive, "
          f"{sum(1 for line in intended_drops if line.strip())} intentionally replaced, "
          f"0 lost)")
    if not dry_run:
        atomic_write(Path(document.path), "\n".join(new_lines) +
                     ("\n" if document.trailing_newline else ""))
    return 0


def report_refusal(document: Document, headline: str,
                   losses: list[tuple[str, int]] | None = None) -> None:
    print(f"REFUSING TO WRITE {document.path}: {headline}.", file=sys.stderr)
    for line, count in (losses or [])[:40]:
        position = document.lines.index(line) + 1 if line in document.lines else 0
        where = f"old line {position}" if position else "old text"
        print(f"  {where}{f' x{count}' if count > 1 else ''}: {line[:160]}", file=sys.stderr)
    print("Nothing was written; the roadmap is unchanged.", file=sys.stderr)


def refuse_unowned(document: Document, entry: Entry) -> int | None:
    """Return exit code 2 when `entry` — the block being edited — holds unowned bullets.

    Scoped to the entry, not the document: an unowned bullet in some OTHER entry is
    carried through byte for byte by the span that owns it, which the round-trip
    guard proves on every write, so it is no reason to block an unrelated edit. A
    refusal here is narrow enough that `set` still works for a real edit.
    """
    if not entry.unowned:
        return None
    print(f"REFUSING TO WRITE {document.path}: entry {entry.id} holds "
          f"{len(entry.unowned)} field-shaped bullet(s) the schema does not own, so a "
          f"`set` on it could not tell which bullet a field=value targets. Editing around a "
          f"shape the writer cannot name is how issue 0026 deleted 149 lines of recorded "
          f"measurements and still exited 0.", file=sys.stderr)
    for bullet in entry.unowned:
        print(bullet.report(), file=sys.stderr)
    print("Nothing was written. Make each bullet either a field this tool owns "
          "(`- <field>: ...` or `- <field> (<qualifier>): ...`) or unambiguous prose, then "
          "re-run. `check` lists the same shapes across the whole roadmap.", file=sys.stderr)
    return 2


def write_guard(document: Document, intent: str, entry: Entry | None = None) -> int | None:
    """Every write passes through here: unowned shapes first, then the round trip."""
    if entry is not None:
        refused = refuse_unowned(document, entry)
        if refused is not None:
            return refused
    diff = round_trip_diff(document)
    if diff:
        print(f"REFUSING TO WRITE {document.path}: re-emitting the parsed model does not "
              f"reproduce the file, so {intent} would not be this document plus that edit. "
              f"That is a parser/writer disagreement, not a roadmap defect.", file=sys.stderr)
        for line in diff[:40]:
            print(f"  {line}", file=sys.stderr)
        print("Nothing was written.", file=sys.stderr)
        return 2
    return None


def field_line(key: str, value: str) -> str:
    """`- key: value`, or a bare `- key:` when the value is empty (no trailing space)."""
    return f"- {key}: {value}" if value else f"- {key}:"


def apply_field_edits(lines: list[str], entry: Entry,
                      updates: list[tuple[str, str]]) -> tuple[list[str], list[str]]:
    """Rewrite only the named field lines; return (lines, the lines replaced).

    A field's continuation lines stay exactly where they were: `set notes=X`
    replaces the first line of the value and leaves the paragraphs that continue
    it untouched, which is why replacing a multi-line field cannot delete its
    table by accident.
    """
    lines = list(lines)
    canonical = entry.canonical()
    drops: list[str] = []
    inserts: list[tuple[int, str]] = []
    for key, value in updates:
        span = canonical.get(key)
        if span is not None:
            drops.append(lines[span.line_no])
            lines[span.line_no] = field_line(key, value)
        elif value != "":
            anchor = entry.spans[-1].line_no if entry.spans else entry.line_no
            inserts.append((anchor + 1, field_line(key, value)))
    for position, text in sorted(inserts, key=lambda item: -item[0]):
        lines.insert(position, text)
    return lines, drops


def rewrite_heading(lines: list[str], entry: Entry, title: str) -> tuple[list[str], list[str]]:
    lines = list(lines)
    drops = [lines[entry.line_no]]
    lines[entry.line_no] = f"### {entry.id} {entry.dash} {title}"
    return lines, drops


def section_end(lines: list[str], area: str) -> int:
    """Index just past the last line of `area`'s section, or len(lines) if absent."""
    start = next((index for index, line in enumerate(lines)
                  if AREA_RE.match(line) and AREA_RE.match(line).group(1) == area), None)
    if start is None:
        return len(lines)
    for index in range(start + 1, len(lines)):
        if AREA_RE.match(lines[index]) or lines[index].startswith("# "):
            return index
    return len(lines)


def move_entry_to_area(lines: list[str], entry: Entry, area: str) -> list[str]:
    """Move an entry's whole block — fields, sub-fields, prose — under `## area`.

    The block is cut out whole and re-inserted after blank-line trimming at both
    seams, so moving an entry never leaves a doubled blank line or a heading glued
    to prose. Everything outside the block is untouched.
    """
    head = entry.heading()
    if lines.count(head) != 1:
        raise RoadmapError(f"{lines.count(head)} lines read exactly {head!r}; "
                           f"cannot tell which entry to move")
    start = lines.index(head)
    block = lines[start:start + entry.size()]
    rest = lines[:start] + lines[start + entry.size():]
    moved = list(block)
    while moved and not moved[-1].strip():
        moved.pop()
    if any(AREA_RE.match(line) and AREA_RE.match(line).group(1) == area for line in rest):
        at = section_end(rest, area)
        while at > 0 and not rest[at - 1].strip():
            at -= 1
        tail = rest[at:]
        while tail and not tail[0].strip():
            tail.pop(0)
        return rest[:at] + moved + [""] + tail
    while rest and not rest[-1].strip():
        rest.pop()
    return rest + ["", f"## {area}", ""] + moved


# ---------------------------------------------------------------- reading ----

def effective_status(entry: Entry, entries: dict[str, Entry]) -> str:
    """A todo/in-progress step whose deps aren't all satisfied is BLOCKED."""
    status = entry.status()
    if status in ("todo", "in-progress"):
        for dep in entry.deps():
            target = entries.get(dep)
            if target is None or target.status() not in SATISFIED:
                return "blocked"
    return status


def emoji(status: str) -> str:
    return STATUS_EMOJI.get(status, "?")


def cmd_list(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    for eid in order:
        entry = entries[eid]
        if args.area and entry.area != args.area:
            continue
        effective = effective_status(entry, entries)
        if args.status and effective != args.status and entry.status() != args.status:
            continue
        print(f"{emoji(effective)} {entry.status():<14} {eid:<34} {entry.title}")
    return 0


def cmd_show(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    entry = entries.get(args.id)
    if entry is None:
        print(f"no such entry: {args.id}", file=sys.stderr)
        return 1
    effective = effective_status(entry, entries)
    print(f"### {entry.id} — {entry.title}")
    print(f"  area:     {entry.area}")
    print(f"  status:   {emoji(entry.status())} {entry.status()}" +
          (f"  (effective: {emoji(effective)} {effective})"
           if effective != entry.status() else ""))
    print(f"  deps:     {', '.join(entry.deps()) or '—'}")
    for dep in entry.deps():
        target = entries.get(dep)
        tag = f"{emoji(target.status())} {target.status()}" if target else "‼ UNKNOWN"
        print(f"              {dep}: {tag}")
    for key in ("evidence", "where", "gap", "notes"):
        print(f"  {key + ':':<9} {entry.value_of(key) or '—'}")
    for span in entry.subfields():
        print(f"  [{span.key} qualified, line {span.line_no + 1}] "
              f"{span.bullet.qualifier[:120]}")
    prose = entry.prose_lines()
    if prose:
        print(f"  prose:    {len(prose)} non-blank line(s) continue the fields above (tables, "
              f"rationale, dated corrections) — read them before acting")
    return 0


def cmd_next(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    ready = [entries[eid] for eid in order
             if (not args.area or entries[eid].area == args.area)
             and entries[eid].status() in ("todo", "in-progress")
             and effective_status(entries[eid], entries) != "blocked"]
    print("== RE-ready steps (all deps satisfied) ==")
    if not ready:
        if not entries:
            print("  ‼ ZERO entries parsed — this is NOT 'nothing is ready', it is 'the "
                  "roadmap was never read'. Run `check` for the diagnosis.")
        else:
            print(f"  (none of the {len(entries)} parsed step(s) is ready — each is either "
                  f"done or blocked on upstream RE)")
    for entry in ready:
        print(f"  {emoji(entry.status())} {entry.id:<34} {entry.title}")
        if entry.value_of("gap"):
            print(f"      gap: {entry.value_of('gap')}")
    hacks = [entries[eid] for eid in order
             if entries[eid].status() == "hack"
             and (not args.area or entries[eid].area == args.area)]
    if hacks:
        print("\n== ⛔ hacks to REPLACE with real RE (no-hacks rule) ==")
        for entry in hacks:
            print(f"  {entry.id:<34} {entry.title}")
            if entry.value_of("gap"):
                print(f"      real mechanism: {entry.value_of('gap')}")
    return 0


def cmd_hacks(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    hacks = [entries[eid] for eid in order if entries[eid].status() == "hack"]
    if not hacks:
        print(f"No hacks tracked among {len(order)} parsed entr(ies). (Good — the no-hacks "
              f"rule holds for everything carrying a `- status:` line; debt described only in "
              f"prose is invisible here.)")
        return 0
    print(f"⛔ {len(hacks)} hack(s) — DEBT standing in for real RE, must be removed:\n")
    for entry in hacks:
        print(f"  {entry.id:<34} [{entry.area}] {entry.title}")
        if entry.value_of("where"):
            print(f"      where: {entry.value_of('where')}")
        if entry.value_of("gap"):
            print(f"      real mechanism: {entry.value_of('gap')}")
    return 0


def cmd_blocked(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    for eid in order:
        entry = entries[eid]
        if effective_status(entry, entries) != "blocked":
            continue
        unmet = [dep for dep in entry.deps()
                 if dep not in entries or entries[dep].status() not in SATISFIED]
        print(f"⏸ {eid:<34} {entry.title}")
        print(f"      waiting on: {', '.join(unmet)}")
    return 0


def cmd_tree(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    children: dict[str, list[str]] = {eid: [] for eid in order}
    roots: list[str] = []
    for eid in order:
        deps = [dep for dep in entries[eid].deps() if dep in entries]
        if not deps:
            roots.append(eid)
        for dep in deps:
            children[dep].append(eid)

    printed: set[str] = set()

    def walk(eid: str, depth: int) -> None:
        if args.area and entries[eid].area != args.area:
            return
        mark = " (seen)" if eid in printed else ""
        print(f"{'  ' * depth}{emoji(effective_status(entries[eid], entries))} {eid}{mark}")
        if eid in printed:
            return
        printed.add(eid)
        for child in children[eid]:
            walk(child, depth + 1)

    for root in roots:
        walk(root, 0)
    return 0


def cmd_stats(document: Document, entries: dict[str, Entry],
               order: list[str], args) -> int:
    counts: collections.Counter[str] = collections.Counter(
        effective_status(entries[eid], entries) for eid in order)
    print(f"{len(order)} step(s) tracked:")
    for status in ("re-verified", "re-partial", "in-progress", "blocked", "todo", "hack",
                   "skip-by-design"):
        if counts.get(status):
            print(f"  {emoji(status)} {status:<14} {counts[status]}")
    return 0


def cmd_check(document: Document, entries: dict[str, Entry], order: list[str], args) -> int:
    if not document.exists:
        print(f"‼ {document.path} does not exist — checked NOTHING. Set $RE_FRONTIER_ROADMAP "
              f"or run `scaffold`.", file=sys.stderr)
        return 1
    problems = 0

    # LOSS AWARENESS, part one: the model must rebuild the file, or every check
    # below is computed over a document this tool does not actually understand.
    diff = round_trip_diff(document)
    if diff:
        problems += 1
        print(f"‼ {document.path}: re-emitting the parsed model does not reproduce the file, "
              f"so nothing below is trustworthy:", file=sys.stderr)
        for line in diff[:40]:
            print(f"  {line}", file=sys.stderr)

    # LOSS AWARENESS, part two: a field-shaped bullet the grammar does not own is
    # the shape that lost 149 lines on 2026-09-29. `check` fails so the author sees
    # it the moment it is written, and `set`/`add` refuse until it is resolved.
    if document.unowned:
        problems += len(document.unowned)
        print(f"‼ {len(document.unowned)} field-shaped bullet(s) in {document.path} that the "
              f"schema does not own. A `set` on one of these entries refuses rather than "
              f"guessing which bullet the assignment targets; a `set` elsewhere is unaffected "
              f"because the writer copies these lines through byte for byte. This is the shape "
              f"that made issue 0026 drop 149 lines of recorded measurements and exit 0.",
              file=sys.stderr)
        for bullet in document.unowned:
            print(bullet.report(), file=sys.stderr)
        print("  resolve each by making it a field this tool owns (`- <field>: ...` or "
              "`- <field> (<qualifier>): ...`) or by making it unambiguous prose, then "
              "re-run `check`. Do not delete the text.", file=sys.stderr)

    for eid in order:
        entry = entries[eid]
        if entry.status() not in VALID_STATUS:
            print(f"‼ {eid}: invalid status '{entry.status()}'", file=sys.stderr)
            problems += 1
        duplicates = entry.duplicates()
        if duplicates:
            print(f"‼ {eid}: duplicate unqualified field bullet(s) "
                  f"{', '.join(duplicates)} — `set` rewrites the first, so a later one would "
                  f"keep contradicting it", file=sys.stderr)
            problems += 1
        for dep in entry.deps():
            if dep not in entries:
                print(f"‼ {eid}: unknown dependency '{dep}'", file=sys.stderr)
                problems += 1
        if entry.status() == "re-verified" and not entry.value_of("evidence"):
            print(f"‼ {eid}: re-verified but no evidence cited (RE must name ground truth)",
                  file=sys.stderr)
            problems += 1

    white, gray, black = 0, 1, 2
    color = {eid: white for eid in order}

    def dfs(eid: str, stack: list[str]) -> bool:
        color[eid] = gray
        for dep in entries[eid].deps():
            if dep not in entries:
                continue
            if color[dep] == gray:
                print(f"‼ dependency cycle: {' -> '.join(stack + [dep])}", file=sys.stderr)
                return True
            if color[dep] == white and dfs(dep, stack + [dep]):
                return True
        color[eid] = black
        return False

    for eid in order:
        if color[eid] == white and dfs(eid, [eid]):
            problems += 1

    # ZERO entries is a failure, not a pass: every check above is vacuously true
    # over an empty set, so a green result over an unparsed file measures nothing.
    if not order:
        print(f"‼ {document.path} exists ({len(document.lines)} lines) but ZERO entries "
              f"parsed — verified NOTHING. Either the file has no steps yet (run `scaffold`) "
              f"or the parser and the document disagree about the format.", file=sys.stderr)
        return 1

    hacks = sum(1 for eid in order if entries[eid].status() == "hack")
    if hacks:
        print(f"⛔ {hacks} hack(s) present — debt, run `re_frontier.py hacks` (not a check "
              f"failure, but must be burned down).", file=sys.stderr)
    if problems:
        print(f"\n{problems} problem(s) found.", file=sys.stderr)
        return 1
    subfields = sum(len(entry.subfields()) for entry in document.entries())
    continued = sum(1 for entry in document.entries() for span in entry.spans
                    if span.continuation)
    print(f"re-frontier OK: {len(order)} entr(ies) parsed from {document.path} "
          f"({len(document.lines)} lines, every line accounted for) — no unknown deps, no "
          f"cycles, every re-verified step cites evidence. Modelled alongside the six body "
          f"fields: {subfields} qualified sub-field(s), {continued} field(s) with "
          f"multi-line values.")
    return 0


def cmd_scaffold(document: Document, entries: dict[str, Entry], order: list[str], args) -> int:
    """Bootstrap an empty roadmap at $RE_FRONTIER_ROADMAP (or docs/re-frontier.md)."""
    target = Path(ROADMAP)
    if target.exists():
        print(f"{target} already exists — not overwriting.", file=sys.stderr)
        return 1
    target.parent.mkdir(parents=True, exist_ok=True)
    area = args.area or "core"
    body = (
        "### area.first-step — Describe the first RE step in this chain\n"
        "- status: todo\n"
        "- deps:\n"
        "- evidence:\n"
        "- where:\n"
        "- gap: Fill in real steps; add deps to encode the RE dependency order.\n"
        "- notes:")
    target.write_text(f"{HEADER}\n## {area}\n\n{body}\n", encoding="utf-8")
    print(f"scaffolded {target} — edit it, then `re_frontier.py check`.")
    return 0


def cmd_add(document: Document, entries: dict[str, Entry], order: list[str], args) -> int:
    # `add` appends a block into an existing area's section; it rewrites no entry,
    # so an unowned bullet elsewhere is carried through untouched. The round-trip
    # guard and the post-edit multiset check still gate the write.
    refused = write_guard(document, "adding an entry")
    if refused is not None:
        return refused
    if args.id in entries:
        print(f"entry '{args.id}' already exists (use `set`)", file=sys.stderr)
        return 1
    if args.status not in VALID_STATUS:
        print(f"invalid status '{args.status}'", file=sys.stderr)
        return 1
    if not document.exists:
        print(f"{document.path} does not exist — refusing to create it from `add` "
              f"(run `scaffold`, or set $RE_FRONTIER_ROADMAP to the real roadmap).",
              file=sys.stderr)
        return 1
    lines = list(document.lines)
    block = ["", f"### {args.id} — {args.title}", field_line("status", args.status),
             field_line("deps", args.deps or "")]
    for value, key in ((args.evidence, "evidence"), (args.where, "where"),
                       (args.gap, "gap"), (args.notes, "notes")):
        block.append(field_line(key, value or ""))
    at = section_end(lines, args.area)
    return commit(document, lines[:at] + block + lines[at:], [], f"added {args.id}",
                  args.dry_run)


def cmd_set(document: Document, entries: dict[str, Entry], order: list[str], args) -> int:
    entry = entries.get(args.id)
    if entry is None:
        print(f"no such entry: {args.id}", file=sys.stderr)
        return 1
    refused = write_guard(document, f"updating {args.id}", entry)
    if refused is not None:
        return refused
    updates: list[tuple[str, str]] = []
    new_title: str | None = None
    new_area: str | None = None
    for assignment in args.assignments:
        if "=" not in assignment:
            print(f"bad assignment '{assignment}' (want field=value)", file=sys.stderr)
            return 1
        key, value = assignment.split("=", 1)
        key = key.strip()
        if key not in SETTABLE_FIELDS:
            print(f"unknown field '{key}' (known: {', '.join(sorted(SETTABLE_FIELDS))})",
                  file=sys.stderr)
            return 1
        if key == "status" and value not in VALID_STATUS:
            print(f"invalid status '{value}'", file=sys.stderr)
            return 1
        if key == "title":
            new_title = value
        elif key == "area":
            new_area = value
        else:
            updates.append((key, value))

    lines, drops = apply_field_edits(document.lines, entry, updates)
    if new_title is not None:
        lines, dropped = rewrite_heading(lines, entry, new_title)
        drops += dropped
    if new_area is not None and new_area != entry.area:
        try:
            lines = move_entry_to_area(lines, entry, new_area)
        except RoadmapError as error:
            print(f"REFUSING TO WRITE {document.path}: {error}.", file=sys.stderr)
            return 2
    return commit(document, lines, drops, f"updated {entry.id}", args.dry_run)


HANDLERS = {
    "list": cmd_list, "show": cmd_show, "next": cmd_next, "hacks": cmd_hacks,
    "blocked": cmd_blocked, "tree": cmd_tree, "stats": cmd_stats, "check": cmd_check,
    "scaffold": cmd_scaffold, "add": cmd_add, "set": cmd_set,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Zelda3D RE-frontier progress tracker")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("list")
    sp.add_argument("--area")
    sp.add_argument("--status")
    sp = sub.add_parser("show")
    sp.add_argument("id")
    sp = sub.add_parser("next")
    sp.add_argument("--area")
    sub.add_parser("hacks")
    sub.add_parser("blocked")
    sp = sub.add_parser("tree")
    sp.add_argument("--area")
    sub.add_parser("stats")
    sub.add_parser("check")
    sp = sub.add_parser("scaffold")
    sp.add_argument("--area")
    sp = sub.add_parser("add")
    sp.add_argument("id")
    sp.add_argument("--title", required=True)
    sp.add_argument("--area", required=True)
    sp.add_argument("--status", default="todo")
    sp.add_argument("--deps")
    sp.add_argument("--evidence")
    sp.add_argument("--where")
    sp.add_argument("--gap")
    sp.add_argument("--notes")
    sp.add_argument("--dry-run", action="store_true",
                    help="report the edit without touching the file")
    sp = sub.add_parser("set")
    sp.add_argument("id")
    sp.add_argument("assignments", nargs="+")
    sp.add_argument("--dry-run", action="store_true",
                    help="report the edit without touching the file")

    args = parser.parse_args()
    document, entries, order = load()
    return HANDLERS[args.cmd](document, entries, order, args) or 0


if __name__ == "__main__":
    raise SystemExit(main())