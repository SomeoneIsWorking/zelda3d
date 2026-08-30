"""Source line ceilings and no-growth policy for legacy decomp seams."""

from __future__ import annotations

import ast
from collections.abc import Sequence
from pathlib import Path

from . import VerificationError
from .source_selection import (
    STRUCTURE_SUFFIXES,
    modified_legacy_decomp_files,
    repo_relative,
    repository_files,
    run_git,
)

SOURCE_LINE_LIMIT = 1_200

# These are ceilings, not allowances to grow. They are the HEAD line counts
# when this gate was introduced. Remove an entry once its file reaches 1,200;
# verify_legacy_limit_changes() rejects increasing an existing ceiling.
LEGACY_LINE_LIMITS = {
    "2ship/2s2h/BenGui/BenMenu.cpp": 2303,
    "2ship/2s2h/BenGui/CosmeticEditor.cpp": 1445,
    "2ship/2s2h/BenPort.cpp": 2476,
    "2ship/2s2h/DeveloperTools/SaveEditor.cpp": 2394,
    "2ship/2s2h/GameInteractor/GameInteractor_VanillaBehavior.h": 2462,
    "2ship/2s2h/Rando/DrawFuncs.cpp": 1339,
    "2ship/2s2h/Rando/StaticData/Checks.cpp": 2459,
    "2ship/2s2h/Rando/Types.h": 3212,
    "2ship/include/PR/gbi.h": 4106,
    "2ship/include/sfx.h": 2561,
    "2ship/include/z64camera.h": 1659,
    "2ship/include/z64player.h": 1476,
    "2ship/include/z64save.h": 1885,
    "Shipwright/libultraship/include/fast/lus_gbi.h": 1394,
    "Shipwright/libultraship/include/libultraship/libultra/gbi.h": 4371,
    "Shipwright/libultraship/include/ship/window/gui/Fonts.h": 2304,
    "Shipwright/libultraship/src/fast/backends/gfx_sdl3gpu.cpp": 2835,
    "Shipwright/soh/soh/Enhancements/TimeSavers/timesaver_hook_handlers.cpp": 1434,
    "Shipwright/soh/soh/Enhancements/debugconsole.cpp": 1784,
    "Shipwright/soh/soh/Enhancements/debugger/actorViewer.cpp": 1231,
    "Shipwright/soh/soh/Enhancements/debugger/debugSaveEditor.cpp": 1996,
    "Shipwright/soh/soh/Enhancements/randomizer/3drando/fill.cpp": 1491,
    "Shipwright/soh/soh/Enhancements/randomizer/3drando/hint_list.cpp": 2478,
    "Shipwright/soh/soh/Enhancements/randomizer/3drando/hint_list/hint_list_exclude_dungeon.cpp": 2328,
    "Shipwright/soh/soh/Enhancements/randomizer/3drando/hint_list/hint_list_exclude_overworld.cpp": 2482,
    "Shipwright/soh/soh/Enhancements/randomizer/3drando/hint_list/hint_list_item.cpp": 2169,
    "Shipwright/soh/soh/Enhancements/randomizer/Plandomizer.cpp": 1214,
    "Shipwright/soh/soh/Enhancements/randomizer/RCToRandInf.cpp": 3068,
    "Shipwright/soh/soh/Enhancements/randomizer/Traps.cpp": 1803,
    "Shipwright/soh/soh/Enhancements/randomizer/draw.cpp": 1387,
    "Shipwright/soh/soh/Enhancements/randomizer/entrance.cpp": 1786,
    "Shipwright/soh/soh/Enhancements/randomizer/location_access.cpp": 1238,
    "Shipwright/soh/soh/Enhancements/randomizer/location_access/dungeons/water_temple.cpp": 1453,
    "Shipwright/soh/soh/Enhancements/randomizer/logic.cpp": 3029,
    "Shipwright/soh/soh/Enhancements/randomizer/randomizer_check_tracker.cpp": 2519,
    "Shipwright/soh/soh/Enhancements/randomizer/settings.cpp": 3020,
    "Shipwright/soh/soh/Enhancements/tts/tts.cpp": 1204,
    "Shipwright/soh/soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h": 3082,
    "Shipwright/soh/soh/Enhancements/randomizer/randomizerEnums/RandomizerCheck.h": 3387,
    "Shipwright/soh/soh/Enhancements/randomizer/randomizerEnums/RandomizerHintTextKey.h": 1685,
    "Shipwright/soh/soh/Enhancements/randomizer/randomizerEnums/RandomizerInf.h": 2952,
    "Shipwright/soh/soh/SaveManager.cpp": 2892,
    "Shipwright/soh/soh/config/ConfigUpdaters.cpp": 1582,
    "Shipwright/soh/include/sfx.h": 1423,
    "Shipwright/soh/include/tables/dmadata_table_mqdbg.h": 1535,
    "Shipwright/soh/include/tables/entrance_table.h": 1949,
    "Shipwright/soh/include/z64.h": 2354,
    "Shipwright/soh/include/z64camera.h": 1288,
}


