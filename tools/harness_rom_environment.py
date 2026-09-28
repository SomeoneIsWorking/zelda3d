"""ROM discovery and validation policy for the embedded oracle harness.

Identity is decided by the 3DS image's own **NCCH media ID**, not by a filename. With both 3DS
releases in play — and each project's N64 counterpart next to them in the same drop-in directory — a
glob like `*.3ds` can hand one game's image to the other game's oracle, and every reading taken
through it is then confidently about the wrong game. That is the same failure shape this project
keeps meeting: a confident false negative rather than an error.

It also provisions the **Majora's Mask 3D oracle ROM**. MM3D's image is content-decrypted but its
NCCH `flags` byte has `no_crypto` clear, and `Azahar/src/core/file_sys/ncch_container.cpp:281`
rejects an encrypted NCCH outright — so the emulator refuses it and reports the reason through a
libretro message callback nobody handles, which reads as a silent boot failure. `tools/ctr_oracle_rom.py`
sets that bit **in a copy**; this module is what makes the copy happen automatically instead of
leaving it as a recipe in somebody's notes.
"""

from __future__ import annotations

import sys
from collections.abc import MutableMapping, Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ctr_oracle_rom  # noqa: E402


def _first_file(candidates: Sequence[Path]) -> Path | None:
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def _drop_in_candidates(
    repo: Path, canonical: str, patterns: tuple[str, ...]
) -> list[Path]:
    candidates = [repo / canonical]
    for pattern in patterns:
        candidates.extend(sorted(repo.glob(pattern)))
    return list(dict.fromkeys(candidates))


def _ctr_images(repo: Path) -> dict[bytes, Path]:
    """Map media ID -> path for every 3DS image dropped in the repo root.

    A file whose partition 0 has no NCCH header is not a 3DS image at all and is skipped rather than
    treated as an error: the directory legitimately also holds `.z64` and other drop-ins.
    """
    found: dict[bytes, Path] = {}
    for path in sorted(repo.glob("*.3ds")):
        try:
            found.setdefault(ctr_oracle_rom.read_media_id(path), path)
        except (ValueError, OSError):
            continue
    return found


def provision_mm3d_oracle_rom(
    repo: Path, environment: MutableMapping[str, str]
) -> Path | None:
    """Make an Azahar-loadable copy of MM3D's image and return its path, or None if MM3D is absent.

    Idempotent and content-checked: the copy is only rewritten when the source's `no_crypto` bit is
    actually clear, so a steady-state run copies nothing.
    """
    source_value = environment.get("ZELDA3D_MM3D_ROM")
    if not source_value:
        images = _ctr_images(repo)
        source = images.get(ctr_oracle_rom.MM3D_MEDIA_ID)
        if source is None:
            return None
        source_value = str(source)
        environment["ZELDA3D_MM3D_ROM"] = source_value
    source = Path(source_value)
    if not source.is_file():
        raise RuntimeError(f"provisioned MM3D ROM does not exist: {source}")
    if ctr_oracle_rom.read_flags(source) >> 2 & 1:
        return source

    scratch = repo / "scratch" / "oracle-roms"
    scratch.mkdir(parents=True, exist_ok=True)
    destination = scratch / f"{source.stem}-nocrypto.3ds"
    if not destination.is_file() or destination.stat().st_size != source.stat().st_size:
        destination.unlink(missing_ok=True)
        _was, now = ctr_oracle_rom.set_no_crypto(_copy_then_set(source, destination))
        if not now >> 2 & 1:
            raise RuntimeError(
                f"MM3D oracle ROM still has no_crypto clear after correction: {destination}"
            )
    environment["ZELDA3D_MM3D_ORACLE_ROM"] = str(destination)
    return destination


def _copy_then_set(source: Path, destination: Path) -> Path:
    import shutil

    shutil.copyfile(source, destination)
    return destination


def _require_oot3d(path: Path) -> Path | None:
    """Accept `path` as the OoT3D oracle image, or None if it is demonstrably a DIFFERENT game.

    Identity is only enforced when the file is actually a 3DS image. A file with no NCCH header is
    not contradicted -- it may be a test fixture, or an N64 dump that happens to be named `.3ds` --
    so it passes as before. A file that *does* carry a media ID, and it is MM3D's, is refused: that
    is the exact accident this policy exists to prevent, and accepting it on the strength of a
    filename would make the media-ID check decorative.
    """
    try:
        media = ctr_oracle_rom.read_media_id(path)
    except (ValueError, OSError):
        return path
    if media == ctr_oracle_rom.MM3D_MEDIA_ID:
        return None
    return path


def provision_rom_environment(
    repo: Path, environment: MutableMapping[str, str]
) -> None:
    """Resolve caller/``.env``/drop-in ROMs and require a valid OoT3D ROM."""
    if not environment.get("ZELDA3D_OOT3D_ROM"):
        # Identity, not filename: a repo-root `*.3ds` that is MM3D must not become the OoT3D oracle.
        oot3d = _first_file(_drop_in_candidates(repo, "oot3d.3ds", ()))
        if oot3d is None:
            oot3d = _ctr_images(repo).get(ctr_oracle_rom.OOT3D_MEDIA_ID)
        if oot3d is not None and _require_oot3d(oot3d) is not None:
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
            f"and repo-root *.3ds files; matched 0. OoT3D's media ID is "
            f"{ctr_oracle_rom.OOT3D_MEDIA_ID.decode()} — an image whose media ID is MM3D's "
            f"({ctr_oracle_rom.MM3D_MEDIA_ID.decode()}) is deliberately not accepted here, because "
            "running MM3D's image as the OoT3D oracle produces readings about the wrong game."
        )
    if not Path(oot3d_value).is_file():
        raise RuntimeError(f"provisioned OoT3D ROM does not exist: {oot3d_value}")

    provision_mm3d_oracle_rom(repo, environment)
