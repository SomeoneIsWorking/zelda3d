#!/usr/bin/env python3
"""Package the verified Zelda3D launcher and both cores without game files."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "Shipwright" / "build-cmake"
PACKAGING = ROOT / "packaging"
SCRATCH = ROOT / "scratch"
APPDIR_REQUIRED_FILES = (
    "AppRun",
    "usr/bin/zelda3d",
    "usr/soh/libsoh_core.so",
    "usr/mm/libmm_core.so",
    "usr/soh/soh.o2r",
    "usr/mm/2ship.o2r",
    "usr/soh/assets/Config_N64_NTSC_10.xml",
    "usr/mm/assets/Config_N64_US.xml",
    "usr/soh/assets/xml/GC_MQ_D/audio/Audio.xml",
    "usr/mm/assets/xml/GC_US/audio/Audio.xml",
    "io.github.SomeoneIsWorking.Zelda3D.svg",
    "usr/share/applications/io.github.SomeoneIsWorking.Zelda3D.desktop",
    "usr/share/metainfo/io.github.SomeoneIsWorking.Zelda3D.appdata.xml",
)
ABI_VERSION_CEILINGS = {
    "GLIBC": (2, 35),
    "GLIBCXX": (3, 4, 30),
}
ABI_VERSION_PATTERN = re.compile(r"\b(GLIBCXX|GLIBC)_([0-9]+(?:\.[0-9]+)+)\b")


def refuse(message: str) -> None:
    raise SystemExit(f"appimage: {message}")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        refuse(f"{label} is missing: {path}")
    return path


def require_directory(path: Path, label: str) -> Path:
    if not path.is_dir():
        refuse(f"{label} is missing: {path}")
    return path


def copy_file(source: Path, destination: Path, label: str) -> None:
    require_file(source, label)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def reset_directory(path: Path) -> Path:
    """Create one stable disposable workspace, replacing the previous run."""
    resolved = path.resolve()
    scratch_root = SCRATCH.resolve()
    if resolved == scratch_root or scratch_root not in resolved.parents:
        refuse(f"temporary workspace is outside scratch: {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)
    resolved.mkdir(parents=True)
    return resolved


def require_port_archive(path: Path, label: str) -> Path:
    require_file(path, label)
    if path.stat().st_size > 128 * 1024 * 1024:
        refuse(
            f"{label} is unexpectedly large for a redistributable port archive: {path}"
        )
    try:
        with zipfile.ZipFile(path) as archive:
            if "portVersion" not in archive.namelist():
                refuse(f"{label} lacks the custom-asset portVersion marker: {path}")
            corrupt_member = archive.testzip()
    except zipfile.BadZipFile:
        refuse(f"{label} is not a valid O2R ZIP archive: {path}")
    if corrupt_member is not None:
        refuse(f"{label} contains a corrupt member {corrupt_member!r}: {path}")
    return path


def copy_extractor_assets(source_assets: Path, destination: Path, label: str) -> None:
    extractor = require_directory(
        source_assets / "extractor", f"{label} extractor metadata"
    )
    xml = require_directory(source_assets / "xml", f"{label} XML metadata")
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copytree(extractor, destination, dirs_exist_ok=True)
    shutil.copytree(xml, destination / "xml", dirs_exist_ok=True)


def assert_no_game_files(appdir: Path) -> None:
    forbidden_names = {"oot.o2r", "oot-mq.o2r", "mm.o2r"}
    forbidden_suffixes = {".z64", ".n64", ".v64", ".3ds", ".cia", ".zip"}
    offenders = [
        path.relative_to(appdir)
        for path in appdir.rglob("*")
        if path.is_file()
        and (
            path.name.lower() in forbidden_names
            or path.suffix.lower() in forbidden_suffixes
        )
    ]
    if offenders:
        refuse(
            "user-supplied or derived game files entered the package: "
            + ", ".join(map(str, offenders))
        )


def assert_complete_appdir(appdir: Path) -> None:
    missing = [
        relative
        for relative in APPDIR_REQUIRED_FILES
        if not (appdir / relative).is_file()
    ]
    if missing:
        refuse("package payload is incomplete: " + ", ".join(missing))


def parse_abi_versions(version_info: str) -> set[tuple[str, tuple[int, ...]]]:
    return {
        (family, tuple(int(part) for part in version.split(".")))
        for family, version in ABI_VERSION_PATTERN.findall(version_info)
    }


def assert_compatible_abi(appdir: Path, appimage: Path, readelf: str) -> None:
    checked = 0
    offenders: list[str] = []
    candidates = [(appimage, "AppImage runtime")]
    candidates.extend(
        (path, str(path.relative_to(appdir)))
        for path in sorted(
            candidate for candidate in appdir.rglob("*") if candidate.is_file()
        )
    )
    for path, label in candidates:
        try:
            with path.open("rb") as stream:
                magic = stream.read(4)
            if magic != b"\x7fELF":
                continue
        except OSError as exc:
            refuse(f"cannot inspect packaged file {path}: {exc}")
        checked += 1
        result = subprocess.run(
            [readelf, "--version-info", str(path)],
            check=True,
            capture_output=True,
            text=True,
        )
        for family, version in sorted(parse_abi_versions(result.stdout)):
            ceiling = ABI_VERSION_CEILINGS[family]
            if version > ceiling:
                rendered = ".".join(map(str, version))
                maximum = ".".join(map(str, ceiling))
                offenders.append(
                    f"{label} requires {family}_{rendered} (maximum {family}_{maximum})"
                )
    if checked == 0:
        refuse("extracted AppImage contains no ELF payloads to check")
    if offenders:
        refuse(
            "AppImage exceeds the Ubuntu 22.04 ABI baseline: " + "; ".join(offenders)
        )
    ceilings = ", ".join(
        f"{family}_{'.'.join(map(str, version))}"
        for family, version in ABI_VERSION_CEILINGS.items()
    )
    print(f"appimage: ABI compatible ({checked} ELF files, ceilings {ceilings})")


def stage_appdir(
    appdir: Path,
    build: Path,
    oot_assets: Path,
    mm_assets: Path,
    mm_port_archive: Path,
) -> None:
    launcher = build / "zelda3d" / "zelda3d"
    oot_core = build / "soh" / "libsoh_core.so"
    mm_core = build / "mm" / "libmm_core.so"
    copy_file(launcher, appdir / "usr/bin/zelda3d", "Zelda3D launcher")
    copy_file(oot_core, appdir / "usr/soh/libsoh_core.so", "Ocarina of Time core")
    copy_file(mm_core, appdir / "usr/mm/libmm_core.so", "Majora's Mask core")
    copy_file(
        require_port_archive(build / "soh/soh.o2r", "Ocarina port archive"),
        appdir / "usr/soh/soh.o2r",
        "Ocarina port archive",
    )
    copy_file(
        require_port_archive(mm_port_archive, "Majora port archive"),
        appdir / "usr/mm/2ship.o2r",
        "Majora port archive",
    )
    copy_extractor_assets(oot_assets, appdir / "usr/soh/assets", "Ocarina")
    copy_extractor_assets(mm_assets, appdir / "usr/mm/assets", "Majora")

    controller_db = build / "gamecontrollerdb.txt"
    if controller_db.is_file():
        copy_file(
            controller_db,
            appdir / "usr/soh/gamecontrollerdb.txt",
            "controller database",
        )
        copy_file(
            controller_db, appdir / "usr/mm/gamecontrollerdb.txt", "controller database"
        )

    desktop = PACKAGING / "io.github.SomeoneIsWorking.Zelda3D.desktop"
    icon = PACKAGING / "io.github.SomeoneIsWorking.Zelda3D.svg"
    metainfo = PACKAGING / "io.github.SomeoneIsWorking.Zelda3D.metainfo.xml"
    (appdir / "AppRun").symlink_to("usr/bin/zelda3d")
    copy_file(desktop, appdir / desktop.name, "desktop entry")
    copy_file(icon, appdir / icon.name, "SVG icon")
    copy_file(
        desktop, appdir / "usr/share/applications" / desktop.name, "desktop entry"
    )
    copy_file(
        icon, appdir / "usr/share/icons/hicolor/scalable/apps" / icon.name, "SVG icon"
    )
    copy_file(
        metainfo,
        appdir / "usr/share/metainfo/io.github.SomeoneIsWorking.Zelda3D.appdata.xml",
        "AppStream metadata",
    )
    for executable in (
        appdir / "AppRun",
        appdir / "usr/bin/zelda3d",
        appdir / "usr/soh/libsoh_core.so",
        appdir / "usr/mm/libmm_core.so",
    ):
        executable.chmod(0o755)
    assert_complete_appdir(appdir)
    assert_no_game_files(appdir)


def resolve_tool(name: str, configured: str | None) -> str:
    resolved = configured or shutil.which(name)
    if not resolved:
        refuse(f"{name} is required; install it or pass --{name}")
    return resolved


def build_appimage(args: argparse.Namespace) -> None:
    linuxdeploy = resolve_tool("linuxdeploy", args.linuxdeploy)
    appimagetool = resolve_tool("appimagetool", args.appimagetool)
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = reset_directory(raw / "appimage-package")
    appdir = temporary / "AppDir"
    try:
        stage_appdir(
            appdir,
            args.build_dir,
            ROOT / "Shipwright/soh/assets",
            ROOT / "2ship/assets",
            args.mm_port_archive,
        )
        subprocess.run(
            [
                linuxdeploy,
                "--appdir",
                str(appdir),
                "--executable",
                str(appdir / "usr/bin/zelda3d"),
                "--deploy-deps-only",
                str(appdir / "usr/soh/libsoh_core.so"),
                "--deploy-deps-only",
                str(appdir / "usr/mm/libmm_core.so"),
                "--desktop-file",
                str(appdir / "io.github.SomeoneIsWorking.Zelda3D.desktop"),
                "--icon-file",
                str(appdir / "io.github.SomeoneIsWorking.Zelda3D.svg"),
            ],
            cwd=temporary,
            check=True,
        )
        app_run = appdir / "AppRun"
        if app_run.exists() or app_run.is_symlink():
            app_run.unlink()
        app_run.symlink_to("usr/bin/zelda3d")
        assert_complete_appdir(appdir)
        assert_no_game_files(appdir)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [appimagetool, str(appdir), str(args.output)], cwd=temporary, check=True
        )
    finally:
        shutil.rmtree(temporary)
    require_file(args.output, "AppImage output")
    args.output.chmod(0o755)
    print(f"appimage: created {args.output} ({args.output.stat().st_size} bytes)")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def verify_appimage(appimage: Path, readelf: str) -> None:
    require_file(appimage, "AppImage")
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = reset_directory(raw / "appimage-verify")
    try:
        subprocess.run(
            [str(appimage), "--appimage-extract"],
            cwd=temporary,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        appdir = require_directory(
            temporary / "squashfs-root", "extracted AppImage payload"
        )
        assert_complete_appdir(appdir)
        assert_no_game_files(appdir)
        assert_compatible_abi(appdir, appimage, readelf)
    finally:
        shutil.rmtree(temporary)

    environment = os.environ.copy()
    environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    subprocess.run([str(appimage), "--help"], check=True, env=environment)
    subprocess.run([str(appimage), "--probe-cores"], check=True, env=environment)
    digest = sha256_file(appimage)
    print(f"appimage: verified {appimage} sha256={digest}")


def selftest() -> int:
    parsed = parse_abi_versions(
        "Name: GLIBC_2.35 Flags: none  Name: GLIBCXX_3.4.30  Name: GLIBC_PRIVATE"
    )
    expected = {("GLIBC", (2, 35)), ("GLIBCXX", (3, 4, 30))}
    if parsed != expected:
        refuse(f"ABI version parser selftest failed: expected {expected}, got {parsed}")
    raw = SCRATCH / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    temporary = reset_directory(raw / "appimage-selftest")
    try:
        build = temporary / "build"
        oot_assets = temporary / "oot-assets"
        mm_assets = temporary / "mm-assets"
        for path in (
            build / "zelda3d/zelda3d",
            build / "soh/libsoh_core.so",
            build / "mm/libmm_core.so",
            build / "soh/soh.o2r",
            build / "mm/2ship.o2r",
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix == ".o2r":
                with zipfile.ZipFile(path, "w") as archive:
                    archive.writestr("portVersion", "fixture")
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(path.name.encode())
        fixture_files = (
            oot_assets / "extractor/Config_N64_NTSC_10.xml",
            mm_assets / "extractor/Config_N64_US.xml",
            oot_assets / "xml/GC_MQ_D/audio/Audio.xml",
            mm_assets / "xml/GC_US/audio/Audio.xml",
        )
        for path in fixture_files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture", encoding="utf-8")
        appdir = temporary / "AppDir"
        stage_appdir(appdir, build, oot_assets, mm_assets, build / "mm/2ship.o2r")
        assert_complete_appdir(appdir)
        print("package_appimage selftest: complete=True no-game-files=True")
        return 0
    finally:
        shutil.rmtree(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=BUILD)
    parser.add_argument(
        "--mm-port-archive",
        type=Path,
        help="redistributable custom 2ship.o2r (defaults to BUILD/mm/2ship.o2r)",
    )
    parser.add_argument(
        "--output", type=Path, default=SCRATCH / "release/Zelda3D-x86_64.AppImage"
    )
    parser.add_argument("--linuxdeploy")
    parser.add_argument("--appimagetool")
    parser.add_argument("--readelf")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    args.output = args.output.resolve()
    args.build_dir = args.build_dir.resolve()
    args.mm_port_archive = (
        args.mm_port_archive.resolve()
        if args.mm_port_archive is not None
        else args.build_dir / "mm/2ship.o2r"
    )
    if args.verify:
        verify_appimage(args.output, resolve_tool("readelf", args.readelf))
        return 0
    build_appimage(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
