"""ROM discovery and validation policy for the embedded OoT3D harness."""

from __future__ import annotations

from collections.abc import MutableMapping, Sequence
from pathlib import Path


def _first_file(candidates: Sequence[Path]) -> Path | None:
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def _drop_in_candidates(
    repo: Path, canonical: str, patterns: tuple[str, ...]
) -> list[Path]:
    candidates = [repo / canonical]
    for pattern in patterns:
        candidates.extend(sorted(repo.glob(pattern)))
    return list(dict.fromkeys(candidates))


def provision_rom_environment(
    repo: Path, environment: MutableMapping[str, str]
) -> None:
    """Resolve caller/``.env``/drop-in ROMs and require a valid OoT3D ROM."""
    if not environment.get("ZELDA3D_OOT3D_ROM"):
        oot3d = _first_file(_drop_in_candidates(repo, "oot3d.3ds", ("*.3ds",)))
        if oot3d is not None:
            environment["ZELDA3D_OOT3D_ROM"] = str(oot3d)

    if not environment.get("ZELDA3D_OOT_ROM"):
        oot = _first_file(
            _drop_in_candidates(repo, "oot.z64", ("*.z64", "*.n64", "*.v64"))
        )
        if oot is not None:
            environment["ZELDA3D_OOT_ROM"] = str(oot)

    oot3d_value = environment.get("ZELDA3D_OOT3D_ROM")
    if not oot3d_value:
        raise RuntimeError(
            "OoT3D ROM provisioning scanned the process environment, repo .env, "
            "and repo-root *.3ds files; matched 0"
        )
    if not Path(oot3d_value).is_file():
        raise RuntimeError(f"provisioned OoT3D ROM does not exist: {oot3d_value}")
