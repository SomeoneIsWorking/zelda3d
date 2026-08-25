"""JSON and Markdown report rendering for MM animation-map coverage."""

from __future__ import annotations

from collections.abc import Mapping, Sequence

from mm_animmap_coverage import CoverageBucket, CoverageSummary, classify_coverage
from mm_animmap_types import ActorResult

_RULE_TAGS = {
    "XML annotation naming an existing clip": "names an existing clip",
    "XML annotation + unique numeric variant": "unique numeric variant",
    "verified override (measured, cited)": "verified override",
    "annotated ABSENT from MM3D (suppresses guessing)": "absent from MM3D",
    "<TextureAnimation> exclusion": "<TextureAnimation>",
}


def _reason_bucket(why: str) -> str:
    if why.startswith("no candidate shares"):
        return "no shared token (romaji vs english vocabulary gap)"
    if why.startswith("best "):
        return "weak partial overlap (below min-confidence)"
    if why.startswith("non-skeletal"):
        return "non-skeletal symbol (Tex/UV/eye/mouth)"
    if why.startswith("ambiguous"):
        return "ambiguous tie between clips"
    if why == "no GAR":
        return "object has no MM3D actor GAR"
    return why


def _rule_firings(results: Sequence[ActorResult], matched: int) -> dict[str, int]:
    rules = {tag: 0 for tag in _RULE_TAGS}
    for result in results:
        for match in result.matches:
            for tag, marker in _RULE_TAGS.items():
                if marker in match.why:
                    rules[tag] += 1
    rules["token/synonym heuristic"] = matched - sum(
        rules[tag]
        for tag in _RULE_TAGS
        if tag.startswith(("XML annotation", "verified override"))
    )
    return rules


def _json_report(
    results: Sequence[ActorResult],
    meta: Mapping[str, object],
    coverage: CoverageSummary,
) -> dict:
    symbols = sum(len(result.matches) for result in results)
    matched = sum(1 for result in results for match in result.matches if match.clip)
    with_gar = [result for result in results if result.gar]
    reasons: dict[str, int] = {}
    for result in results:
        for match in result.matches:
            if not match.clip:
                reason = _reason_bucket(match.why)
                reasons[reason] = reasons.get(reason, 0) + 1

    return {
        "meta": dict(
            meta,
            symbols_total=symbols,
            symbols_matched=matched,
            symbols_unmatched=symbols - matched,
            objects_with_gar=len(with_gar),
            objects_without_gar=len(results) - len(with_gar),
        ),
        "unmatched_reasons": reasons,
        "rule_firings": _rule_firings(results, matched),
        "actors": [
            {
                "object": result.obj,
                "gar": result.gar,
                "clips": len(result.clips),
                "clip_names": sorted(result.clips),
                "alt_gar_hint": result.alt_gar_hint,
                "symbols": len(result.matches),
                "matched": [
                    {
                        "n64otr": match.symbol,
                        "csab": match.clip,
                        "confidence": match.confidence,
                        "why": match.why,
                    }
                    for match in result.matches
                    if match.clip
                ],
                "unmatched": [
                    {
                        "n64otr": match.symbol,
                        "confidence": match.confidence,
                        "why": match.why,
                    }
                    for match in result.matches
                    if not match.clip
                ],
            }
            for result in results
        ],
        "ungating_coverage": {
            "full": list(coverage.full),
            "partial": list(coverage.partial),
            "zero": list(coverage.zero),
            "excluded": coverage.excluded,
        },
    }


