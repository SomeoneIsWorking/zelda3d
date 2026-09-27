#!/usr/bin/env python3
"""Which texture filter and wrap enums both retail games use, and whether the host resolves each.

Texture filtering and wrapping are named scope alongside formats, and like formats they were
unmeasured. This is the sampler-state half of that check: it walks every bound texture unit of every
material in both games and reports the distinct `min_filter` / `mag_filter` / `wrap_s` / `wrap_t`
values with counts, then names the GL enum behind each so the report is readable without a GL header
open beside it.

The verdict is deliberately narrow. Resolving an enum correctly is the host's business and is pinned
in C++ (`Zelda3DSamplerFilterResolution.*`); what this tool answers is the different question of
**which values the content actually uses**, so that a future enum added to the host's table can be
checked against real demand rather than assumed necessary, and so a value appearing in content that
nobody has reasoned about is visible instead of silently defaulting.

It covers all three texture units per material, not just unit 0. Unit 1 and 2 exist because
`render.multi-stage-tev` combines tex0+tex1+tex2 (Zora's water is the three-stage case), and a
per-unit sampler bug would only show up there.

Usage:
    source .env
    tools/pica_sampler_state_survey.py
    tools/pica_sampler_state_survey.py --game mm --json
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from cmb_corpus import iter_corpus  # noqa: E402
from tev_corpus_survey import material_chunk_pointer  # noqa: E402

# The GL ES enums these fields hold. CMB material texture bindings store them verbatim, so naming them
# here is what makes a hex count readable. Values outside this table are reported as UNNAMED rather
# than guessed at -- an unnamed enum in retail content is a finding, not a formatting problem.
GL_NAMES: dict[int, str] = {
    0x2600: "GL_NEAREST",
    0x2601: "GL_LINEAR",
    0x2700: "GL_NEAREST_MIPMAP_NEAREST",
    0x2701: "GL_LINEAR_MIPMAP_NEAREST",
    0x2702: "GL_NEAREST_MIPMAP_LINEAR",
    0x2703: "GL_LINEAR_MIPMAP_LINEAR",
    0x2900: "GL_CLAMP",
    0x2901: "GL_REPEAT",
    0x812D: "GL_CLAMP_TO_BORDER",
    0x812F: "GL_CLAMP_TO_EDGE",
    0x8370: "GL_MIRRORED_REPEAT",
}
TEXTURE_UNITS = 3
BINDING_STRIDE = 0x18

# The enums `Fast::ResolveZelda3DSamplerFilter` (Shipwright/libultraship/include/fast/zelda3d_sampler.h)
# and `Fast::wrapMode` (zelda3d_sdl3gpu_resources.cpp) handle. Kept as data so this tool can say what
# the host covers without re-implementing either: the C++ side is pinned by its own tests against real
# values, and the point here is demand-vs-coverage, not a second evaluation of the logic.
HOST_COVERED_MIN = {0x2600, 0x2601, 0x2700, 0x2701, 0x2702, 0x2703}
HOST_COVERED_MAG = {0x2600, 0x2601}
HOST_COVERED_WRAP = {0x2900, 0x2901, 0x812F, 0x8370}


@dataclass
class SamplerStateReport:
    """Per-axis enum histograms across every bound texture unit of one game's materials."""

    game: str
    materials_with_tex0: int = 0
    units_bound: int = 0
    min_filter: Counter = field(default_factory=Counter)
    mag_filter: Counter = field(default_factory=Counter)
    wrap_s: Counter = field(default_factory=Counter)
    wrap_t: Counter = field(default_factory=Counter)

    def unnamed(self) -> Counter:
        """Any enum in retail content that this tool has no name for."""
        out: Counter = Counter()
        for axis in (self.min_filter, self.mag_filter, self.wrap_s, self.wrap_t):
            for value, count in axis.items():
                if value not in GL_NAMES:
                    out[value] += count
        return out

    def uncovered(self) -> Counter:
        """Enums the content uses that the host's tables do not cover."""
        out: Counter = Counter()
        for value, count in self.min_filter.items():
            if value not in HOST_COVERED_MIN:
                out[value] += count
        for value, count in self.mag_filter.items():
            if value not in HOST_COVERED_MAG:
                out[value] += count
        for axis in (self.wrap_s, self.wrap_t):
            for value, count in axis.items():
                if value not in HOST_COVERED_WRAP:
                    out[value] += count
        return out


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _s16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def scan(game: str) -> SamplerStateReport:
    """Every bound texture unit of every material in `game`'s CMB corpus.

    Uses the shared `material_chunk_pointer`, so MM3D's version>=7 header shift is handled by the one
    owner rather than re-derived -- reading 0x28 unconditionally pointed at MM3D's `qtrs` chunk and
    silently produced zero materials, which is the same class of failure this project has already paid
    for once.
    """
    report = SamplerStateReport(game=game)
    for _label, data in iter_corpus(game)():
        mats = material_chunk_pointer(data)
        if mats is None:
            continue
        stride = 0x15C if _u32(data, 0x08) <= 6 else 0x16C
        for index in range(_u32(data, mats + 8)):
            base = mats + 0x0C + index * stride
            if base + 0x10 + BINDING_STRIDE * TEXTURE_UNITS > len(data):
                continue
            if _s16(data, base + 0x10) < 0:
                continue  # unit 0 unbound: this material has no texture sampler at all
            report.materials_with_tex0 += 1
            for unit in range(TEXTURE_UNITS):
                binding = base + 0x10 + BINDING_STRIDE * unit
                if _s16(data, binding) < 0:
                    continue  # this unit is not bound; its fields are not sampler state
                report.units_bound += 1
                report.min_filter[_u16(data, binding + 4)] += 1
                report.mag_filter[_u16(data, binding + 6)] += 1
                report.wrap_s[_u16(data, binding + 8)] += 1
                report.wrap_t[_u16(data, binding + 0x0A)] += 1
    if not report.units_bound:
        raise RuntimeError(
            f"game={game}: no bound texture units found; the scan is broken, not clean"
        )
    return report


