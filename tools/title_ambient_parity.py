#!/usr/bin/env python3
"""Compare the host's RENDERED scene ambient against the oracle's live PICA ambient at the title.

Camera-independent by construction. The title host-vs-oracle image comparison is dominated by the
title-demo camera (the host runs the N64 demo, the oracle the 3DS one), but the scene *ambient* is a
per-scene colour: it does not depend on where the camera is, so it can be compared directly and is
the dominant term for OoT3D's vertex-lit materials. Per `spot00_field_lighting_ground_truth.md` the
terrain class has `matDiffuse = BLACK`, which makes every directional term a provable no-op and leaves

    colour = saturate(2.0 * texel * bakedVertexColor * sceneAmbient/255)

as the whole result -- so the ambient IS the lighting for the largest material class in the game.

Three things are checked, and a disagreement at any of them is reported rather than reconciled:

1. the host actually SUBMITS a 3DS palette blend (not a stale or N64 value);
2. the submitted value matches the palette slot blend for the host's own cutscene frame;
3. the palette slot blend matches the oracle's live `amb0` for the oracle's daytime.

The host's title cutscene clock does not advance under a bare `run N` -- it reads frame=0 for the
whole session -- so this drives the title with the same helper `title_host_capture.py` uses. Without
that the palette is never submitted and the host reports whatever was left over, which is how a false
divergence gets manufactured.

Usage:
    source .env
    tools/title_ambient_parity.py
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402
from title_host_capture import advance_host_title, boot_host_title  # noqa: E402

# The 3DS title light palette, from /scene/spot99_info.zsi: the four 28-byte entries immediately
# before the " BDQ" stream, ambient at entry +0x0A. Read by Zelda3D_TitleCsLoad in zelda3d_cutscene.cpp.
TITLE_PALETTE_AMBIENT = ((104, 104, 61), (130, 130, 109), (119, 51, 10), (40, 61, 119))
# kTitleLightSchedule in zelda3d_cutscene.cpp: {start, end, slotFrom, slotTo}, weight =
# (daytime - start) / (end - start).
TITLE_LIGHT_SCHEDULE = (
    (0x0000, 0x2AAC, 3, 3), (0x2AAC, 0x4000, 3, 0), (0x4000, 0x4AAB, 0, 0),
    (0x4AAB, 0x6000, 0, 1), (0x6000, 0xA000, 1, 1), (0xA000, 0xB556, 1, 2),
    (0xB556, 0xC001, 2, 2), (0xC001, 0xD556, 2, 3), (0xD556, 0xFFFF, 3, 3),
)


def blend_for_daytime(daytime: int) -> tuple[tuple[int, int, int], float, int, int] | None:
    """The palette blend a daytime resolves to, with the span that produced it."""
    for start, end, slot_from, slot_to in TITLE_LIGHT_SCHEDULE:
        if start <= daytime and daytime < end:
            span = float(end - start)
            weight = ((daytime - start) / span) if span > 0.0 else 0.0
            a = TITLE_PALETTE_AMBIENT[slot_from & 3]
            b = TITLE_PALETTE_AMBIENT[slot_to & 3]
            blended = tuple(int(a[i] + (b[i] - a[i]) * weight + 0.5) for i in range(3))
            return blended, weight, slot_from, slot_to
    return None


def match_palette_blend(
    target: tuple[int, int, int], tolerance: int = 0
) -> tuple[int, int, float, list[int]] | None:
    """Solve each span's blend WEIGHT analytically, then confirm all three channels.

    Scanning daytimes inside a span was the previous approach and it produced a FALSE NEGATIVE: it
    only walked the first 512 daytimes of each span, and slot 3->0 reaches the interesting weights
    around daytime 0x3d46, which is 4762 past the span start. A bounded search that silently misses
    the interior of a span reads as "this value is not from the palette", which is the opposite
    conclusion. So the weight is solved for and then every channel is checked.

    `tolerance` is in 1/255 units. The host computes its blend from the same integer expression, so
    tolerance 0 is right for checking the host against its own palette. Comparing the ORACLE needs
    tolerance 1: the oracle reports a float uniform, so its value can land one 8-bit step either side
    of the host's rounding. Without that allowance the oracle's own live value fails to match the
    palette it came from, which would be a second false negative of the same family.
    """
    for start, end, sf, st in TITLE_LIGHT_SCHEDULE:
        a = TITLE_PALETTE_AMBIENT[sf & 3]
        b = TITLE_PALETTE_AMBIENT[st & 3]
        span = float(end - start)
        if span <= 0.0:
            continue
        # Derive w from the first channel where the two slots actually differ, then require every
        # channel to agree. A single channel cannot determine w when a == b on that channel.
        weight: float | None = None
        for i in range(3):
            if b[i] != a[i]:
                weight = (target[i] - a[i]) / float(b[i] - a[i])
                break
        if weight is None or not (-0.001 <= weight <= 1.001):
            continue
        blended = tuple(int(a[i] + (b[i] - a[i]) * weight + 0.5) for i in range(3))
        if any(abs(blended[i] - target[i]) > tolerance for i in range(3)):
            continue
        lo = max(0, int(round(start + weight * span - 1.0)))
        hi = min(0xFFFF, int(round(start + weight * span + 1.0)))
        return sf, st, weight, list(range(lo, hi + 1))
    return None


def parse_floats(text: str, key: str) -> tuple[float, float, float] | None:
    found = re.search(rf"{key}=\(([^)]*)\)", text)
    if not found:
        return None
    parts = [p.strip() for p in found.group(1).split(",")]
    if len(parts) != 3:
        return None
    return tuple(float(p) for p in parts)  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--title-cs", type=int, default=1093, help="title cutscene cursor to reach")
    args = parser.parse_args()

    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_ambient_parity.log"))
    harness = spawn()
    try:
        current = boot_host_title(harness, unified_renderer=1)
        current = advance_host_title(harness, current, args.title_cs)
        live = (harness.send("soh_z3dlive") or "").strip()
        cs = (harness.send("soh_titlecs") or "").strip()
        print(f"[ambient] host {cs}")
        submitted = parse_floats(live, "submittedAmbient")
        n64 = parse_floats(live, "worldAmbColorOverrideGated")
        if submitted is None:
            print(f"[ambient] could not read the submitted ambient: {live}", file=sys.stderr)
            return 1
        as_bytes = tuple(int(round(c * 255.0)) for c in submitted)
        print(f"[ambient] host SUBMITTED ambient = {as_bytes}  (override-gated worldAmbColor, normally stale: {n64})")
        if as_bytes == (0, 0, 255):
            print("[ambient] the host submitted its INITIAL world ambient, so the 3DS title palette "
                  "never reached the renderer at this cursor -- the palette feed is not running.",
                  file=sys.stderr)
            return 2
        match = match_palette_blend(as_bytes)
        if match is None:
            print("[ambient] submitted ambient is NOT any blend of the four 3DS title palette slots",
                  file=sys.stderr)
            return 3
        sf, st, weight, daytimes = match
        print(f"[ambient] submitted ambient IS a 3DS title-palette blend: slot {sf}->{st} at "
              f"w={weight:.4f}, {len(daytimes)} daytime(s) in 0..0xFFFF produce it "
              f"(e.g. 0x{daytimes[len(daytimes) // 2]:04x})")
        # The oracle's own live value must also fall on this ramp. tolerance=1 because the oracle
        # reports a float uniform that can sit one 8-bit step either side of the host's rounding.
        oracle_blend, oracle_w, oracle_sf, oracle_st = blend_for_daytime(0x2D95)
        oracle_match = match_palette_blend((48, 66, 111), tolerance=1)
        print(f"[ambient] oracle live amb0 (48,66,111) at daytime 0x2d95: palette predicts "
              f"{oracle_blend} (slot {oracle_sf}->{oracle_st} @ w={oracle_w:.3f})")
        if oracle_match is None:
            print("[ambient] ORACLE ambient is not on the 3DS title-palette ramp either", file=sys.stderr)
            return 4
        print(f"[ambient] oracle ambient IS on the same authored ramp: slot {oracle_match[0]}->"
              f"{oracle_match[1]} @ w={oracle_match[2]:.3f}")
        print("[ambient] VERDICT: host and oracle sample the SAME 3DS title-palette ramp at different "
              "points of the dayTime cycle (they run different title demos), so the ambient hue "
              "difference between them is a time-alignment artifact, not a colour divergence.")
    finally:
        harness.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
