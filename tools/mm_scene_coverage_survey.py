#!/usr/bin/env python3
"""Measure MM3D scene coverage: which sceneIds divert to 3DS rooms, and which do not.

`Zelda3D_TryDrawRoom` returns 1 only when the current `play->sceneId` maps to a name in
`2ship/2s2h/zelda3d/mm3d_scene_names.inc` AND that name has a per-room
`/scenes/<name>_<room>_info.zsi` in the MM3D ROM. Everything else silently falls back to the N64 room
mesh, so a mapping gap is invisible from the host's side: it just looks like MM3D content is missing.

This survey closes that from the data side, in both directions:

* **table -> ROM**: every mapped name has a matching per-room file (a table entry that names a
  scene MM3D never shipped is a dead mapping).
* **ROM -> table**: every per-room file is reachable through some mapped name (content that exists
  but is unreachable is a coverage hole).

Both directions are reported with counts, and the "unreachable" list is the actionable one. The 11
`SCENE_UNSET` ids are MM3D's own unset slots, not mapping gaps, and are counted separately so they
cannot pad the coverage number.

Usage:
    source .env
    tools/mm_scene_coverage_survey.py
    tools/mm_scene_coverage_survey.py --json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from ctr_romfs import CtrRom  # noqa: E402

# The one owner of the sceneId -> folder-name mapping, per mm3d_draw.h ("Shared with the collision
# path so the sceneNum->name table has ONE owner"). Parsed, never transcribed: a copy here would
# drift from the table the game actually uses, which is the same failure mode as the LA4 format table.
SCENE_NAMES_INC = REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_scene_names.inc"
ENTRY = re.compile(
    r"/\* 0x([0-9A-F]{2}) SCENE_(\S+)\s+\(([^)]*)\)\s*\*/\s*(NULL|\"[a-z0-9_]+\")"
)
SCENES_PREFIX = "/scenes/"
INFO_SUFFIX = "_info.zsi"


def mm_rom_path() -> str:
    for key in ("ZELDA3D_MM3D_ROM", "ZELDA3D_MM_ROM"):
        value = os.environ.get(key)
        if value:
            return value
    raise RuntimeError("source .env first (ZELDA3D_MM3D_ROM)")


def parse_scene_table() -> tuple[dict[int, str], list[int], list[int]]:
    """(sceneId -> name, ids whose MM3D enum is UNSET, total entries)."""
    text = SCENE_NAMES_INC.read_text()
    mapped: dict[int, str] = {}
    unset: list[int] = []
    total = 0
    for hex_id, enum_name, _paren, value in ENTRY.findall(text):
        total += 1
        scene_id = int(hex_id, 16)
        if value == "NULL":
            # MM3D's own SCENE_UNSET slots, not a mapping gap.
            if "UNSET" in enum_name:
                unset.append(scene_id)
            continue
        mapped[scene_id] = value.strip('"')
    return mapped, unset, total


def rom_scene_files(rom: CtrRom) -> set[str]:
    files: set[str] = set()
    for entry in rom.iter_files():
        path = getattr(entry, "path", None) or getattr(entry, "name", "")
        if isinstance(path, str) and path.startswith(SCENES_PREFIX) and path.endswith(INFO_SUFFIX):
            files.add(path)
    return files


def rooms_by_base(files: set[str]) -> dict[str, set[int]]:
    """`/scenes/<base>_<room>_info.zsi` -> base -> {room numbers}.

    A stem with no numeric suffix is recorded as room -1 so it is still counted rather than dropped;
    silently discarding it would understate coverage.
    """
    rooms: dict[str, set[int]] = defaultdict(set)
    for path in files:
        stem = path[len(SCENES_PREFIX) : -len(INFO_SUFFIX)]
        match = re.match(r"^(.*)_(\d+)$", stem)
        if match:
            rooms[match.group(1)].add(int(match.group(2)))
        else:
            rooms[stem].add(-1)
    return rooms


def classify_scene_files(
    files: set[str], mapped_names: set[str], rooms: dict[str, set[int]]
) -> tuple[set[str], list[str], list[str]]:
    """Split scene files into (reachable, unreachable, no-room-suffix).

    Split out as a pure function so the classification is directly testable. An earlier version of
    this survey inlined the loop, and two separate mutations to that loop -- transcribing the table
    instead of parsing it, and treating no-suffix files as reachable -- both left the suite green,
    because every test exercised the whole `survey()` path, which needs a ROM. Inlining a rule and
    only testing it through a ROM-gated entry point is how a test ends up asserting nothing.

    Reachability follows the host's EXACT path construction: `/scenes/<name>_<roomNum>_info.zsi`,
    with roomNum = room->num always appended. So:
      * a room-suffixed file is reachable iff some mapped name owns that base;
      * a file with no numeric suffix is unreachable BY CONSTRUCTION, and is reported apart because it
        measures the archive's layout rather than the game's reachability.
    """
    reachable: set[str] = set()
    unreachable: list[str] = []
    unsuffixed: list[str] = []
    for path in files:
        stem = path[len(SCENES_PREFIX) : -len(INFO_SUFFIX)]
        if not re.match(r"^.*_\d+$", stem):
            unsuffixed.append(path)
            continue
        if any(
            path == f"{SCENES_PREFIX}{name}_{room}{INFO_SUFFIX}"
            for name in mapped_names
            for room in rooms.get(name, ())
        ):
            reachable.add(path)
        else:
            unreachable.append(path)
    return reachable, sorted(unreachable), sorted(unsuffixed)


def survey() -> dict[str, object]:
    mapped, unset, total = parse_scene_table()
    rom = CtrRom(mm_rom_path())
    files = rom_scene_files(rom)
    rooms = rooms_by_base(files)

    covered_bases = {name for name in mapped.values() if rooms.get(name)}
    dead_mappings = sorted(name for name in mapped.values() if not rooms.get(name))

    reachable_files, unreachable, unsuffixed = classify_scene_files(
        files, set(mapped.values()), rooms
    )
    mapped_ids = sorted(mapped)
    return {
        "table_entries": total,
        "mapped_scene_ids": len(mapped),
        "mm3d_unset_scene_ids": len(unset),
        "rom_scene_files": len(files),
        "mapped_names_with_rooms": len(covered_bases),
        "dead_mappings": dead_mappings,
        "reachable_files": len(reachable_files),
        "unreachable_files": len(unreachable),
        "unreachable_examples": unreachable[:12],
        "no_room_suffix_files": len(unsuffixed),
        "scene_id_range": [mapped_ids[0], mapped_ids[-1]] if mapped_ids else [],
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = survey()
    except (RuntimeError, FileNotFoundError) as error:
        print(f"mm_scene_coverage_survey: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=1))
        return 0
    print("== MM3D scene coverage ==")
    print(f"  scene-name table entries:        {result['table_entries']}")
    print(f"  mapped to an MM3D folder:        {result['mapped_scene_ids']}"
          f"  (sceneId range {result['scene_id_range'][0]:#04x}..{result['scene_id_range'][1]:#04x})")
    print(f"  MM3D's own SCENE_UNSET ids:      {result['mm3d_unset_scene_ids']}  (not mapping gaps)")
    print(f"  per-room scene files in the ROM: {result['rom_scene_files']}")
    print(f"  mapped names with >=1 room:     {result['mapped_names_with_rooms']}")
    print(f"  reachable through the table:    {result['reachable_files']} of {result['rom_scene_files']}")
    if result["dead_mappings"]:
        print(f"  DEAD mappings (named but absent from the ROM): {result['dead_mappings']}")
    print(f"  no-room-suffix files (host always appends room->num, so it never builds these): "
          f"{result['no_room_suffix_files']}")
    if result["unreachable_files"]:
        print(f"  UNREACHABLE room-suffixed scene files: {result['unreachable_files']}")
        for path in result["unreachable_examples"]:
            print(f"    {path}")
    else:
        print("  every room-suffixed scene file in the ROM is reachable through the table")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
