"""Early native-dependency diagnostics with platform package guidance."""

from __future__ import annotations

import shlex
import shutil
import subprocess
import sys
from collections.abc import Callable, Mapping, MutableMapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from platform import machine

WINDOWS_VCPKG_PORTS = (
    "zlib",
    "bzip2",
    "libzip",
    "libpng",
    "sdl3",
    "glfw3",
    "nlohmann-json",
    "tinyxml2",
    "spdlog",
    "libogg",
    "libvorbis",
    "opus",
    "opusfile",
    "freetype",
)

PKG_CONFIG_MODULES = (
    "sdl3",
    "libpng",
    "libzip",
    "nlohmann_json",
    "tinyxml2",
    "spdlog",
    "opusfile",
    "vorbisfile",
    "freetype2",
)


@dataclass(frozen=True)
class DependencyReport:
    missing_tools: tuple[str, ...]
    missing_libraries: tuple[str, ...]

    @property
    def ready(self) -> bool:
        return not self.missing_tools and not self.missing_libraries


class MissingNativeDependencies(RuntimeError):
    """Raised before CMake when required host packages can be named precisely."""


def windows_vcpkg_triplet(host_machine: str | None = None) -> str:
    architecture = (host_machine or machine()).lower()
    if architecture in {"arm64", "aarch64"}:
        return "arm64-windows-static"
    return "x64-windows-static"


def prepare_windows_toolchain_environment(
    source: Mapping[str, str], *, host_machine: str | None = None
) -> dict[str, str]:
    """Derive CMake's user-owned vcpkg toolchain without downloading anything."""
    environment = dict(source)
    root_value = environment.get("VCPKG_ROOT")
    if not root_value:
        return environment
    root = Path(root_value).expanduser().resolve()
    triplet = environment.get("VCPKG_TARGET_TRIPLET") or windows_vcpkg_triplet(
        host_machine
    )
    environment["VCPKG_ROOT"] = str(root)
    environment["VCPKG_TARGET_TRIPLET"] = triplet
    environment["VCPKG_DEFAULT_TRIPLET"] = triplet
    environment.setdefault(
        "CMAKE_TOOLCHAIN_FILE",
        str(root / "scripts" / "buildsystems" / "vcpkg.cmake"),
    )
    return environment


def propagate_cmake_environment(
    source: Mapping[str, str], target: MutableMapping[str, str]
) -> None:
    for name in (
        "CMAKE_TOOLCHAIN_FILE",
        "VCPKG_ROOT",
        "VCPKG_TARGET_TRIPLET",
        "VCPKG_DEFAULT_TRIPLET",
    ):
        if source.get(name):
            target[name] = source[name]


def _command_name(value: str) -> str:
    arguments = shlex.split(value)
    return arguments[0] if arguments else value


def _available_compiler(
    environment: Mapping[str, str],
    variable: str,
    candidates: Sequence[str],
    which: Callable[[str], str | None],
) -> bool:
    selected = environment.get(variable)
    if selected:
        return which(_command_name(selected)) is not None
    return any(which(candidate) is not None for candidate in candidates)


