#!/usr/bin/env python3
"""Mutation-check `tools/test_pica_lighting_registers.py`.

A mutation harness that fails to apply its own mutation reports "all tests pass" while having
measured nothing, which is worse than having no mutation harness. So this driver ASSERTS that each
replacement actually changed the file, and reports the before/after hashes; an `applied: no` line
is a failure of the harness, not a result.

Run: `python3 tools/check_pica_lighting_registers_mutations.py`
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
TARGET = TOOLS / "pica_lighting_registers.py"
TESTS = TOOLS / "test_pica_lighting_registers.py"

# (description, old, new) -- each must apply cleanly and must be caught by at least one test.
MUTATIONS: list[tuple[str, str, str]] = [
    (
        "diff names bit 17 instead of bit 16 (the recorded wrong claim)",
        '"disable_lut_d0",',
        '"disable_lut_d1",',
    ),
    (
        "Distribution1 support ignores Config0",
        '"Distribution1": frozenset({0, 1, 5}),',
        '"Distribution1": frozenset(),',
    ),
    (
        "hardwired bit-18 refusal removed",
        "if not (config1 >> 18) & 1:",
        "if False:",
    ),
    (
        "per-slot shadow gate reads the wrong 8-bit field",
        '_all_set("disable_shadow")',
        '_all_set("disable_spot_atten")',
    ),
    (
        "Config7 renumbered to 7 (closing the documented hole)",
        '8: "Config7",',
        '7: "Config7",',
    ),
    (
        "bump_mode read from the wrong field",
        '_mask(regmap, "config0", "bump_mode")',
        '_mask(regmap, "config0", "bump_selector")',
    ),
    (
        "reduced form re-introduces a half-vector specular term",
        "specular[c] += slot.get(\"specular_0\", (0.0, 0.0, 0.0))[c]",
        "specular[c] += slot.get(\"specular_0\", (0.0, 0.0, 0.0))[c] * n_dot_l",
    ),
    (
        "reduced form forgets to clamp the outputs",
        "clamp = lambda v: (max(0.0, min(1.0, v[0])), max(0.0, min(1.0, v[1])), max(0.0, min(1.0, v[2])))",
        "clamp = lambda v: v",
    ),
    (
        "light direction is not normalised",
        "lx, ly, lz = px / length, py / length, pz / length",
        "lx, ly, lz = px, py, pz",
    ),
    (
        "global_ambient leaks into the specular output",
        'return {"primary": clamp(diffuse), "secondary": clamp(specular)}',
        'return {"primary": clamp(diffuse), "secondary": clamp([specular[c] + global_ambient[c] for c in range(3)])}',
    ),
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def run_tests() -> tuple[bool, str]:
    # Defeat the bytecode cache. A mutation that preserves the file SIZE (e.g. `8:` -> `7:`) can
    # leave the stale `__pycache__/*.pyc` valid under Python's (mtime, size) staleness check, and
    # then the "mutated" run imports the ORIGINAL module -- so the mutation silently measures
    # nothing and reports a clean pass. Purging before every run is what makes the result mean
    # what it says. This was observed, not hypothesised: the Config7 mutation reported `caught`
    # only because of an unrelated leftover, and the Distribution1 mutation reported `SURVIVED`
    # while the module under test was not the one on disk.
    for cache in TOOLS.rglob("__pycache__"):
        shutil.rmtree(cache, ignore_errors=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    proc = subprocess.run(
        [sys.executable, "-m", "pytest", str(TESTS), "-q"],
        cwd=TOOLS.parent, capture_output=True, text=True, env=env)
    return proc.returncode == 0, proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""


def main() -> int:
    original = TARGET.read_text(encoding="utf-8")
    original_hash = digest(TARGET)

    ok, baseline = run_tests()
    print(f"baseline: {TARGET.name} {original_hash}  tests: {baseline}")
    if not ok:
        print("BASELINE FAILS -- mutations would be meaningless")
        return 1

    survivors = []
    print()
    for desc, old, new in MUTATIONS:
        # Restore FIRST. A `continue` on a pattern miss must not leave the previous iteration's
        # mutation in place, or the next `run_tests()` measures a file nobody intended.
        TARGET.write_text(original, encoding="utf-8")
        if original.count(old) < 1:
            print(f"  [HARNESS BUG] {desc}: pattern not found: {old!r}")
            survivors.append(desc + " [pattern not found]")
            continue
        TARGET.write_text(original.replace(old, new, 1), encoding="utf-8")
        applied = digest(TARGET) != original_hash
        passed, line = run_tests()
        status = "SURVIVED" if passed else "caught"
        print(f"  {status:9s} applied={applied!s:5s} {desc}")
        print(f"            -> {line}")
        if not applied:
            survivors.append(desc + " [mutation did not change the file]")
        elif passed:
            survivors.append(desc)

    TARGET.write_text(original, encoding="utf-8")
    ok, restored = run_tests()
    print(f"\nrestored: {TARGET.name} {digest(TARGET)}  tests: {restored}")
    if digest(TARGET) != original_hash:
        print("RESTORE MISMATCH -- the target file was not returned to its original bytes")
        return 1
    if survivors:
        print(f"\n{len(survivors)} MUTATION(S) SURVIVED:")
        for s in survivors:
            print(f"  - {s}")
        return 1
    print(f"\nall {len(MUTATIONS)} mutations caught")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
