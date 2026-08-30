#!/usr/bin/env python3
"""Summarize one cached oracle draw's fragment stream without rerunning Azahar."""

from __future__ import annotations

import argparse
import json
import re
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, TextIO

PIXEL_RE = re.compile(
    r"^PIXEL draw=(?P<draw>\d+) tex0=(?P<tex0>[0-9a-fA-F]+) "
    r"xy=\((?P<x>\d+),(?P<y>\d+)\) depth=(?P<depth>[-+\d.eE]+) "
    r"texcol=\((?P<texcol>[\d,]+)\) tex1col=\((?P<tex1col>[\d,]+)\) "
    r"primary=\((?P<primary>[\d,]+)\) combined=\((?P<combined>[\d,]+)\)$"
)
COLOR_FIELDS = ("texcol", "tex1col", "primary", "combined")
CHANNEL_NAMES = ("r", "g", "b", "a")


@dataclass(frozen=True)
class Fragment:
    draw: int
    tex0: str
    x: int
    y: int
    depth: float
    texcol: tuple[int, int, int, int]
    tex1col: tuple[int, int, int, int]
    primary: tuple[int, int, int, int]
    combined: tuple[int, int, int, int]


def _color(value: str) -> tuple[int, int, int, int]:
    channels = tuple(int(channel) for channel in value.split(","))
    if len(channels) != 4:
        raise ValueError(f"expected four color channels, got {value!r}")
    return channels


def parse_fragment(line: str) -> Fragment | None:
    match = PIXEL_RE.match(line.rstrip("\n"))
    if match is None:
        return None
    values = match.groupdict()
    return Fragment(
        draw=int(values["draw"]),
        tex0=values["tex0"].lower(),
        x=int(values["x"]),
        y=int(values["y"]),
        depth=float(values["depth"]),
        texcol=_color(values["texcol"]),
        tex1col=_color(values["tex1col"]),
        primary=_color(values["primary"]),
        combined=_color(values["combined"]),
    )


def _channel_summary(values: Iterable[int]) -> dict[str, float | int]:
    samples = list(values)
    return {
        "min": min(samples),
        "max": max(samples),
        "mean": round(statistics.fmean(samples), 3),
        "median": statistics.median(samples),
    }


def summarize(stream: TextIO, draw: int) -> dict[str, object]:
    generated = 0
    nearest: dict[tuple[int, int], Fragment] = {}
    for line in stream:
        fragment = parse_fragment(line)
        if fragment is None or fragment.draw != draw:
            continue
        generated += 1
        key = (fragment.x, fragment.y)
        previous = nearest.get(key)
        if previous is None or fragment.depth < previous.depth:
            nearest[key] = fragment

    if not nearest:
        raise ValueError(f"no PIXEL records found for draw {draw}")

    fragments = list(nearest.values())
    xs = [fragment.x for fragment in fragments]
    ys = [fragment.y for fragment in fragments]
    summary: dict[str, object] = {
        "draw": draw,
        "generated_fragments": generated,
        "unique_pixels": len(fragments),
        "discarded_occluded_fragments": generated - len(fragments),
        "framebuffer_bbox": {
            "min_x": min(xs),
            "min_y": min(ys),
            "max_x": max(xs),
            "max_y": max(ys),
        },
        "tex0_addresses": sorted({fragment.tex0 for fragment in fragments}),
    }
    colors: dict[str, object] = {}
    for field in COLOR_FIELDS:
        color_summary: dict[str, object] = {}
        for index, channel in enumerate(CHANNEL_NAMES):
            color_summary[channel] = _channel_summary(
                getattr(fragment, field)[index] for fragment in fragments
            )
        colors[field] = color_summary
    summary["colors"] = colors
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="cached oracle log containing PIXEL records")
    parser.add_argument("--draw", type=int, required=True, help="exact vsuni_log draw index")
    parser.add_argument("--output", type=Path, help="optional JSON output path")
    args = parser.parse_args()

    with args.log.open(encoding="utf-8", errors="replace") as stream:
        result = summarize(stream, args.draw)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
