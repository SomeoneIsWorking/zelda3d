"""Majora's Mask ROM discovery and authoritative runtime-archive extraction."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import zipfile
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

import cmake_build_policy
from repo_environment import apply_repo_environment
from rom_identity import is_mm3d_rom, is_mm_n64_rom

MM_ROM_NAME = "ZELDA3D_MM_ROM"
MM3D_ROM_NAME = "ZELDA3D_MM3D_ROM"
MM_ROM_CANONICAL_NAMES = ("mm.z64", "mm.n64", "mm.v64")
MM_ROM_PATTERNS = ("*.z64", "*.n64", "*.v64")
MM3D_ROM_CANONICAL_NAMES = ("mm3d.3ds",)
MM3D_ROM_PATTERNS = ("*.3ds",)
PROJECT_VERSION_RE = re.compile(
    r"\bproject\s*\(\s*Ship\s+VERSION\s+([0-9]+(?:\.[0-9]+){1,3})\b",
    re.IGNORECASE,
)


class MmAssetError(RuntimeError):
    """Raised when MM inputs or generated runtime archives are invalid."""


@dataclass(frozen=True)
class MmAssetLayout:
    """Paths owned by MM's independent N64 asset-extraction pipeline."""

    repo: Path
    extraction_build_dir: Path
    mm_source_dir: Path
    extractor: Path
    zapd: Path
    runtime_dir: Path

    @classmethod
    def for_repo(
        cls, repo: Path, runtime_build_dir: Path | None = None
    ) -> MmAssetLayout:
        resolved_repo = repo.resolve()
        extraction_build_dir = resolved_repo / "Shipwright" / "build-mm-extract"
        runtime_root = (
            runtime_build_dir.resolve()
            if runtime_build_dir is not None
            else resolved_repo / "Shipwright" / "build-cmake"
        )
        return cls(
            repo=resolved_repo,
            extraction_build_dir=extraction_build_dir,
            mm_source_dir=resolved_repo / "2ship",
            extractor=resolved_repo
            / "Shipwright"
            / "OTRExporter"
            / "extract_assets.py",
            zapd=extraction_build_dir / "ZAPD" / "ZAPD.out",
            runtime_dir=runtime_root / "mm",
        )

    @property
    def generated_archives(self) -> tuple[Path, Path]:
        return self.mm_source_dir / "mm.o2r", self.mm_source_dir / "2ship.o2r"

    @property
    def runtime_archives(self) -> tuple[Path, Path]:
        return self.runtime_dir / "mm.o2r", self.runtime_dir / "2ship.o2r"


CommandRunner = Callable[[Sequence[str], Path], None]


def _canonical_file(repo: Path, names: Sequence[str]) -> Path | None:
    for name in names:
        candidate = repo / name
        if candidate.is_file():
            return candidate.resolve()
    return None


def _matching_drop_in(
    repo: Path,
    patterns: Sequence[str],
    predicate: Callable[[Path], bool],
    description: str,
    environment_name: str,
) -> Path | None:
    seen: set[Path] = set()
    matches: list[Path] = []
    for pattern in patterns:
        for candidate in sorted(repo.glob(pattern)):
            resolved = candidate.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            if candidate.is_file() and predicate(candidate):
                matches.append(resolved)
    if len(matches) > 1:
        names = ", ".join(path.name for path in matches)
        raise MmAssetError(
            f"multiple {description} repo-root drop-ins match ({names}); "
            f"set {environment_name} explicitly"
        )
    return matches[0] if matches else None


def _repo_relative_path(repo: Path, value: str) -> Path:
    path = Path(value).expanduser()
    return (path if path.is_absolute() else repo / path).resolve()


