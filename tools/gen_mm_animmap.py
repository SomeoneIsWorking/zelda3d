#!/usr/bin/env python3
"""Generate N64-animation to MM3D-CSAB mapping tables and coverage reports.

Fully offline: reads 2ship object assets and ``$ZELDA3D_MM3D_ROM``. Archive
decoding, source inventory, matching, output rendering, and evidence
verification live in focused sibling modules; this file composes the pipeline.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

from mm_animmap_archive import (
    Mm3dActors,
)
from mm_animmap_c_tables import emit_default_inc, emit_inc
from mm_animmap_inventory import (
    all_object_dirs,
    n64_anims,
    object_to_gar,
    xml_original_names,
    xml_texture_anims,
)
from mm_animmap_matching import (
    ACCEPT,
    Match,
    match_anims,
)
from mm_animmap_paths import REPO, load_env
from mm_animmap_report import build_report
from mm_animmap_types import ActorResult
from mm_animmap_verify import verify_overrides

_load_env = load_env


def _alt_gar_hint(obj: str, gar: str, actors: Mm3dActors) -> str | None:
    stem = re.sub(r"^(object_|obj_|gameplay_)", "", obj.lower())
    bases = {re.sub(r"\d+$", "", stem), stem.split("_")[0]}
    for base in sorted(bases):
        for candidate in ("zelda2_" + base, "zelda_" + base, base):
            if candidate != gar and actors.clips(candidate):
                return candidate
    return None


def build(
    only: str | None = None, accept: float = ACCEPT
) -> tuple[list[ActorResult], dict]:
    animations = n64_anims()
    if only:
        animations = {key: value for key, value in animations.items() if key == only}
        if not animations:
            raise SystemExit(f"no animation symbols for object {only!r}")

    actors = Mm3dActors()
    known = set(actors.actors)
    original_names = xml_original_names()
    texture_animations = xml_texture_anims()
    results: list[ActorResult] = []
    for obj in sorted(animations):
        candidates = object_to_gar(obj, known)
        gar = candidates[0] if candidates else None
        clips = (actors.clips(gar) if gar else []) or []
        if clips:
            matches = match_anims(
                animations[obj],
                clips,
                obj,
                accept,
                original_names.get(obj),
                texture_animations.get(obj),
            )
        else:
            reason = "no GAR" if not gar else "GAR has no CSAB clips"
            matches = [Match(symbol, None, 0.0, reason) for symbol in animations[obj]]
        hint = _alt_gar_hint(obj, gar, actors) if gar and not clips else None
        results.append(ActorResult(obj, gar, tuple(clips), tuple(matches), hint))

    meta = {
        "rom": getattr(actors.rom, "product_code", "MM3D"),
        "actor_gars_in_rom": len(known),
        "object_dirs_total": len(all_object_dirs()),
        "objects_with_anims": len(animations),
        "min_confidence": accept,
    }
    return results, meta


def _write_outputs(
    output: str, report_stem: str, results: list[ActorResult], meta: dict
) -> dict:
    include = emit_inc(results, meta)
    markdown, report = build_report(results, meta)
    for destination in (output, report_stem + ".md", report_stem + ".json"):
        os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(output, "w") as include_file:
        include_file.write(include)

    default_include, unknown = emit_default_inc(results)
    if os.path.basename(output) == "mm3d_animmap.inc":
        default_output = os.path.join(os.path.dirname(output), "mm3d_defaultanim.inc")
    else:
        default_output = os.path.splitext(output)[0] + "_defaultanim.inc"
    with open(default_output, "w") as default_file:
        default_file.write(default_include)
    default_count = default_include.count("\n    { ")
    print(f"default-idle table: {default_count} actors -> {default_output}")
    if unknown:
        unknown_names = ", ".join(unknown)
        print(
            f"  NO determinable idle for {len(unknown)} actor(s) -- these keep the runtime's arbitrary "
            f"first-CSAB fallback, which is a KNOWN GAP, not a choice: {unknown_names}"
        )

    with open(report_stem + ".md", "w") as markdown_file:
        markdown_file.write(markdown)
    with open(report_stem + ".json", "w") as json_file:
        json.dump(report, json_file, indent=2)
    return report


def _print_summary(report: dict, output: str, report_stem: str) -> None:
    meta = report["meta"]
    print(
        f"objects with anims: {meta['objects_with_anims']}  "
        f"(resolved to a GAR: {meta['objects_with_gar']}, no GAR: {meta['objects_without_gar']})"
    )
    matched_percent = 100.0 * meta["symbols_matched"] / max(1, meta["symbols_total"])
    print(
        f"symbols: {meta['symbols_total']}  matched: {meta['symbols_matched']} "
        f"({matched_percent:.1f}%)  unmatched: {meta['symbols_unmatched']}"
    )
    for reason, count in sorted(
        report["unmatched_reasons"].items(), key=lambda item: -item[1]
    ):
        print(f"  {count:5d}  {reason}")
    print(
        f"wrote {os.path.relpath(output, REPO)}, {os.path.relpath(report_stem, REPO)}.md, {os.path.relpath(report_stem, REPO)}.json"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--only", help="restrict to a single N64 object dir (e.g. object_dog)"
    )
    parser.add_argument(
        "--min-confidence",
        type=float,
        default=ACCEPT,
        help=f"acceptance threshold (default {ACCEPT:.2f})",
    )
    parser.add_argument(
        "--out",
        default=os.path.join("scratch", "mm_animmap.inc"),
        help="C entries output (default scratch/mm_animmap.inc)",
    )
    parser.add_argument(
        "--report",
        default=os.path.join("scratch", "mm_animmap_report"),
        help="report path stem; writes <stem>.md and <stem>.json",
    )
    parser.add_argument(
        "--verify-overrides",
        action="store_true",
        help="re-derive VERIFIED_OVERRIDES from the assets and exit",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    load_env()
    args = parse_args(argv)
    if args.verify_overrides:
        return verify_overrides()

    results, meta = build(args.only, args.min_confidence)
    output = args.out if os.path.isabs(args.out) else os.path.join(REPO, args.out)
    report_stem = (
        args.report if os.path.isabs(args.report) else os.path.join(REPO, args.report)
    )
    report = _write_outputs(output, report_stem, results, meta)
    _print_summary(report, output, report_stem)
    return 0


if __name__ == "__main__":
    sys.exit(main())
