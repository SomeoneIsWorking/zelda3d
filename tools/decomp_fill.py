#!/usr/bin/env python3
"""Drive a Ghidra project to decompile its whole function inventory, resumably.

`tools/decomp_coverage.py` answers "how much is recovered"; this is the thing that raises that
number. Coverage of 0.25% for MM3D against a 10% floor is a pipeline that runs and writes almost
nothing, and the two titles disagree about how a batch of targets is passed to the Ghidra script:
OoT3D's `DecompDump.py` reads a FILE named by `DECOMP_TARGETS`, MM3D's `DumpDecomp.py` reads a
comma-separated list in the same variable. That per-title difference is configuration here, not two
implementations of the loop.

    uv run --frozen python tools/decomp_fill.py mm3d            # fill the missing functions
    uv run --frozen python tools/decomp_fill.py oot3d --limit 200
    uv run --frozen python tools/decomp_fill.py mm3d --dry-run   # what would run, no Ghidra

Design rules this tool holds itself to:

* **Resumable, and correct because of it.** A run that dies half way leaves real output behind, so
  the next run must skip what already has a body rather than redo it. "Recovered" is asked of
  `decomp_coverage`, never re-decided here -- one implementation of that rule.
* **Denominators everywhere.** Every line reports out of the inventory, never a bare count, because
  "decompiled 4000 functions" reads identically whether that is 33% or 100% of the image.
* **Refuses a missing project or inventory** rather than reporting an empty success. A tool that
  reports "0 filled" when it never reached Ghidra is the exact failure this program exists to fix.
* **Bounded batches.** Ghidra's decompiler leaks across thousands of functions in one process, and a
  single 12k-target invocation that dies at 9k loses the batch. Batches are the unit of progress and
  the unit of retry.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
from dataclasses import dataclass, field

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from decomp_coverage import GAMES, load_inventory, load_recovered  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class TitleSpec:
    """Everything that differs between the two 3DS titles' Ghidra pipelines.

    The differences are real and were found by reading each repo's Ghidra script, not by guessing:
    OoT3D's script names a target FILE, MM3D's takes an inline list, and they write different
    filenames into different trees. Everything else about driving Ghidra is shared.
    """

    name: str
    game_dir: pathlib.Path
    project_dir: pathlib.Path
    project: str
    program: str
    script_dir: pathlib.Path
    script: str
    target_mode: str  # "file" (OoT3D) or "env-list" (MM3D)
    extra_env: dict[str, str] = field(default_factory=dict)


def title_specs() -> dict[str, TitleSpec]:
    """The per-title Ghidra invocation contract, resolved from this checkout.

    The OoT3D full project is the analyzed one (the small `oot3d` project predates full analysis);
    the MM3D project lives in the engine build tree because its `.code` is extracted there.
    """
    oot3d = GAMES["oot3d"]
    mm3d = GAMES["mm3d"]
    return {
        "oot3d": TitleSpec(
            name="oot3d",
            game_dir=oot3d,
            project_dir=oot3d / "build" / "ghidra",
            project="oot3d_full",
            program="code.bin",
            script_dir=oot3d / "tools" / "ghidra_scripts",
            script="DecompDump.py",
            target_mode="file",
            extra_env={"OOT3D_REPO": str(oot3d)},
        ),
        "mm3d": TitleSpec(
            name="mm3d",
            game_dir=mm3d,
            project_dir=REPO / "build" / "ghidra",
            project="mm3d",
            program="mm3d.code",
            script_dir=mm3d / "tools",
            script="DumpDecomp.py",
            target_mode="env-list",
            # VERBATIM output directory: `DumpDecomp.py` only appends "/decomp" on the fallback
            # path that reads ZELDA3D_REPO, so this must be the full directory itself. Pointing it
            # at the game root or at `build/` writes a real, complete, and entirely invisible
            # corpus that the coverage owner never reads -- a full-looking run reporting zero.
            extra_env={"MM3D_DECOMP_OUT": str(mm3d / "build" / "decomp"), "ZELDA3D_REPO": str(REPO)},
        ),
    }


def missing_targets(game: pathlib.Path) -> tuple[list[int], int, int]:
    """The inventory addresses with no decompiled body yet, and the two counts behind that.

    Returns (missing, total, already_recovered). Raises when there is no inventory: without the
    denominator there is no way to report this run honestly, and a silent empty list would look
    like a finished job.
    """
    inventory = load_inventory(game)
    if not inventory:
        raise SystemExit(
            f"no function inventory at {game / 'build' / 'decomp' / 'functions.csv'}; "
            "run the Ghidra inventory script with no targets first"
        )
    recovered, _stubs = load_recovered(game)
    todo = sorted(addr for addr in inventory if addr not in recovered)
    return todo, len(inventory), len(set(inventory) & set(recovered))


def batches(values: list[int], size: int) -> list[list[int]]:
    if size < 1:
        raise ValueError("batch size must be >= 1")
    return [values[i : i + size] for i in range(0, len(values), size)]


def build_invocation(spec: TitleSpec, targets: list[int]) -> tuple[list[str], dict[str, str]]:
    """The exact Ghidra command and the env it needs, as data.

    Split out from `run_batch` so the per-title target convention -- the one thing that differs
    between the two repos and the one that silently produces zero output when wrong -- is checkable
    without launching Ghidra. `run_batch` performs the only side effect here: writing OoT3D's
    target file, which OoT3D's script reads by name.
    """
    if not targets:
        raise ValueError("refusing to build a Ghidra invocation with no targets")
    env = dict(spec.extra_env)
    if spec.target_mode == "file":
        env["DECOMP_TARGETS"] = str(target_file_for(spec))
    elif spec.target_mode == "env-list":
        env["DECOMP_TARGETS"] = ",".join(f"{addr:08x}" for addr in targets)
    else:
        raise ValueError(f"unknown target_mode {spec.target_mode!r}")
    command = [
        "analyzeHeadless",
        str(spec.project_dir),
        spec.project,
        "-process",
        spec.program,
        "-noanalysis",
        "-scriptPath",
        str(spec.script_dir),
        "-postScript",
        spec.script,
    ]
    return command, env


def target_file_for(spec: TitleSpec) -> pathlib.Path:
    """Where OoT3D's Ghidra script will look for the batch's target list.

    Under the repo's gitignored `scratch/`, not inside the submodule: the file is a per-run artifact
    of this driver, and a submodule that tracks its own tree would otherwise be handed thousands of
    transient, uncommitted writes.
    """
    return REPO / "scratch" / "decomp_fill" / f"{spec.name}_batch_targets.txt"


def recovered_count(game: pathlib.Path) -> int:
    """How many functions in the game's inventory have a decompiled body right now.

    Counted from the tree, not from what a Ghidra script said it wrote. The two titles report
    differently -- MM3D's script announces every write on stdout, while OoT3D's Jython script
    announces nothing catchable -- so a log-parsing counter reported "0 written" for OoT3D batches
    that were in fact landing hundreds of files each. A progress number that can read zero while
    work happens is worse than no progress number.
    """
    inventory = load_inventory(game)
    recovered, _stubs = load_recovered(game)
    return len(set(inventory) & set(recovered))


def run_batch(spec: TitleSpec, targets: list[int], timeout: int) -> tuple[int, str]:
    """Decompile one batch. Returns (the script's own CLAIM of writes, combined log).

    The claim is returned for diagnostics only and is NOT used as progress: it is parsed from
    `wrote` lines on stdout, which MM3D's script emits and OoT3D's does not. Progress is measured
    from the tree by `recovered_count`.
    """
    command, overrides = build_invocation(spec, targets)
    env = dict(os.environ)
    env.update(overrides)
    if spec.target_mode == "file":
        target_file = target_file_for(spec)
        target_file.parent.mkdir(parents=True, exist_ok=True)
        target_file.write_text(
            "".join(f"{addr:08x}\n" for addr in targets), encoding="utf-8"
        )
    try:
        proc = subprocess.run(
            command,
            env=env,
            cwd=str(REPO),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError:
        raise SystemExit("analyzeHeadless not found on PATH; Ghidra is required to decompile")
    except subprocess.TimeoutExpired:
        return 0, f"batch timed out after {timeout}s"

    log = (proc.stdout or "") + (proc.stderr or "")
    # Both scripts announce each write on stdout as `wrote <path>`. Counting the FILES after the
    # batch is the honest measure, but the log line is the cheap per-batch signal, so use it and
    # re-derive the truth from the tree at the end of the run.
    written = sum(1 for line in log.splitlines() if line.strip().startswith("wrote "))
    return written, log


def output_dir_for(spec: TitleSpec) -> pathlib.Path:
    """Where this title's Ghidra script will actually WRITE, read off its own source.

    `tools/decomp_coverage.py` reads `<game>/build/decomp`, and a run whose output lands anywhere
    else reports zero recovery while having done all the work. So the destination is derived from
    each script's real env handling rather than assumed symmetric:

    * MM3D's `DumpDecomp.py` uses `MM3D_DECOMP_OUT` VERBATIM as the output directory, and only
      appends `/decomp` in the fallback path that reads `ZELDA3D_REPO`.
    * OoT3D's `DecompDump.py` joins `build/decomp` onto `OOT3D_REPO` unconditionally.

    The two differ in exactly the way that produces a complete, invisible corpus: pointing
    MM3D_DECOMP_OUT at the game root or at `build/` decompiles thousands of functions that coverage
    never counts. `preflight` therefore refuses a spec that does not land on the coverage directory,
    turning a silent misplacement into an immediate, named failure.
    """
    if spec.target_mode == "env-list":
        return pathlib.Path(spec.extra_env["MM3D_DECOMP_OUT"])
    return pathlib.Path(spec.extra_env["OOT3D_REPO"]) / "build" / "decomp"


def preflight(spec: TitleSpec) -> None:
    """Refuse early and by name when the pipeline cannot run, instead of reporting a zero fill."""
    if not spec.project_dir.is_dir():
        raise SystemExit(f"{spec.name}: Ghidra project dir missing: {spec.project_dir}")
    if not (spec.project_dir / f"{spec.project}.gpr").is_file():
        raise SystemExit(
            f"{spec.name}: no {spec.project}.gpr in {spec.project_dir}; the analyzed project is "
            "absent, so there is nothing to decompile from"
        )
    if not (spec.script_dir / spec.script).is_file():
        raise SystemExit(f"{spec.name}: Ghidra script missing: {spec.script_dir / spec.script}")
    out = output_dir_for(spec)
    expected = spec.game_dir / "build" / "decomp"
    if out != expected:
        raise SystemExit(
            f"{spec.name}: output would land in {out}, but coverage reads {expected}; a run would "
            "decompile the whole image and report none of it"
        )


def say(message: str) -> None:
    """Print progress and flush it.

    A twenty-minute Ghidra run whose output is block-buffered into a log file shows nothing for the
    first several batches, which is indistinguishable from a hang. Every progress line here is
    flushed so a redirected run can be watched.
    """
    print(message, flush=True)


def fill(name: str, *, batch_size: int, limit: int, timeout: int, dry_run: bool) -> int:
    specs = title_specs()
    if name not in specs:
        raise SystemExit(f"unknown title {name!r}; choose from {', '.join(sorted(specs))}")
    spec = specs[name]
    preflight(spec)

    todo, total, already = missing_targets(spec.game_dir)
    planned = todo[:limit] if limit else todo
    say(
        f"{name}: {len(planned)} of {total} functions to decompile "
        f"({already} already recovered, {len(todo) - len(planned)} deferred by --limit)"
    )
    if not planned:
        say(f"{name}: nothing to do; the inventory is fully covered")
        return 0
    chunks = batches(planned, batch_size)
    say(f"{name}: {len(chunks)} batch(es) of up to {batch_size}")
    if dry_run:
        for chunk in chunks[:3]:
            say(f"  would run {len(chunk)} targets, first 0x{chunk[0]:08x}")
        if len(chunks) > 3:
            say(f"  ... and {len(chunks) - 3} more")
        return 0

    for index, chunk in enumerate(chunks, start=1):
        before = recovered_count(spec.game_dir)
        _claimed, _log = run_batch(spec, chunk, timeout)
        landed = recovered_count(spec.game_dir) - before
        done = index * len(chunk) if index < len(chunks) else len(planned)
        say(
            f"  batch {index}/{len(chunks)}: {len(chunk)} targets, {landed} new bodies "
            f"({min(done, len(planned))}/{len(planned)} attempted, "
            f"{recovered_count(spec.game_dir)}/{total} total)"
        )

    # Re-derive the truth from the tree, never from the sum of what the scripts claimed.
    final_todo, final_total, final_done = missing_targets(spec.game_dir)
    filled = final_done - already
    say(
        f"{name}: filled {filled}; now {final_done}/{final_total} recovered "
        f"({final_done / final_total * 100:.2f}%), {len(final_todo)} still missing"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("title", choices=sorted(title_specs()), help="which 3DS title to fill")
    parser.add_argument("--batch-size", type=int, default=500, help="targets per Ghidra run")
    parser.add_argument("--limit", type=int, default=0, help="stop after N targets (0 = all)")
    parser.add_argument("--timeout", type=int, default=1800, help="per-batch timeout in seconds")
    parser.add_argument("--dry-run", action="store_true", help="report the plan, run no Ghidra")
    args = parser.parse_args()
    return fill(
        args.title,
        batch_size=args.batch_size,
        limit=args.limit,
        timeout=args.timeout,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    sys.exit(main())