def resolve_mm_rom_environment(
    repo: Path, environment: Mapping[str, str] | None = None
) -> dict[str, str]:
    """Resolve MM inputs using caller, then ``.env``, then repo drop-ins."""
    repo = repo.resolve()
    resolved = dict(os.environ if environment is None else environment)
    caller_values = {
        name for name in (MM_ROM_NAME, MM3D_ROM_NAME) if resolved.get(name)
    }
    apply_repo_environment(repo, resolved)

    discoveries = (
        (
            MM_ROM_NAME,
            MM_ROM_CANONICAL_NAMES,
            MM_ROM_PATTERNS,
            is_mm_n64_rom,
            "Majora's Mask N64 ROM",
        ),
        (
            MM3D_ROM_NAME,
            MM3D_ROM_CANONICAL_NAMES,
            MM3D_ROM_PATTERNS,
            is_mm3d_rom,
            "decrypted Majora's Mask 3D ROM",
        ),
    )
    for name, canonical_names, patterns, predicate, description in discoveries:
        value = resolved.get(name)
        if value:
            if name not in caller_values:
                resolved[name] = str(_repo_relative_path(repo, value))
            continue
        discovered = _canonical_file(repo, canonical_names)
        if discovered is None:
            discovered = _matching_drop_in(repo, patterns, predicate, description, name)
        if discovered is not None:
            resolved[name] = str(discovered)
    return resolved


def require_mm_roms(repo: Path, environment: Mapping[str, str]) -> tuple[Path, Path]:
    """Require the owned N64 MM and decrypted MM3D inputs used by the product."""
    paths: list[Path] = []
    for name, description in (
        (MM_ROM_NAME, "Majora's Mask N64 ROM"),
        (MM3D_ROM_NAME, "decrypted Majora's Mask 3D .3ds"),
    ):
        value = environment.get(name)
        if not value:
            raise MmAssetError(
                f"no {description} found — set {name}, add it to ./.env, "
                f"or use the canonical repo-root filename in {repo}"
            )
        path = _repo_relative_path(repo.resolve(), value)
        if not path.is_file():
            raise MmAssetError(f"{name} does not name an existing file: {path}")
        predicate = is_mm_n64_rom if name == MM_ROM_NAME else is_mm3d_rom
        if not predicate(path):
            raise MmAssetError(f"{name} is not a recognized {description}: {path}")
        paths.append(path)
    return paths[0], paths[1]


def _project_version(cmake_file: Path) -> str:
    try:
        source = cmake_file.read_text(encoding="utf-8")
    except OSError as exc:
        raise MmAssetError(
            f"cannot read project version from {cmake_file}: {exc}"
        ) from exc
    match = PROJECT_VERSION_RE.search(source)
    if match is None:
        raise MmAssetError(f"cannot find Ship project version in {cmake_file}")
    return match.group(1)


def configure_command(
    layout: MmAssetLayout, python_executable: str | Path
) -> list[str]:
    """Configure the independent GAME_MM exporter without constraining the C++ compiler."""
    return cmake_build_policy.configure_command(
        layout.repo,
        layout.extraction_build_dir,
        options=("-DCMAKE_BUILD_TYPE=Release", "-DGAME_STR=MM"),
        python_executable=python_executable,
    )


def has_required_configuration(
    layout: MmAssetLayout, python_executable: str | Path
) -> bool:
    return cmake_build_policy.has_ninja_configuration(
        layout.extraction_build_dir
    ) and cmake_build_policy.cache_matches(
        layout.extraction_build_dir,
        {
            "GAME_STR": "MM",
            "Python3_EXECUTABLE": str(Path(python_executable).resolve()),
        },
    )


def zapd_build_command(layout: MmAssetLayout, jobs: int) -> list[str]:
    return [
        "cmake",
        "--build",
        str(layout.extraction_build_dir),
        "--target",
        "ZAPD",
        f"-j{jobs}",
    ]


def extraction_command(
    layout: MmAssetLayout, mm_rom: Path, python_executable: str | Path
) -> list[str]:
    """Run the repository's OTRExporter against the MM XML and custom assets."""
    return [
        str(python_executable),
        str(layout.extractor),
        "-z",
        str(layout.zapd),
        "--non-interactive",
        "--xml-root",
        str(layout.mm_source_dir / "assets" / "xml"),
        "--custom-otr-file",
        "2ship.o2r",
        "--custom-assets-path",
        str(layout.mm_source_dir / "assets" / "custom"),
        "--port-ver",
        _project_version(layout.repo / "CMakeLists.txt"),
        str(mm_rom),
    ]