def _axis(name: str, counts: Counter, covered: set[int]) -> list[str]:
    rows = [f"  {name}:"]
    for value, count in sorted(counts.items(), key=lambda item: (-item[1], item[0])):
        label = GL_NAMES.get(value, "UNNAMED")
        mark = "" if value in covered else "   <- not in the host's table"
        rows.append(f"    0x{value:04X} {label:28s} {count:7d}{mark}")
    return rows


def format_report(report: SamplerStateReport) -> str:
    lines = [
        f"== {report.game} ==",
        f"{report.materials_with_tex0} materials with a bound unit 0,"
        f" {report.units_bound} bound texture units across {TEXTURE_UNITS} units each",
        "",
    ]
    lines += _axis("min_filter", report.min_filter, HOST_COVERED_MIN)
    lines += _axis("mag_filter", report.mag_filter, HOST_COVERED_MAG)
    lines += _axis("wrap_s", report.wrap_s, HOST_COVERED_WRAP)
    lines += _axis("wrap_t", report.wrap_t, HOST_COVERED_WRAP)
    lines.append("")
    unnamed = report.unnamed()
    uncovered = report.uncovered()
    if unnamed:
        lines.append(f"UNNAMED enums in retail content: { {hex(k): v for k, v in unnamed.items()} }")
    else:
        lines.append("every enum in this game's content is a named GL sampler enum")
    if uncovered:
        lines.append(f"NOT COVERED by the host's tables: { {hex(k): v for k, v in uncovered.items()} }")
    else:
        lines.append("every enum in this game's content is covered by the host's resolution tables")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", choices=("oot", "mm", "both"), default="both")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    games = ("oot", "mm") if args.game == "both" else (args.game,)
    try:
        reports = {game: scan(game) for game in games}
    except (RuntimeError, ValueError) as error:
        print(f"pica_sampler_state_survey: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(
            json.dumps(
                {
                    game: {
                        "materials_with_tex0": report.materials_with_tex0,
                        "units_bound": report.units_bound,
                        "min_filter": {hex(k): v for k, v in sorted(report.min_filter.items())},
                        "mag_filter": {hex(k): v for k, v in sorted(report.mag_filter.items())},
                        "wrap_s": {hex(k): v for k, v in sorted(report.wrap_s.items())},
                        "wrap_t": {hex(k): v for k, v in sorted(report.wrap_t.items())},
                        "unnamed": {hex(k): v for k, v in report.unnamed().items()},
                        "uncovered": {hex(k): v for k, v in report.uncovered().items()},
                    }
                    for game, report in reports.items()
                },
                indent=1,
            )
        )
        return 0
    for game in games:
        print(format_report(reports[game]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
