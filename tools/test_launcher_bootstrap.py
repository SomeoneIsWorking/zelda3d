"""Tests for launcher bootstrap arguments, environment, and composition."""

from __future__ import annotations

import importlib.util
import os
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(TOOLS))

from launcher_bootstrap.arguments import parse_arguments
from launcher_bootstrap.native_dependencies import (
    PKG_CONFIG_MODULES,
    WINDOWS_VCPKG_PORTS,
    inspect_dependencies,
    installation_guidance,
    prepare_windows_toolchain_environment,
)
from launcher_bootstrap.runtime_environment import prepare_runtime_environment


def _load_bootstrap():
    spec = importlib.util.spec_from_file_location(
        "zelda3d_bootstrap", REPO / "bootstrap.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load bootstrap.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LauncherBootstrapTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.fixture = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_product_arguments_are_forwarded_unchanged(self) -> None:
        arguments = parse_arguments(
            ["--bootstrap-jobs", "3", "--", "oot", "--developer-option"]
        )
        self.assertEqual(arguments.jobs, 3)
        self.assertEqual(arguments.product_arguments, ("oot", "--developer-option"))

    def test_runtime_defaults_preserve_explicit_product_choices(self) -> None:
        source = {"ZELDA3D_AUTO": "0", "ZELDA3D_ENTRANCE": "17", "DISPLAY": ":9"}
        environment = prepare_runtime_environment(source, platform="linux")
        self.assertEqual(environment["ZELDA3D_AUTO"], "0")
        self.assertEqual(environment["ZELDA3D_ENTRANCE"], "17")
        self.assertEqual(environment["DISPLAY"], ":9")
        self.assertEqual(environment["SOH3D"], "1")
        self.assertEqual(
            source, {"ZELDA3D_AUTO": "0", "ZELDA3D_ENTRANCE": "17", "DISPLAY": ":9"}
        )

    def test_dependency_check_accepts_caller_selected_compilers(self) -> None:
        available = {
            "cmake",
            "ninja",
            "git",
            "pkg-config",
            "my-cc",
            "my-cxx",
        }
        report = inspect_dependencies(
            {"CC": "my-cc", "CXX": "my-cxx"},
            platform="linux",
            which=lambda name: f"/bin/{name}" if name in available else None,
            pkg_config_exists=lambda module: module in PKG_CONFIG_MODULES,
        )
        self.assertTrue(report.ready)

    def test_dnf_refusal_is_an_exact_user_run_command(self) -> None:
        with mock.patch(
            "launcher_bootstrap.native_dependencies._linux_family", return_value="dnf"
        ):
            guidance = installation_guidance("linux")
        self.assertIn("sudo dnf install", guidance)
        self.assertIn("SDL3-devel", guidance)

    def test_windows_toolchain_and_static_triplet_are_propagated(self) -> None:
        environment = prepare_windows_toolchain_environment(
            {"VCPKG_ROOT": r"C:\vcpkg"}, host_machine="ARM64"
        )
        self.assertEqual(environment["VCPKG_TARGET_TRIPLET"], "arm64-windows-static")
        self.assertEqual(environment["VCPKG_DEFAULT_TRIPLET"], "arm64-windows-static")
        self.assertTrue(
            environment["CMAKE_TOOLCHAIN_FILE"].endswith(
                "scripts/buildsystems/vcpkg.cmake"
            )
        )

    def test_windows_missing_ports_are_detected_before_cmake(self) -> None:
        root = self.fixture / "vcpkg"
        (root / "scripts" / "buildsystems").mkdir(parents=True)
        (root / "scripts" / "buildsystems" / "vcpkg.cmake").touch()
        (root / "vcpkg.exe").touch()
        available = {"cmake", "ninja", "git", "cl"}
        report = inspect_dependencies(
            {
                "VCPKG_ROOT": str(root),
                "VCPKG_TARGET_TRIPLET": "x64-windows-static",
            },
            platform="win32",
            which=lambda name: f"C:/bin/{name}" if name in available else None,
        )
        self.assertEqual(report.missing_libraries, WINDOWS_VCPKG_PORTS)

    def test_windows_guidance_is_exact_and_sdl3_only(self) -> None:
        guidance = installation_guidance(
            "win32", windows_triplet="arm64-windows-static"
        )
        self.assertIn("Microsoft.VisualStudio.Workload.VCTools", guidance)
        self.assertIn("vcpkg.exe install --triplet arm64-windows-static", guidance)
        self.assertIn(" sdl3 ", guidance)
        self.assertNotIn("sdl2", guidance.lower())

    def test_windows_cmake_contract_has_no_automatic_or_sdl2_dependency(self) -> None:
        root_cmake = (REPO / "CMakeLists.txt").read_text()
        windows_vcpkg = (
            REPO / "Shipwright/libultraship/cmake/dependencies/windows-vcpkg.cmake"
        ).read_text()
        windows_backend = (
            REPO / "Shipwright/libultraship/cmake/dependencies/windows.cmake"
        ).read_text()
        soh_cmake = (REPO / "Shipwright/soh/CMakeLists.txt").read_text()
        dependency_section = soh_cmake.split(
            "################################################################################\n"
            "# Dependencies\n",
            1,
        )[1]
        soh_windows = dependency_section.split(
            'if (CMAKE_SYSTEM_NAME STREQUAL "Windows")', 1
        )[1].split('elseif(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")', 1)[0]
        self.assertNotIn("vcpkg_bootstrap()", root_cmake)
        self.assertNotIn("vcpkg_install_packages(", root_cmake)
        self.assertNotIn("vcpkg_bootstrap()", windows_vcpkg)
        self.assertFalse((REPO / "Shipwright/CMake/automate-vcpkg.cmake").exists())
        self.assertFalse(
            (REPO / "Shipwright/libultraship/cmake/automate-vcpkg.cmake").exists()
        )
        self.assertNotIn("SDL2", windows_backend)
        self.assertNotIn("SDL2", soh_windows)
        self.assertIn("SDL3::SDL3", soh_windows)

    def test_run_sh_enters_repo_before_frozen_uv_from_another_cwd(self) -> None:
        fake_bin = self.fixture / "bin"
        fake_bin.mkdir()
        fake_uv = fake_bin / "uv"
        fake_uv.write_text('#!/bin/sh\nprintf \'%s\\n\' "$PWD" "$@"\n')
        fake_uv.chmod(fake_uv.stat().st_mode | stat.S_IXUSR)
        result = subprocess.run(
            [str(REPO / "run.sh"), "oot", "--developer-option"],
            cwd=self.fixture,
            env={**os.environ, "PATH": f"{fake_bin}:{os.environ['PATH']}"},
            check=True,
            capture_output=True,
            text=True,
        )
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], str(REPO))
        self.assertEqual(
            lines[1:],
            [
                "run",
                "--frozen",
                "python",
                str(REPO / "bootstrap.py"),
                "oot",
                "--developer-option",
            ],
        )

    def test_extraction_input_exists_before_build_and_locked_python_is_forwarded(
        self,
    ) -> None:
        bootstrap = _load_bootstrap()
        repo = self.fixture / "repo"
        app_dir = repo / "Shipwright" / "build-cmake" / "soh"
        oot3d = repo / "game.3ds"
        oot = repo / "game.z64"
        mm3d = repo / "mm-game.3ds"
        mm = repo / "mm-game.z64"
        repo.mkdir()
        oot_header = bytearray(0x40)
        oot_header[:4] = b"\x80\x37\x12\x40"
        oot_header[0x20:0x34] = b"THE LEGEND OF ZELDA "
        oot_header[0x3B:0x3E] = b"CZL"
        oot.write_bytes(oot_header)
        mm_header = bytearray(0x40)
        mm_header[:4] = b"\x80\x37\x12\x40"
        mm_header[0x20:0x34] = b"ZELDA MAJORA MASK   "
        mm_header[0x3B:0x3E] = b"NZS"
        mm.write_bytes(mm_header)
        oot3d_image = bytearray(0x600)
        oot3d_image[0x100:0x104] = b"NCSD"
        struct.pack_into("<I", oot3d_image, 0x120, 1)
        oot3d_image[0x300:0x304] = b"NCCH"
        oot3d_image[0x350:0x35A] = b"CTR-P-AQEE"
        oot3d_image[0x38F] = 0x04
        oot3d.write_bytes(oot3d_image)
        mm3d_image = bytearray(oot3d_image)
        mm3d_image[0x350:0x35A] = b"CTR-P-AJRE"
        mm3d.write_bytes(mm3d_image)
        observed: dict[str, object] = {}
        order: list[str] = []

        def ensure_mm(_layout, _environment, *, python_executable, jobs):
            order.append("mm")
            observed["mm_python"] = python_executable
            observed["mm_jobs"] = jobs

        def build(
            _layout,
            _runner=bootstrap.launcher_build.run_command,
            *,
            python_executable=None,
        ):
            order.append("build")
            observed["link_present"] = (app_dir / "zelda3d-source.z64").is_symlink()
            observed["python"] = python_executable

        with (
            mock.patch.object(bootstrap, "REPO", repo),
            mock.patch.object(bootstrap, "require_dependencies"),
            mock.patch.object(bootstrap, "ensure_build_sources"),
            mock.patch.object(
                bootstrap,
                "propagate_cmake_environment",
                side_effect=lambda _source, _target: order.append("cmake-environment"),
            ),
            mock.patch.object(
                bootstrap, "ensure_mm_runtime_archives", side_effect=ensure_mm
            ),
            mock.patch.object(
                bootstrap.launcher_build, "ensure_launcher_build", side_effect=build
            ),
            mock.patch.dict(
                os.environ,
                {
                    "ZELDA3D_OOT3D_ROM": str(oot3d),
                    "ZELDA3D_OOT_ROM": str(oot),
                    "ZELDA3D_MM3D_ROM": str(mm3d),
                    "ZELDA3D_MM_ROM": str(mm),
                },
                clear=True,
            ),
        ):
            self.assertEqual(bootstrap.run(["--bootstrap-prepare-only"]), 0)
        self.assertTrue(observed["link_present"])
        self.assertEqual(observed["python"], sys.executable)
        self.assertEqual(observed["mm_python"], sys.executable)
        self.assertEqual(order, ["cmake-environment", "mm", "build"])


if __name__ == "__main__":
    unittest.main()