def run_command(command: Sequence[str], cwd: Path) -> None:
    try:
        subprocess.run(list(command), cwd=cwd, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise MmAssetError(f"MM asset command failed: {' '.join(command)}") from exc


def _require_sources(layout: MmAssetLayout) -> None:
    required_files = (
        layout.repo / "CMakeLists.txt",
        layout.extractor,
        layout.mm_source_dir / "assets" / "extractor" / "Config_N64_US.xml",
    )
    required_dirs = (
        layout.mm_source_dir / "assets" / "xml",
        layout.mm_source_dir / "assets" / "custom",
    )
    missing = [path for path in required_files if not path.is_file()]
    missing.extend(path for path in required_dirs if not path.is_dir())
    if missing:
        names = ", ".join(str(path.relative_to(layout.repo)) for path in missing)
        raise MmAssetError(f"checkout is missing MM asset source(s): {names}")


def _clear_generated_archives(archives: Sequence[Path]) -> None:
    for archive in archives:
        if archive.exists() or archive.is_symlink():
            archive.unlink()


def validate_archive(path: Path, required_member: str) -> None:
    """Reject missing, empty, corrupt, or semantically wrong O2R ZIP archives."""
    if not path.is_file() or path.stat().st_size == 0:
        raise MmAssetError(f"MM extraction did not produce a non-empty archive: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            if required_member not in archive.namelist():
                raise MmAssetError(
                    f"MM extraction archive lacks required member {required_member!r}: {path}"
                )
            corrupt_member = archive.testzip()
    except zipfile.BadZipFile as exc:
        raise MmAssetError(
            f"MM extraction produced an invalid O2R archive: {path}"
        ) from exc
    if corrupt_member is not None:
        raise MmAssetError(
            f"MM extraction archive has a corrupt member {corrupt_member!r}: {path}"
        )


def runtime_archives_are_valid(layout: MmAssetLayout) -> bool:
    try:
        for archive, required_member in zip(
            layout.runtime_archives, ("version", "portVersion"), strict=True
        ):
            validate_archive(archive, required_member)
    except MmAssetError:
        return False
    return True


def extract_mm_runtime_archives(
    layout: MmAssetLayout,
    environment: Mapping[str, str],
    *,
    python_executable: str | Path,
    jobs: int | None = None,
    runner: CommandRunner = run_command,
) -> tuple[Path, Path]:
    """Build GAME_MM ZAPD, extract both archives, validate them, and install them."""
    _require_sources(layout)
    mm_rom, _mm3d_rom = require_mm_roms(layout.repo, environment)
    resolved_jobs = jobs if jobs is not None else (os.cpu_count() or 4)
    if resolved_jobs < 1:
        raise MmAssetError("MM extraction job count must be positive")

    if not has_required_configuration(layout, python_executable):
        runner(configure_command(layout, python_executable), layout.repo)
        if not has_required_configuration(layout, python_executable):
            raise MmAssetError(
                "MM extraction CMake metadata does not use Ninja, GAME_STR=MM, and "
                "the requested Python3_EXECUTABLE"
            )
    runner(zapd_build_command(layout, resolved_jobs), layout.repo)
    if not layout.zapd.is_file():
        raise MmAssetError(f"GAME_MM ZAPD is missing after its build: {layout.zapd}")

    _clear_generated_archives(layout.generated_archives)
    runner(
        extraction_command(layout, mm_rom, python_executable),
        layout.mm_source_dir,
    )
    for archive, required_member in zip(
        layout.generated_archives, ("version", "portVersion"), strict=True
    ):
        validate_archive(archive, required_member)

    layout.runtime_dir.mkdir(parents=True, exist_ok=True)
    for source, destination in zip(
        layout.generated_archives, layout.runtime_archives, strict=True
    ):
        shutil.copy2(source, destination)
    for archive, required_member in zip(
        layout.runtime_archives, ("version", "portVersion"), strict=True
    ):
        validate_archive(archive, required_member)
    return layout.runtime_archives


def ensure_mm_runtime_archives(
    layout: MmAssetLayout,
    environment: Mapping[str, str],
    *,
    python_executable: str | Path,
    jobs: int | None = None,
    runner: CommandRunner = run_command,
) -> tuple[Path, Path]:
    """Keep valid MM runtime archives or regenerate the complete pair."""
    if runtime_archives_are_valid(layout):
        return layout.runtime_archives
    return extract_mm_runtime_archives(
        layout,
        environment,
        python_executable=python_executable,
        jobs=jobs,
        runner=runner,
    )
