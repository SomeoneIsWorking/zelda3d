"""Focused contract tests for MM cold-path ROM and archive provisioning."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOLS = REPO / "tools"
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(TOOLS))

from launcher_bootstrap.mm_assets import (
    MM3D_ROM_NAME,
    MM_ROM_NAME,
    MmAssetError,
    MmAssetLayout,
    ensure_mm_runtime_archives,
    extract_mm_runtime_archives,
    resolve_mm_rom_environment,
)


class MmBootstrapAssetTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _prepare_sources(self) -> MmAssetLayout:
        (self.repo / "CMakeLists.txt").write_text(
            "project(Ship VERSION 9.2.3 LANGUAGES C CXX)\n", encoding="utf-8"
        )
        (self.repo / "Shipwright" / "OTRExporter").mkdir(parents=True)
        (self.repo / "Shipwright" / "OTRExporter" / "extract_assets.py").touch()
        (self.repo / "2ship" / "assets" / "extractor").mkdir(parents=True)
        (self.repo / "2ship" / "assets" / "extractor" / "Config_N64_US.xml").touch()
        (self.repo / "2ship" / "assets" / "xml").mkdir()
        (self.repo / "2ship" / "assets" / "custom").mkdir()
        return MmAssetLayout.for_repo(self.repo)

    @staticmethod
    def _write_configuration(layout: MmAssetLayout, python_executable: Path) -> None:
        layout.extraction_build_dir.mkdir(parents=True, exist_ok=True)
        (layout.extraction_build_dir / "build.ninja").write_text("# fixture\n")
        (layout.extraction_build_dir / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\n"
            "CMAKE_C_COMPILER:FILEPATH=/usr/bin/cc\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n"
            "GAME_STR:UNINITIALIZED=MM\n"
            f"Python3_EXECUTABLE:FILEPATH={python_executable.resolve()}\n"
        )

    @staticmethod
    def _write_archive(path: Path, members: tuple[str, ...]) -> None:
        with zipfile.ZipFile(path, "w") as archive:
            for member in members:
                archive.writestr(member, b"fixture")

    @staticmethod
    def _write_n64_rom(path: Path, *, mm: bool = True) -> None:
        header = bytearray(0x40)
        header[:4] = b"\x80\x37\x12\x40"
        header[0x20:0x34] = b"ZELDA MAJORA MASK   " if mm else b"THE LEGEND OF ZELDA "
        header[0x3B:0x3E] = b"NZS" if mm else b"CZL"
        path.write_bytes(header)

    @staticmethod
    def _write_3ds_rom(
        path: Path, *, mm: bool = True, no_crypto_flag: bool = True
    ) -> None:
        data = bytearray(0x600)
        data[0x100:0x104] = b"NCSD"
        struct.pack_into("<I", data, 0x120, 1)
        data[0x300:0x304] = b"NCCH"
        data[0x350:0x35A] = b"CTR-P-AJRE" if mm else b"CTR-P-AQEE"
        data[0x38F] = 0x04 if no_crypto_flag else 0
        path.write_bytes(data)

    def test_discovery_priority_is_caller_then_env_then_canonical_drop_in(self) -> None:
        caller_mm = self.repo / "caller-mm.z64"
        env_mm = self.repo / "env-mm.z64"
        canonical_mm = self.repo / "mm.z64"
        canonical_mm3d = self.repo / "mm3d.3ds"
        fallback_mm3d = self.repo / "aaa.3ds"
        for rom in (caller_mm, env_mm, canonical_mm):
            self._write_n64_rom(rom)
        self._write_3ds_rom(canonical_mm3d)
        self._write_3ds_rom(fallback_mm3d, mm=False)
        (self.repo / ".env").write_text(
            f"{MM_ROM_NAME}={env_mm.name}\n", encoding="utf-8"
        )

        environment = resolve_mm_rom_environment(
            self.repo, {MM_ROM_NAME: str(caller_mm)}
        )

        self.assertEqual(environment[MM_ROM_NAME], str(caller_mm.resolve()))
        self.assertEqual(environment[MM3D_ROM_NAME], str(canonical_mm3d.resolve()))

    def test_caller_values_are_preserved_exactly(self) -> None:
        environment = resolve_mm_rom_environment(
            self.repo,
            {MM_ROM_NAME: "relative/mm.z64", MM3D_ROM_NAME: "relative/mm3d.3ds"},
        )
        self.assertEqual(environment[MM_ROM_NAME], "relative/mm.z64")
        self.assertEqual(environment[MM3D_ROM_NAME], "relative/mm3d.3ds")

    def test_relative_env_values_are_resolved_from_repo(self) -> None:
        rom_dir = self.repo / "roms"
        rom_dir.mkdir()
        mm = rom_dir / "game.z64"
        mm3d = rom_dir / "game.3ds"
        self._write_n64_rom(mm)
        self._write_3ds_rom(mm3d)
        (self.repo / ".env").write_text(
            f"{MM_ROM_NAME}=roms/game.z64\n{MM3D_ROM_NAME}=roms/game.3ds\n",
            encoding="utf-8",
        )

        environment = resolve_mm_rom_environment(self.repo, {})

        self.assertEqual(environment[MM_ROM_NAME], str(mm.resolve()))
        self.assertEqual(environment[MM3D_ROM_NAME], str(mm3d.resolve()))

    def test_generic_drop_ins_are_selected_by_game_identity(self) -> None:
        oot = self.repo / "a.z64"
        mm = self.repo / "b.z64"
        oot3d = self.repo / "a.3ds"
        mm3d = self.repo / "b.3ds"
        self._write_n64_rom(oot, mm=False)
        self._write_n64_rom(mm)
        self._write_3ds_rom(oot3d, mm=False)
        self._write_3ds_rom(mm3d)

        environment = resolve_mm_rom_environment(self.repo, {})

        self.assertEqual(environment[MM_ROM_NAME], str(mm.resolve()))
        self.assertEqual(environment[MM3D_ROM_NAME], str(mm3d.resolve()))

    def test_product_identity_does_not_guess_decryption_from_ncch_flag(self) -> None:
        mm3d = self.repo / "mm-retained-flag.3ds"
        self._write_3ds_rom(mm3d, no_crypto_flag=False)

        environment = resolve_mm_rom_environment(self.repo, {})

        self.assertEqual(environment[MM3D_ROM_NAME], str(mm3d.resolve()))

    def test_ambiguous_mm_drop_ins_refuse_to_guess(self) -> None:
        self._write_n64_rom(self.repo / "one.z64")
        self._write_n64_rom(self.repo / "two.z64")
        with self.assertRaisesRegex(MmAssetError, "multiple Majora's Mask"):
            resolve_mm_rom_environment(self.repo, {})

    def test_extraction_uses_locked_python_mm_build_and_validates_copies(self) -> None:
        layout = self._prepare_sources()
        mm = self.repo / "mm.z64"
        mm3d = self.repo / "mm3d.3ds"
        self._write_n64_rom(mm)
        self._write_3ds_rom(mm3d)
        commands: list[tuple[list[str], Path]] = []
        locked_python = self.repo / ".venv" / "bin" / "python"

        def runner(command, cwd):
            command = list(command)
            commands.append((command, cwd))
            if "-S" in command:
                self._write_configuration(layout, locked_python)
            if "--target" in command:
                layout.zapd.parent.mkdir(parents=True, exist_ok=True)
                layout.zapd.touch()
            if str(layout.extractor) in command:
                self._write_archive(layout.generated_archives[0], ("version", "asset"))
                self._write_archive(
                    layout.generated_archives[1], ("portVersion", "asset")
                )

        outputs = extract_mm_runtime_archives(
            layout,
            {MM_ROM_NAME: str(mm), MM3D_ROM_NAME: str(mm3d)},
            python_executable=locked_python,
            jobs=7,
            runner=runner,
        )

        configure, build, extract = commands
        self.assertIn("-DGAME_STR=MM", configure[0])
        self.assertIn(f"-DPython3_EXECUTABLE={locked_python}", configure[0])
        self.assertEqual(build[0][-2:], ["ZAPD", "-j7"])
        self.assertEqual(extract[0][0], str(locked_python))
        self.assertEqual(extract[1], layout.mm_source_dir)
        self.assertIn(str(layout.mm_source_dir / "assets" / "xml"), extract[0])
        self.assertEqual(outputs, layout.runtime_archives)
        self.assertTrue(all(path.is_file() for path in outputs))

    def test_mismatched_cached_python_forces_fresh_mm_reconfigure(self) -> None:
        layout = self._prepare_sources()
        locked_python = self.repo / ".venv" / "bin" / "python"
        self._write_configuration(layout, Path("/usr/bin/python3"))
        mm = self.repo / "mm.z64"
        mm3d = self.repo / "mm3d.3ds"
        self._write_n64_rom(mm)
        self._write_3ds_rom(mm3d)
        commands: list[list[str]] = []

        def runner(command, _cwd):
            command = list(command)
            commands.append(command)
            if "-S" in command:
                self._write_configuration(layout, locked_python)
            elif "--target" in command:
                layout.zapd.parent.mkdir(parents=True, exist_ok=True)
                layout.zapd.touch()
            else:
                self._write_archive(layout.generated_archives[0], ("version",))
                self._write_archive(layout.generated_archives[1], ("portVersion",))

        extract_mm_runtime_archives(
            layout,
            {MM_ROM_NAME: str(mm), MM3D_ROM_NAME: str(mm3d)},
            python_executable=locked_python,
            runner=runner,
        )

        self.assertIn("--fresh", commands[0])
        self.assertIn(f"-DPython3_EXECUTABLE={locked_python}", commands[0])

    def test_valid_runtime_pair_skips_extraction(self) -> None:
        layout = self._prepare_sources()
        layout.runtime_dir.mkdir(parents=True)
        self._write_archive(layout.runtime_archives[0], ("version",))
        self._write_archive(layout.runtime_archives[1], ("portVersion",))

        outputs = ensure_mm_runtime_archives(
            layout,
            {},
            python_executable="/locked/python",
            runner=lambda _command, _cwd: self.fail("unexpected extraction"),
        )

        self.assertEqual(outputs, layout.runtime_archives)

    def test_missing_extractor_outputs_cannot_be_masked_by_stale_archives(self) -> None:
        layout = self._prepare_sources()
        mm = self.repo / "mm.z64"
        mm3d = self.repo / "mm3d.3ds"
        self._write_n64_rom(mm)
        self._write_3ds_rom(mm3d)
        self._write_archive(layout.generated_archives[0], ("version",))
        self._write_archive(layout.generated_archives[1], ("portVersion",))

        def runner(command, _cwd):
            if "-S" in command:
                self._write_configuration(layout, Path("/locked/python"))
            if "--target" in command:
                layout.zapd.parent.mkdir(parents=True, exist_ok=True)
                layout.zapd.touch()

        with self.assertRaisesRegex(MmAssetError, "did not produce"):
            extract_mm_runtime_archives(
                layout,
                {MM_ROM_NAME: str(mm), MM3D_ROM_NAME: str(mm3d)},
                python_executable="/locked/python",
                runner=runner,
            )

    def test_corrupt_archive_is_rejected_before_runtime_copy(self) -> None:
        layout = self._prepare_sources()
        mm = self.repo / "mm.z64"
        mm3d = self.repo / "mm3d.3ds"
        self._write_n64_rom(mm)
        self._write_3ds_rom(mm3d)

        def runner(command, _cwd):
            if "-S" in command:
                self._write_configuration(layout, Path("/locked/python"))
            if "--target" in command:
                layout.zapd.parent.mkdir(parents=True, exist_ok=True)
                layout.zapd.touch()
            if str(layout.extractor) in command:
                layout.generated_archives[0].write_bytes(b"not a zip")
                self._write_archive(layout.generated_archives[1], ("portVersion",))

        with self.assertRaisesRegex(MmAssetError, "invalid O2R"):
            extract_mm_runtime_archives(
                layout,
                {MM_ROM_NAME: str(mm), MM3D_ROM_NAME: str(mm3d)},
                python_executable="/locked/python",
                runner=runner,
            )
        self.assertFalse(layout.runtime_archives[0].exists())


if __name__ == "__main__":
    unittest.main()
