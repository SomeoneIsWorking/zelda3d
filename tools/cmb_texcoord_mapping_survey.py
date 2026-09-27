#!/usr/bin/env python3
"""Survey CMB coordinator mapping methods against the two lighting flags.

`oot3d-decomp/docs/cmb_texcoord_mapping.md` recovers that OoT3D's `CmbVShader`
only contains the texture-coordinator mapping switch in the body that also
carries the `IsVertexLighting` / `IsFragmentLighting` paths (body@14); the other
body (body@214) has no lighting block and always takes the plain coordinate. The
entry point picks between them with the single `ShaderMode.w` uniform.

The host does not transport `ShaderMode.w`, but it does parse both lighting bytes,
so "this draw was lit" is data the host already has. This survey answers the
question that decides whether the host may gate the mapping on lit-ness: for every
retail material, what is its coordinator mapping method, and is it lit?

It also separates the units a combiner actually samples from units that merely
declare a texture, because only a sampled unit can change a pixel. Never starts
the oracle.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter

from cmb_corpus import iter_cmbs
from tev_corpus_survey import parse_material_records, slots_used

TEXTURE_SOURCES = (0x84C0, 0x84C1, 0x84C2, 0x84C3)
MAPPED_METHODS = (3, 4)

METHOD_NAMES = {
    0: "None",
    1: "UvCoordinateMap",
    2: "CameraCubeEnvMap",
    3: "CameraSphereEnvMap",
    4: "ProjectionMap",
    5: "SphereMap",
}


def consumed_units(stages) -> set[int]:
    units = set()
    for stage in stages:
        for op, srcs in ((stage.rgb_op, stage.rgb_src), (stage.a_op, stage.a_src)):
            for index in range(slots_used(op)):
                value = srcs[index]
                if TEXTURE_SOURCES[0] <= value <= TEXTURE_SOURCES[3]:
                    units.add(value - TEXTURE_SOURCES[0])
    return units


def lighting_kind(record) -> str:
    """Name the record's lighting combination; `-` is the unlit bucket's own key."""
    if record.fragment_lighting and record.vertex_lighting:
        return "both"
    if record.vertex_lighting:
        return "vertex-only"
    if record.fragment_lighting:
        return "fragment-only"
    return "-"


def survey(label_filter: str | None = None) -> dict[str, Counter]:
    table: dict[str, Counter] = {}
    files = materials = parse_failures = 0
    for label, cmb in iter_cmbs():
        if label_filter and label_filter not in label:
            continue
        try:
            records = list(parse_material_records(cmb))
        except Exception as error:  # a corpus scan must not silently drop a file
            print(f"parse error {label}: {error}", file=sys.stderr)
            parse_failures += 1
            continue
        if not records:
            continue
        files += 1
        for record in records:
            materials += 1
            consumed = consumed_units(record.stages)
            for unit in range(3):
                method = record.coord_mapping[unit]
                if method not in MAPPED_METHODS:
                    continue
                scope = "consumed" if unit in consumed else "declared-only"
                key = f"tex{unit} {METHOD_NAMES[method]} [{scope}]"
                bucket = table.setdefault(key, Counter())
                bucket["lit" if record.lit else "unlit"] += 1
                bucket[lighting_kind(record)] += 1
                if label_filter:
                    print(f"    {label} mat{record.index} tex{unit} method={method} "
                          f"fragLit={int(record.fragment_lighting)} "
                          f"vertLit={int(record.vertex_lighting)} scope={scope}")
    print(f"scanned {files} files / {materials} materials, {parse_failures} parse failures",
          file=sys.stderr)
    return table


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", help="only list materials whose label contains this")
    args = parser.parse_args()
    table = survey(args.file)
    if args.file:
        return 0
    print("\n== coordinator mapping methods 3 and 4, by lighting state ==")
    for key in sorted(table):
        counts = table[key]
        total = counts["lit"] + counts["unlit"]
        share = 100.0 * counts["unlit"] / total if total else 0.0
        print(f"  {key}: {total} materials "
              f"({counts['lit']} lit / {counts['unlit']} unlit, {share:.1f}% unlit)")
        print(f"      fragment-only={counts['fragment-only']} "
              f"vertex-only={counts['vertex-only']} both={counts['both']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
