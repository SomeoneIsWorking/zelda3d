"""Discovery and parser validation for repository clang-tidy configurations."""

from __future__ import annotations

import subprocess
from collections.abc import Callable
from pathlib import Path

from . import VerificationError
from .source_selection import repo_relative, run_git

CommandRunner = Callable[..., subprocess.CompletedProcess[str]]


def repository_tidy_configs(repo: Path) -> list[Path]:
    """Return tracked and non-ignored untracked clang-tidy configurations."""
    tracked = run_git(repo, ["ls-files", "-z"]).stdout
    untracked = run_git(
        repo, ["ls-files", "--others", "--exclude-standard", "-z"]
    ).stdout
    configs = {
        repo / relative
        for relative in (tracked + untracked).split("\0")
        if relative
        and Path(relative).name == ".clang-tidy"
        and (repo / relative).is_file()
    }
    return sorted(configs)


def validate_tidy_config(
    repo: Path,
    config: Path,
    executable: str,
    *,
    runner: CommandRunner = subprocess.run,
) -> None:
    """Require clang-tidy to parse and recognize one configuration cleanly."""
    relative = repo_relative(config, repo)
    result = runner(
        [
            executable,
            f"--config-file={config}",
            "--verify-config",
            "--dump-config",
        ],
        cwd=repo,
        check=False,
        text=True,
        capture_output=True,
    )
    stderr = result.stderr.strip()
    if result.returncode != 0:
        detail = stderr or result.stdout.strip() or "no diagnostic output"
        raise VerificationError(
            f"{relative}: clang-tidy config validation exited {result.returncode}:\n{detail}"
        )
    if stderr:
        raise VerificationError(
            f"{relative}: clang-tidy reported config diagnostics despite exit 0:\n{stderr}"
        )
    if not result.stdout.strip():
        raise VerificationError(
            f"{relative}: clang-tidy returned no parsed configuration dump"
        )


def validate_repository_tidy_configs(
    repo: Path,
    executable: str,
    *,
    runner: CommandRunner = subprocess.run,
) -> int:
    """Validate every repository clang-tidy config and return the checked count."""
    configs = repository_tidy_configs(repo)
    failures = []
    for config in configs:
        try:
            validate_tidy_config(repo, config, executable, runner=runner)
        except VerificationError as exc:
            failures.append(str(exc))
    if failures:
        raise VerificationError(
            "clang-tidy configuration check failed:\n  "
            + "\n  ".join(failures).replace("\n", "\n  ")
        )
    return len(configs)
