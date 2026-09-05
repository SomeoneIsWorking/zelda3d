"""Immutable native dependency sources shared by CI and release builders."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class NativeSource:
    name: str
    url: str
    revision: str
    options: tuple[str, ...]


SDL = NativeSource(
    "sdl",
    "https://github.com/libsdl-org/SDL.git",
    "683181b47cfabd293e3ea409f838915b8297a4fd",  # release-3.4.2
    ("-DSDL_TESTS=OFF", "-DSDL_TEST_LIBRARY=OFF"),
)
TINYXML2 = NativeSource(
    "tinyxml2",
    "https://github.com/leethomason/tinyxml2.git",
    "9148bdf719e997d1f474be6bcc7943881046dba1",  # 11.0.0
    ("-DBUILD_SHARED_LIBS=ON",),
)
GLSLANG = NativeSource(
    "glslang",
    "https://github.com/KhronosGroup/glslang.git",
    "f0bd0257c308b9a26562c1a30c4748a0219cc951",  # 16.2.0
    (
        "-DBUILD_SHARED_LIBS=OFF",
        "-DBUILD_TESTING=OFF",
        "-DENABLE_CTEST=OFF",
        "-DENABLE_GLSLANG_BINARIES=OFF",
        "-DENABLE_OPT=OFF",
    ),
)
LUCENT = NativeSource(
    "lucent",
    "https://github.com/SomeoneIsWorking/lucent.git",
    "cac301db85b1f0aabdec8bdcf31a13cae560d8e2",
    ("-DLUCENT_BUILD_ZIP=ON", "-DLUCENT_BUILD_TESTS=OFF"),
)
SOURCES = {source.name: source for source in (TINYXML2, GLSLANG, SDL, LUCENT)}


def require_pinned_checkout(source: NativeSource, directory: Path) -> None:
    if not directory.exists():
        directory.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["git", "clone", "--no-checkout", source.url, str(directory)], check=True
        )
        subprocess.run(
            ["git", "-C", str(directory), "checkout", "--detach", source.revision],
            check=True,
        )
    revision = subprocess.check_output(
        ["git", "-C", str(directory), "rev-parse", "HEAD"], text=True
    ).strip()
    dirty = subprocess.check_output(
        ["git", "-C", str(directory), "status", "--porcelain"], text=True
    ).strip()
    if revision != source.revision or dirty:
        raise RuntimeError(
            f"{source.name} source must be clean at {source.revision}: {directory}"
        )


def build_source(source: NativeSource, root: Path, prefix: Path, jobs: int = 2) -> Path:
    if jobs < 1:
        raise ValueError("native build jobs must be positive")
    directory = root / f"{source.name}-source"
    build = root / f"{source.name}-build"
    require_pinned_checkout(source, directory)
    subprocess.run(
        [
            "cmake",
            "-S",
            str(directory),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={prefix}",
            *source.options,
        ],
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(build), "--target", "install", f"-j{jobs}"], check=True
    )
    return prefix
