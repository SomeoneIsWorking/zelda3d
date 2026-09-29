#!/usr/bin/env python3
"""Decompilation coverage for both 3DS titles, with denominators.

"Fully decompile the game" has no meaning until it is counted, and the two repos had no
common way to say how much of the image was actually recovered. This is that way.

    python3 tools/decomp_coverage.py            # report
    python3 tools/decomp_coverage.py --check    # gate, non-zero exit if the floor drops

Counting rules, and why each one exists:

* A function counts as recovered only if a `.c` file exists for its address AND the file is
  non-trivial. An empty or header-only file is Ghidra saying "this one did not decompile", and
  counting it would inflate coverage with the exact failure we care about.
* Names count separately from coverage. `FUN_0040d1a8` is decompiled but unreadable, and the
  objective asks for READABLE C++, so a decompiled-but-unnamed function is reported as such
  rather than silently folded into the total.
* The denominator is the Ghidra function inventory, not a file count. Coverage of a corpus with
  no denominator is a vanity number.

The inventory files are produced by the Ghidra script
`oot3d-decomp/tools/ghidra_scripts/DecompDump.py` run with no targets file, which writes
`build/decomp/functions.csv` (addr,size,name) for the analyzed project.
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

GAMES = {
    "oot3d": REPO / "oot3d-decomp",
    "mm3d": REPO / "mm3d-decomp",
}

# The two repos used different output conventions, so both are recognised rather than forcing
# one repo's files to be miscounted as zero. OoT3D writes <vaddr>.c; MM3D writes fn_0x<vaddr>.c.
FILE_PATTERNS = (
    re.compile(r"^([0-9a-f]{8})\.c$"),
    re.compile(r"^fn_0x([0-9a-f]{8})\.c$"),
)

# A file this short is Ghidra emitting a stub, not a recovered function.
MIN_USEFUL_BYTES = 200


def load_inventory(game: pathlib.Path) -> dict[int, str]:
    csv_path = game / "build" / "decomp" / "functions.csv"
    if not csv_path.is_file():
        return {}
    inventory: dict[int, str] = {}
    with csv_path.open(encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle):
            try:
                inventory[int(row["vaddr"], 16)] = row.get("name", "")
            except (KeyError, ValueError):
                continue
    return inventory


def load_recovered(game: pathlib.Path) -> tuple[dict[int, pathlib.Path], list[pathlib.Path]]:
    """Return (address -> file) for substantial output, plus the stubs found on the way."""
    decomp = game / "build" / "decomp"
    recovered: dict[int, pathlib.Path] = {}
    stubs: list[pathlib.Path] = []
    if not decomp.is_dir():
        return recovered, stubs
    for path in sorted(decomp.iterdir()):
        if not path.is_file():
            continue
        for pattern in FILE_PATTERNS:
            match = pattern.match(path.name)
            if not match:
                continue
            # Bytes, not lines: a function that decompiles to nothing still gets a header comment.
            if path.stat().st_size < MIN_USEFUL_BYTES:
                stubs.append(path)
            else:
                recovered[int(match.group(1), 16)] = path
            break
    return recovered, stubs


def measure(game_name: str, game: pathlib.Path) -> dict:
    inventory = load_inventory(game)
    recovered, stubs = load_recovered(game)
    if not inventory:
        return {
            "game": game_name,
            "status": "NO INVENTORY",
            "detail": f"missing {game / 'build' / 'decomp' / 'functions.csv'}; "
                      f"run DecompDump.py with no targets file against the Ghidra project",
        }
    # A recovered file for an address Ghidra does not list still counts as work, but reporting it
    # inside the denominator would be dishonest, so it is surfaced separately.
    orphans = sorted(set(recovered) - set(inventory))
    total = len(inventory)
    done = sum(1 for addr in inventory if addr in recovered)
    named = sum(1 for addr in inventory if addr in recovered
                and not inventory[addr].startswith("FUN_")
                and not inventory[addr].startswith("sub_"))
    return {
        "game": game_name,
        "status": "ok",
        "total": total,
        "recovered": done,
        "stubs": len(stubs),
        "orphans": len(orphans),
        "named": named,
        "coverage": done / total if total else 0.0,
        "readable": named / total if total else 0.0,
    }


def report(results: list[dict], floor: float) -> int:
    print("decompilation coverage")
    print("=" * 78)
    for r in results:
        if r["status"] != "ok":
            print(f"  {r['game']:<6} {r['status']}: {r['detail']}")
            continue
        pct = r["coverage"] * 100
        rpct = r["readable"] * 100
        bar = "#" * int(pct // 2)
        print(f"  {r['game']:<6} {r['recovered']:>6} / {r['total']:<6} recovered "
              f"({pct:5.2f}%)  {bar}")
        print(f"  {'':<6} {r['named']:>6} / {r['total']:<6} READABLE (named) ({rpct:5.2f}%)")
        if r["stubs"]:
            print(f"  {'':<6} {r['stubs']} file(s) are decompiler stubs under "
                  f"{200} bytes and are NOT counted")
        if r["orphans"]:
            print(f"  {'':<6} {r['orphans']} recovered file(s) are outside the Ghidra inventory")
    ok = [r for r in results if r["status"] == "ok"]
    print("-" * 78)
    if not ok:
        print("no game has an inventory; coverage cannot be reported")
        return 1
    floor_fail = [r["game"] for r in ok if r["coverage"] < floor]
    if floor_fail:
        print(f"FAIL: coverage below the {floor:.2%} floor for {', '.join(floor_fail)} "
              f"(coverage regressed; decompiled output was lost)")
        return 1
    print(f"OK: every game is at or above the {floor:.2%} coverage floor")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="gate mode: non-zero exit on regression")
    parser.add_argument("--floor", type=float, default=0.10,
                        help="minimum recovered fraction per game (default 0.10)")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args()

    results = [measure(name, path) for name, path in GAMES.items()]
    if args.json:
        print(json.dumps(results, indent=2))
        return 0
    return report(results, args.floor)


if __name__ == "__main__":
    sys.exit(main())
