#!/usr/bin/env python3
"""Correlate CmbVShader's ShaderMode.w with the lighting flags across a vsuni log.

`oot3d-decomp/docs/cmb_texcoord_mapping.md` recovers that the entry point picks
between the mapping-capable vertex body (body@14) and the plain one (body@214)
with the single `ShaderMode.w` uniform (shader register c89.w), and that
`texSlotMap.w` in the oracle's `vsuni_log` line IS that value. This tool answers,
from a real capture, two questions the shader cannot answer on its own:

  * which values ShaderMode.w actually takes, and
  * whether ShaderMode.w is just "this draw was lit" — the hypothesis the shader's
    structure suggests (body@14 is the only body with a lighting block).

If the second question answers "no", gating the host's mapping on lit-ness is
wrong and the host's current unconditional behaviour is the right default.

Usage: python3 tools/cmb_shader_mode_correlation.py <vsuni.log>
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

DRAW_RE = re.compile(r"^draw n=(?P<draw>\d+) idx=(?P<idx>\d+)")
VLIT_RE = re.compile(r"\bvLit=(?P<value>[01])\b")
FLIT_RE = re.compile(r"\bfLit=(?P<value>[01])\b")
SLOTMAP_RE = re.compile(r"texSlotMap=\((?P<x>[^,]+),(?P<y>[^,]+),(?P<z>[^,]+),(?P<w>[^,]+)\)")
METHOD_RE = re.compile(r"texMappingMethod=\((?P<a>[^,]+),(?P<b>[^,]+),(?P<c>[^,]+),(?P<d>[^,]+)\)")


def _one(pattern: re.Pattern[str], line: str, label: str) -> re.Match[str]:
    match = pattern.search(line)
    if match is None:
        raise ValueError(f"vsuni line has no {label}")
    return match


def rows(lines) -> list[dict[str, object]]:
    parsed = []
    for line in lines:
        if not DRAW_RE.match(line):
            continue
        slot = _one(SLOTMAP_RE, line, "texSlotMap")
        method = _one(METHOD_RE, line, "texMappingMethod")
        vertex_lit = _one(VLIT_RE, line, "vLit").group("value") == "1"
        fragment_lit = _one(FLIT_RE, line, "fLit").group("value") == "1"
        parsed.append({
            "draw": int(DRAW_RE.match(line).group("draw")),
            "vertex_lit": vertex_lit,
            "fragment_lit": fragment_lit,
            "lit": vertex_lit or fragment_lit,
            "tex_coord_slot": tuple(slot.group(k) for k in "xyz"),
            "shader_mode": slot.group("w").strip(),
            "mapping": tuple(method.group(k).strip() for k in "abcd"),
        })
    return parsed


def report(parsed) -> int:
    if not parsed:
        print("no draw lines in the log", file=sys.stderr)
        return 1
    print(f"draws={len(parsed)}")
    modes = Counter(str(row["shader_mode"]) for row in parsed)
    print("ShaderMode.w values: " + ", ".join(f"{k}x{v}" for k, v in sorted(modes.items())))
    slots = Counter(",".join(row["tex_coord_slot"]) for row in parsed)
    print("TexCoordSlot.xyz values: " + ", ".join(f"{k}x{v}" for k, v in sorted(slots.items())))

    print("\nShaderMode.w against lighting (does the mode just mean 'lit'?):")
    table: dict[str, Counter] = {}
    for row in parsed:
        table.setdefault(str(row["shader_mode"]), Counter())["lit" if row["lit"] else "unlit"] += 1
    for mode in sorted(table):
        counts = table[mode]
        print(f"  mode={mode}: {counts['lit']} lit / {counts['unlit']} unlit")

    mapped = [row for row in parsed if any(m in ("3", "4") for m in row["mapping"][:3])]
    print(f"\ndraws sampling a coordinator with mapping 3 or 4: {len(mapped)}")
    for row in mapped:
        print(f"  draw {row['draw']}: mode={row['shader_mode']} "
              f"vLit={int(row['vertex_lit'])} fLit={int(row['fragment_lit'])} "
              f"mapping={row['mapping'][:3]} slot={row['tex_coord_slot']}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args(argv)
    with args.log.open(encoding="utf-8", errors="replace") as stream:
        return report(rows(stream))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
