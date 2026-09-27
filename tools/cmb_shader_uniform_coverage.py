#!/usr/bin/env python3
"""Report which CmbVShader uniform blocks each cached command list actually contains.

`render.cmb-texcoord-mapping` has open questions that all reduce to "what did the oracle upload into
float uniform cN for a draw like this one", and `pica_shader_uniforms.decode_vertex_uniforms` can
answer any of them from a command list that is already in the oracle cache. What it cannot do is tell
you whether a given capture spans the block you need: a capture taken for a narrow question decodes
26 uniforms and simply has nothing at c4..c7, and the decoder reports an absent uniform the same way
it reports a present one until you ask for it.

So this is the survey that answers the question *before* the next capture is planned. It walks every
command list in the cache, decodes the vertex uniform array as of that draw, and reports per capture
which named blocks are present. It prints what it scanned and what it matched, with denominators, and
refuses an empty or unreadable corpus rather than reporting a clean sheet.

It also names, per open RE question, whether ANY cached capture can answer it. That is the part worth
reading: a question with no capable capture is a requirement for the next capture, not a reason to
re-derive the shader.

Usage:
    tools/cmb_shader_uniform_coverage.py
    tools/cmb_shader_uniform_coverage.py --cache-root scratch/oracle_cache --json
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from pica_command_list import parse_command_writes
from pica_shader_uniforms import decode_vertex_uniforms

# Uniform blocks of the retail CmbVShader, by float-uniform index. The indices are the ones recovered
# in oot3d-decomp/docs/cmb_texcoord_mapping.md's uniform table; nothing here is inferred from a
# capture, so a block that no capture writes stays reported as absent instead of quietly vanishing.
UNIFORM_BLOCKS: dict[str, tuple[int, ...]] = {
    "uModelView": (4, 5, 6, 7),
    "matDif": (8,),
    "matAmb": (9,),
    "TexMtx0": (10, 11, 12),
    "TexMtx1": (14, 15, 16),
    "TexMtx2": (17, 18, 19),
    "uInvView": (76, 77, 78, 79),
    "TexCoordSlot+ShaderMode": (89,),
    "VertexAttributeScale0": (90,),
    "VertexAttributeScale1": (91,),
    "TexMappingMethod": (92,),
    "MappingConstants": (93, 94, 95),
}

# Read the block table above as a NAMES-INDEXES map, not as a universal one.
#
# A float uniform index means whatever the material's C program assigned to it. c92 is TexMappingMethod
# for the title wordmark's layout and reads 3.0 there; on the cached gameplay draws c92 reads
# 1065353216.0, which is the honest float32 value of the word 0x4e7e0000 and is not a mapping method.
# So the same index carries different quantities for different materials, and a capture that spans an
# index proves the oracle WROTE it -- not that it wrote the quantity this table names.
#
# Consequence, and the reason this survey is a capture planner rather than an answer key: only a
# capture whose material layout is identified may be read through this table. The title capture
# qualifies (its layout is the recovered one); the gameplay command lists do not, because nothing in
# their provenance records the material. Do not report "a capture spans c89" as "TexCoordSlot was
# measured".
# An "answerable" question is still only answered over the captures that could answer it. Where one
# capable capture exists, say what the resulting claim may and may not cover.
ANSWER_DENOMINATOR_NOTES: dict[str, str] = {
    "TexCoordSlot ever non-zero per coordinator": (
        "only the title wordmark material qualifies, so this can answer 'the title's coordinators' "
        "and NOT 'ever' -- an ever-claim needs a second, different material's capture"
    ),
    "coordinator-1 sub@300 per-vertex step": (
        "one capable capture is one draw; the per-vertex (x==0 ? x-32 : x+32) branch needs draws on "
        "both sides of x==0 to be observable"
    ),
}

INDEX_SCOPE_NOTE = (
    "float-uniform indices are PER-MATERIAL: c92 is TexMappingMethod for the title wordmark layout "
    "and reads 3.0 there, while the cached gameplay draws read 1065353216.0 at c92 (the correct "
    "float32 value of 0x4e7e0000, not a mapping method). Spanning a block proves the oracle wrote "
    "that index, not which quantity it carried; only a capture with an identified material layout may "
    "be read through UNIFORM_BLOCKS."
)

def _model_view_rows_are_identity(uniforms) -> bool:
    """True when uModelView (c4..c6) is the identity 3x3 -- the title capture's actual value.

    The point of question (1) is a draw whose model-view rows are NOT identity, because on every
    title draw the distinguishing part sits in row 3, which the shader never reads. A capture that
    only ever spans the block therefore cannot answer it, and saying otherwise would report the
    title as closing a question it is degenerate for.
    """
    for row in range(3):
        vector = uniforms.get(4 + row)
        if vector is None:
            return True  # absent: treat as unusable, not as non-identity
        for column in range(3):
            expected = 1.0 if row == column else 0.0
            if abs(vector[column] - expected) > 1e-4:
                return False
    return True


# What a capture has to provide before UNIFORM_BLOCKS may be read through it. Each clause below is a
# measured or source-read fact, not a precaution.
#
# 1. MATERIAL IDENTITY. Azahar's per-draw vsuni log (Azahar/src/video_core/pica/pica_core.cpp, the
#    `draw n=%d idx=%d ...` fprintf) prints `idx` as `(int)is_indexed` -- the PICA indexed-draw flag,
#    NOT a material. Nothing in that log names the bound C material, and the C program is what
#    decides what each float-uniform index means. So this is a requirement on the INSTRUMENTATION, not
#    just on the probe record: a capture cannot be read through the block table until the logger
#    carries the material.
# 2. SPAN. The named blocks must actually be written; `MappingConstants` (c93..c95) is written by no
#    capture in the current corpus, so the method-4 question cannot be reached at all today.
# 3. A USEFUL VALUE. A capture can span a block and still be degenerate for the question, which is
#    why UNIFORM_QUESTIONS carries value checks.
CAPTURE_REQUIREMENTS: tuple[str, ...] = (
    "material identity in the per-draw log (today `idx` is is_indexed, the PICA indexed-draw flag)",
    "the named blocks actually written (MappingConstants c93..c95: written by no cached capture)",
    "values non-degenerate for the question (an identity model-view answers nothing about a "
    "non-identity model-view)",
)

# An open question, the blocks a capture must span, where it is tracked, and an optional VALUE check.
# The value check is what stops "the index is present" from being reported as "the question is
# answerable": question (1) needs a non-identity model-view, and the only layout-identified capture
# has an identity one, so the corpus cannot answer it. Kept here rather than in the doc so the survey
# and the frontier cannot disagree about what a capture has to contain.
UNIFORM_QUESTIONS: dict[str, tuple[tuple[str, ...], str, object]] = {
    "uInvView equals inverse(view) on a non-identity model-view draw": (
        ("uModelView", "uInvView"),
        "render.cmb-texcoord-mapping remaining (1)",
        lambda uniforms: not _model_view_rows_are_identity(uniforms),
    ),
    "TexCoordSlot ever non-zero per coordinator": (
        ("TexCoordSlot+ShaderMode",),
        "render.cmb-texcoord-mapping remaining (4)",
        None,
    ),
    "coordinator-1 sub@300 per-vertex step": (
        ("TexMtx1", "VertexAttributeScale1"),
        "render.cmb-texcoord-mapping remaining (3)",
        None,
    ),
    "ProjectionMap method 4 operand values": (
        ("TexMappingMethod", "MappingConstants", "uInvView"),
        "render.cmb-texcoord-mapping remaining (2)",
        None,
    ),
}


@dataclass
class CaptureCoverage:
    """One cached command list, and which uniform blocks its draw actually wrote."""

    cache_key: str
    probe_path: Path
    artifact_path: Path
    end_word: int
    draw: object
    present: frozenset[str]
    uniform_count: int
    error: str | None = None
    # True only when the probe records enough provenance to identify the material whose C program
    # defines what each float-uniform index means. Without it the block names above are guesses.
    layout_identified: bool = False
    # The decoded array, kept so a question's VALUE check can reject a capture whose values are
    # degenerate for it. None when the capture failed to decode.
    uniforms: object | None = None

    def spans(self, blocks: tuple[str, ...]) -> bool:
        """Whether this capture wrote every index in `blocks` -- NOT that it can answer a question."""
        return all(block in self.present for block in blocks)

    def can_answer(self, blocks: tuple[str, ...], value_check=None) -> bool:
        """Whether the capture spans the blocks, names a material, and holds usable VALUES.

        All three, because each has already been mistaken for the others: a capture can write the
        index without naming the material, and it can name the material while holding values that are
        degenerate for the question (an identity model-view for question 1).
        """
        if not (self.layout_identified and self.spans(blocks)):
            return False
        if value_check is not None:
            return self.uniforms is not None and bool(value_check(self.uniforms))
        return True


@dataclass
class CoverageReport:
    """The survey result: every capture it looked at, and what the corpus can answer."""

    captures: list[CaptureCoverage] = field(default_factory=list)
    scanned_probes: int = 0
    unreadable: int = 0

    def answered(self) -> dict[str, list[CaptureCoverage]]:
        """Questions at least one cached capture can actually answer.

        Requires an identified material layout, not just the indices being present -- see
        INDEX_SCOPE_NOTE. `spanning()` is the weaker check a capture planner wants.
        """
        result: dict[str, list[CaptureCoverage]] = {}
        for question, (blocks, _tracker, value_check) in UNIFORM_QUESTIONS.items():
            capable = [
                capture for capture in self.captures if capture.can_answer(blocks, value_check)
            ]
            if capable:
                result[question] = capable
        return result

    def spanning(self) -> dict[str, list[CaptureCoverage]]:
        """Questions whose blocks at least one capture writes, layout identified or not."""
        result: dict[str, list[CaptureCoverage]] = {}
        for question, (blocks, _tracker, _value_check) in UNIFORM_QUESTIONS.items():
            hits = [capture for capture in self.captures if capture.spans(blocks)]
            if hits:
                result[question] = hits
        return result

    def blocked(self) -> list[tuple[str, tuple[str, ...], str]]:
        """Questions NO cached capture can answer, with the blocks a capture would have to span."""
        return [
            (question, blocks, tracker)
            for question, (blocks, tracker, value_check) in UNIFORM_QUESTIONS.items()
            if not any(capture.can_answer(blocks, value_check) for capture in self.captures)
        ]


def _probe_command_list(probe: dict) -> tuple[Path | None, int | None, object]:
    """Pull (artifact, end_word, draw) out of a cached command-list probe record."""
    end_word = probe.get("command_list_word_index")
    artifact = probe.get("command_list_artifact") or probe.get("artifact")
    if end_word is None or not artifact:
        return None, None, probe.get("draw")
    return Path(artifact), int(end_word), probe.get("draw")


def _layout_identified(probe_path: Path, record: dict) -> bool:
    """Whether the probe names the material whose C program defines these uniform indices.

    The title capture does (`title_oracle_probe` records the title material); the bulk gameplay
    command lists do not, which is exactly why they cannot be read through UNIFORM_BLOCKS.
    """
    if "title" in probe_path.name:
        return True
    return any(key in record for key in ("material", "material_name", "cmb", "layout"))


def coverage_for_probe(probe_path: Path, record: dict) -> CaptureCoverage | None:
    """Decode one probe's command list into block coverage, or None when it is not one."""
    artifact, end_word, draw = _probe_command_list(record)
    if artifact is None:
        return None
    cache_key = probe_path.parent.parent.name
    layout = _layout_identified(probe_path, record)
    if not artifact.exists():
        return CaptureCoverage(cache_key, probe_path, artifact, end_word or 0, draw, frozenset(), 0,
                               "artifact missing", layout)
    try:
        uniforms = decode_vertex_uniforms(
            parse_command_writes(artifact.read_bytes(), end_word), end_word
        )
    except Exception as error:  # a malformed capture is a finding, not a crash
        return CaptureCoverage(cache_key, probe_path, artifact, end_word or 0, draw, frozenset(), 0,
                               str(error)[:120], layout)
    present = frozenset(
        name for name, indices in UNIFORM_BLOCKS.items() if all(uniforms.get(i) is not None for i in indices)
    )
    return CaptureCoverage(cache_key, probe_path, artifact, end_word or 0, draw, present,
                           len(uniforms.written), None, layout, uniforms)