def inspect_dependencies(
    environment: Mapping[str, str],
    *,
    platform: str = sys.platform,
    which: Callable[[str], str | None] = shutil.which,
    pkg_config_exists: Callable[[str], bool] | None = None,
) -> DependencyReport:
    missing_tools = [name for name in ("cmake", "ninja", "git") if which(name) is None]
    c_candidates = ("cl",) if platform == "win32" else ("cc", "gcc", "clang")
    cxx_candidates = ("cl",) if platform == "win32" else ("c++", "g++", "clang++")
    if not _available_compiler(environment, "CC", c_candidates, which):
        missing_tools.append(environment.get("CC") or "C compiler")
    if not _available_compiler(environment, "CXX", cxx_candidates, which):
        missing_tools.append(environment.get("CXX") or "C++ compiler")

    if platform == "win32":
        root_value = environment.get("VCPKG_ROOT")
        if not root_value:
            missing_tools.append("VCPKG_ROOT/vcpkg")
            return DependencyReport(tuple(missing_tools), WINDOWS_VCPKG_PORTS)
        root = Path(root_value).expanduser()
        triplet = environment.get("VCPKG_TARGET_TRIPLET") or windows_vcpkg_triplet()
        if not (root / "vcpkg.exe").is_file():
            missing_tools.append(str(root / "vcpkg.exe"))
        if not (root / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
            missing_tools.append(str(root / "scripts" / "buildsystems" / "vcpkg.cmake"))
        installed = root / "installed" / triplet / "share"
        missing_libraries = [
            port for port in WINDOWS_VCPKG_PORTS if not (installed / port).is_dir()
        ]
        return DependencyReport(tuple(missing_tools), tuple(missing_libraries))
    if which("pkg-config") is None:
        missing_tools.append("pkg-config")
        return DependencyReport(tuple(missing_tools), PKG_CONFIG_MODULES)

    checker = pkg_config_exists or _pkg_config_exists
    missing_libraries = [module for module in PKG_CONFIG_MODULES if not checker(module)]
    return DependencyReport(tuple(missing_tools), tuple(missing_libraries))


def _pkg_config_exists(module: str) -> bool:
    return (
        subprocess.run(["pkg-config", "--exists", module], check=False).returncode == 0
    )


def _linux_family(os_release: Path = Path("/etc/os-release")) -> str:
    try:
        values = {
            name: value.strip('"')
            for name, value in (
                line.split("=", 1)
                for line in os_release.read_text(encoding="utf-8").splitlines()
                if "=" in line
            )
        }
    except OSError:
        return "unknown"
    identifiers = f"{values.get('ID', '')} {values.get('ID_LIKE', '')}".split()
    if any(name in identifiers for name in ("fedora", "rhel", "centos")):
        return "dnf"
    if any(name in identifiers for name in ("debian", "ubuntu")):
        return "apt"
    if any(name in identifiers for name in ("arch", "manjaro")):
        return "pacman"
    return "unknown"


def installation_guidance(
    platform: str = sys.platform, *, windows_triplet: str | None = None
) -> str:
    if platform == "darwin":
        return (
            "Install the missing native dependencies, then rerun ./run.sh:\n"
            "  xcode-select --install\n"
            "  brew install cmake ninja pkgconf sdl3 libpng libzip nlohmann-json "
            "tinyxml2 spdlog opus libogg libvorbis freetype bzip2"
        )
    if platform == "win32":
        triplet = windows_triplet or windows_vcpkg_triplet()
        ports = " ".join(WINDOWS_VCPKG_PORTS)
        return (
            "Install the C++ build workload and user-owned vcpkg dependencies, then rerun "
            "run.sh from its developer shell:\n"
            "  winget install Kitware.CMake Ninja-build.Ninja\n"
            "  winget install Microsoft.VisualStudio.2022.BuildTools --override "
            '"--wait --passive --add Microsoft.VisualStudio.Workload.VCTools '
            '--includeRecommended"\n'
            "  git clone https://github.com/microsoft/vcpkg.git C:\\vcpkg\n"
            "  C:\\vcpkg\\bootstrap-vcpkg.bat\n"
            f"  C:\\vcpkg\\vcpkg.exe install --triplet {triplet} {ports}\n"
            "  set VCPKG_ROOT=C:\\vcpkg"
        )
    family = _linux_family()
    if family == "dnf":
        return (
            "Install the missing native dependencies yourself, then rerun ./run.sh:\n"
            "  sudo dnf install cmake ninja-build gcc gcc-c++ git pkgconf-pkg-config "
            "SDL3-devel libpng-devel libzip-devel nlohmann-json-devel tinyxml2-devel "
            "spdlog-devel opusfile-devel libogg-devel libvorbis-devel freetype-devel "
            "bzip2-devel zlib-devel"
        )
    if family == "apt":
        return (
            "Install the missing native dependencies yourself, then rerun ./run.sh:\n"
            "  sudo apt install build-essential cmake ninja-build git pkg-config libsdl3-dev "
            "libpng-dev libzip-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev "
            "libopusfile-dev libogg-dev libvorbis-dev libfreetype-dev libbz2-dev zlib1g-dev"
        )
    if family == "pacman":
        return (
            "Install the missing native dependencies yourself, then rerun ./run.sh:\n"
            "  sudo pacman -S --needed base-devel cmake ninja git pkgconf sdl3 libpng "
            "libzip nlohmann-json tinyxml2 spdlog opusfile libogg libvorbis freetype2 bzip2 zlib"
        )
    return (
        "Install CMake, Ninja, Git, pkg-config, a supported C/C++ compiler, SDL3, libpng, "
        "libzip, nlohmann-json, tinyxml2, spdlog, opusfile, libvorbis, and FreeType "
        "with your system package manager, then rerun ./run.sh."
    )


def require_dependencies(
    environment: Mapping[str, str], *, platform: str = sys.platform
) -> None:
    report = inspect_dependencies(environment, platform=platform)
    if report.ready:
        return
    details = []
    if report.missing_tools:
        details.append("missing tools: " + ", ".join(report.missing_tools))
    if report.missing_libraries:
        details.append("missing libraries: " + ", ".join(report.missing_libraries))
    triplet = environment.get("VCPKG_TARGET_TRIPLET") if platform == "win32" else None
    raise MissingNativeDependencies(
        "; ".join(details)
        + "\n"
        + installation_guidance(platform, windows_triplet=triplet)
    )