def count_lines(path: Path) -> int:
    data = path.read_bytes()
    return data.count(b"\n") + int(bool(data) and not data.endswith(b"\n"))


def count_blob_lines(data: bytes) -> int:
    return data.count(b"\n") + int(bool(data) and not data.endswith(b"\n"))


def parse_legacy_limits(source: str) -> dict[str, int]:
    tree = ast.parse(source)
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "LEGACY_LINE_LIMITS"
            for target in node.targets
        ):
            value = ast.literal_eval(node.value)
            if isinstance(value, dict):
                return {str(path): int(limit) for path, limit in value.items()}
            break
    raise VerificationError("could not read LEGACY_LINE_LIMITS from verifier source")


def head_legacy_limits(repo: Path) -> dict[str, int] | None:
    candidates = ("tools/clang_verifier/source_structure.py", "tools/verify_clang.py")
    for relative in candidates:
        result = run_git(repo, ["show", f"HEAD:{relative}"], check=False)
        if result.returncode == 0:
            return parse_legacy_limits(result.stdout)
    return None


def verify_legacy_limit_changes(
    repo: Path, current: dict[str, int], previous: dict[str, int] | None = None
) -> list[str]:
    previous = head_legacy_limits(repo) if previous is None else previous
    if previous is None:
        return []
    failures = []
    for path in sorted(set(current) - set(previous)):
        failures.append(
            f"{path}: new legacy ceiling is not allowed; split the file to {SOURCE_LINE_LIMIT} lines"
        )
    for path, old_limit in previous.items():
        new_limit = current.get(path)
        if new_limit is not None and new_limit > old_limit:
            failures.append(
                f"{path}: legacy ceiling increased {old_limit} -> {new_limit}"
            )
    return failures


def verify_modified_decomp_seams(repo: Path) -> list[str]:
    failures = []
    for path in modified_legacy_decomp_files(repo):
        relative = repo_relative(path, repo)
        head = run_git(repo, ["show", f"HEAD:{relative}"], check=False)
        if head.returncode != 0:
            continue
        current_lines = count_lines(path)
        head_lines = count_blob_lines(head.stdout.encode())
        if current_lines > head_lines:
            failures.append(
                f"{relative}: modified legacy decomp seam grew {head_lines} -> {current_lines} lines; "
                "extract or compact the seam"
            )
    return failures


def verify_structure(
    repo: Path,
    files: Sequence[Path] | None = None,
    legacy_limits: dict[str, int] | None = None,
) -> list[str]:
    full_repository_check = files is None
    files = (
        repository_files(repo, STRUCTURE_SUFFIXES) if full_repository_check else files
    )
    limits = LEGACY_LINE_LIMITS if legacy_limits is None else legacy_limits
    failures = verify_legacy_limit_changes(repo, limits)
    if full_repository_check:
        failures.extend(verify_modified_decomp_seams(repo))
    seen = set()
    for path in files:
        relative = repo_relative(path, repo)
        seen.add(relative)
        lines = count_lines(path)
        limit = limits.get(relative, SOURCE_LINE_LIMIT)
        if lines > limit:
            failures.append(f"{relative}: {lines} lines (limit {limit})")
        elif relative in limits and lines <= SOURCE_LINE_LIMIT:
            failures.append(
                f"{relative}: {lines} lines; remove its obsolete legacy ceiling (default is {SOURCE_LINE_LIMIT})"
            )
        elif relative in limits and lines < limit:
            failures.append(
                f"{relative}: {lines} lines; lower its legacy ceiling from {limit} to {lines}"
            )
    for relative in sorted(set(limits) - seen):
        failures.append(
            f"{relative}: stale legacy ceiling names a missing or excluded file"
        )
    return failures