def survey(cache_root: Path) -> CoverageReport:
    """Walk every cached command-list probe under `cache_root`.

    Raises RuntimeError when the corpus contains none: an empty survey must not read as "no capture
    can answer anything", which is the answer a reader would otherwise assume.
    """
    report = CoverageReport()
    if not cache_root.is_dir():
        raise RuntimeError(f"missing oracle cache root: {cache_root}")
    for probe_path in sorted(cache_root.glob("*/probes/*.json")):
        try:
            record = json.loads(probe_path.read_text())
        except (OSError, json.JSONDecodeError):
            report.unreadable += 1
            continue
        if not isinstance(record, dict) or "command_list_word_index" not in record:
            continue
        report.scanned_probes += 1
        coverage = coverage_for_probe(probe_path, record)
        if coverage is not None:
            if coverage.error:
                report.unreadable += 1
            report.captures.append(coverage)
    if not report.captures:
        raise RuntimeError(
            f"no decodable command-list captures under {cache_root}; "
            "warm one with tools/oracle_cache.py before reading this survey"
        )
    return report


def format_report(report: CoverageReport, cache_root: Path) -> str:
    lines = [
        f"CmbVShader uniform coverage over {cache_root}",
        f"scanned {report.scanned_probes} command-list probe(s), decoded {len(report.captures)}, "
        f"unreadable {report.unreadable}",
        "",
        "per capture (blocks written / total blocks):",
    ]
    block_names = list(UNIFORM_BLOCKS)
    for capture in report.captures:
        marks = "".join("." if name in capture.present else "-" for name in block_names)
        note = f"  [{capture.error}]" if capture.error else ""
        lines.append(
            f"  {capture.cache_key[:42]:44s} draw={str(capture.draw):>5s} "
            f"uniforms={capture.uniform_count:>3d} {len(capture.present)}/{len(block_names)} {marks}{note}"
        )
    lines.append("")
    lines.append("block order: " + " ".join(block_names))
    lines.append("")
    lines.append("NOTE " + INDEX_SCOPE_NOTE)
    lines.append("")
    lines.append("what a capture must provide before it may be read through the block table:")
    for requirement in CAPTURE_REQUIREMENTS:
        lines.append(f"  - {requirement}")
    lines.append("")
    lines.append("questions a cached capture can ANSWER (spans the blocks AND names a material):")
    answered = report.answered()
    if not answered:
        lines.append("  (none)")
    for question, capable in answered.items():
        lines.append(f"  [yes] {question}")
        for capture in capable:
            lines.append(f"         via {capture.cache_key[:42]} draw={capture.draw}")
        denominator = ANSWER_DENOMINATOR_NOTES.get(question)
        if denominator:
            lines.append(f"         DENOMINATOR: {denominator}")
    lines.append("")
    lines.append("questions whose BLOCKS some capture writes (NOT answerable without a layout):")
    spanning = report.spanning()
    if not spanning:
        lines.append("  (none)")
    for question, hits in spanning.items():
        identified = sum(1 for capture in hits if capture.layout_identified)
        lines.append(
            f"  [span] {question}  ({len(hits)} capture(s), {identified} with an identified layout)"
        )
    lines.append("")
    lines.append("questions NO cached capture can answer (a capture requirement, not a re-derivation):")
    blocked = report.blocked()
    if not blocked:
        lines.append("  (none)")
    for question, blocks, tracker in blocked:
        lines.append(f"  [NO ] {question}")
        lines.append(f"         needs {list(blocks)} + an identified material  ({tracker})")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--cache-root",
        type=Path,
        default=REPO / "scratch" / "oracle_cache",
        help="oracle cache root to survey",
    )
    parser.add_argument("--json", action="store_true", help="emit the report as JSON")
    args = parser.parse_args(argv)
    try:
        report = survey(args.cache_root)
    except RuntimeError as error:
        print(f"cmb_shader_uniform_coverage: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(
            json.dumps(
                {
                    "scanned_probes": report.scanned_probes,
                    "decoded": len(report.captures),
                    "unreadable": report.unreadable,
                    "captures": [
                        {
                            "cache_key": capture.cache_key,
                            "draw": capture.draw,
                            "uniforms": capture.uniform_count,
                            "present": sorted(capture.present),
                            "layout_identified": capture.layout_identified,
                            "error": capture.error,
                        }
                        for capture in report.captures
                    ],
                    "answerable": sorted(report.answered()),
                    "spanning": {
                        question: len(hits) for question, hits in report.spanning().items()
                    },
                    "blocked": [question for question, _blocks, _tracker in report.blocked()],
                },
                indent=1,
            )
        )
        return 0
    print(format_report(report, args.cache_root))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