def _coverage_markdown(coverage: CoverageSummary) -> list[str]:
    lines = [
        "",
        "## Un-gating coverage (skeletal symbols only, actors that can use the skinned path)",
        "",
        "| bucket | actors | meaning |",
        "| --- | --- | --- |",
        f"| FULL | {len(coverage.full)} | every mappable skeletal animation resolves to a clip; safe to un-gate |",
        f"| PARTIAL | {len(coverage.partial)} | animates for some actions, idles for others -- the un-gating risk |",
        f"| ZERO | {len(coverage.zero)} | has clips but nothing matched; behaves as idle throughout |",
        f"| (excluded) | {coverage.excluded} | no GAR, or a GAR with no CSAB clips -- cannot take the path |",
        "",
    ]
    partial = [
        item for item in coverage.actors if item.bucket is CoverageBucket.PARTIAL
    ]
    if partial:
        lines += [
            "Partial actors, worst first:",
            "",
            "| object | GAR | clips | mapped | UNMAPPED (mappable skeletal) |",
            "| --- | --- | --- | --- | --- |",
        ]
        for item in sorted(partial, key=lambda actor: -len(actor.mappable_unmatched)):
            lines.append(
                f"| {item.result.obj} | {item.result.gar} | {len(item.result.clips)} | "
                f"{item.matched} | {len(item.mappable_unmatched)} |"
            )
        lines.append("")
    if coverage.zero:
        lines += [
            "Zero-mapped actors (have clips, matched nothing): "
            + ", ".join(f"`{obj}`" for obj in coverage.zero),
            "",
        ]
    return lines


def _markdown_report(
    results: Sequence[ActorResult], report: dict, coverage: CoverageSummary
) -> str:
    meta = report["meta"]
    matched_percent = 100.0 * meta["symbols_matched"] / max(1, meta["symbols_total"])
    matched_row = (
        f"| symbols matched (conf >= {meta['min_confidence']:.2f}) | "
        f"{meta['symbols_matched']} ({matched_percent:.1f}%) |"
    )
    lines = [
        "# kMMAnimMaps coverage report",
        "",
        "Generated by `tools/gen_mm_animmap.py` (offline: repo assets + MM3D ROM `{}`).".format(
            meta["rom"]
        ),
        "",
        "| metric | value |",
        "| --- | --- |",
        f"| N64 object dirs | {meta['object_dirs_total']} |",
        f"| objects with animation symbols | {meta['objects_with_anims']} |",
        f"| MM3D `/actors/` GARs in ROM | {meta['actor_gars_in_rom']} |",
        f"| anim-bearing objects resolved to a GAR | {meta['objects_with_gar']} |",
        f"| anim-bearing objects with NO GAR | {meta['objects_without_gar']} |",
        f"| animation symbols total | {meta['symbols_total']} |",
        matched_row,
        f"| symbols unmatched | {meta['symbols_unmatched']} |",
        "",
        f"## Rule firings (of {meta['symbols_total']} symbols)",
        "",
        "| rule | fired |",
        "| --- | --- |",
    ]
    lines += [f"| {rule} | {count} |" for rule, count in report["rule_firings"].items()]
    lines += ["", "## Unmatched reasons", "", "| reason | count |", "| --- | --- |"]
    lines += [
        f"| {reason} | {count} |"
        for reason, count in sorted(
            report["unmatched_reasons"].items(), key=lambda item: -item[1]
        )
    ]
    lines += [
        "",
        "## Per actor",
        "",
        "| object | GAR | clips | symbols | matched | unmatched |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for result in sorted(
        results,
        key=lambda item: (-sum(1 for match in item.matches if match.clip), item.obj),
    ):
        matched = sum(1 for match in result.matches if match.clip)
        lines.append(
            f"| {result.obj} | {result.gar or '(none)'} | {len(result.clips)} | "
            f"{len(result.matches)} | {matched} | {len(result.matches) - matched} |"
        )
    lines += _coverage_markdown(coverage)

    no_gar = [result for result in results if not result.gar]
    if no_gar:
        lines += [
            "",
            "## Anim-bearing objects with no MM3D actor GAR",
            "",
            ", ".join(f"`{result.obj}`" for result in no_gar),
            "",
        ]
    no_clips = [result for result in results if result.gar and not result.clips]
    if no_clips:
        lines += [
            "",
            "## Resolved to a GAR that contains NO CSAB clips",
            "",
            "| object | GAR | symbols | same-family hint |",
            "| --- | --- | --- | --- |",
        ]
        for result in sorted(no_clips, key=lambda item: -len(item.matches)):
            lines.append(
                f"| {result.obj} | {result.gar} | {len(result.matches)} | "
                f"{result.alt_gar_hint or '-'} |"
            )
        lines.append("")
    return "\n".join(lines) + "\n"


def build_report(
    results: Sequence[ActorResult], meta: Mapping[str, object]
) -> tuple[str, dict]:
    coverage = classify_coverage(results)
    report = _json_report(results, meta, coverage)
    return _markdown_report(results, report, coverage), report
