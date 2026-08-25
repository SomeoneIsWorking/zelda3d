"""Tests for shared ROM discovery and first-run extraction provisioning."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import rom_provision


class RomProvisionTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def _write_oot_n64(path: Path) -> None:
        header = bytearray(0x40)
        header[:4] = b"\x80\x37\x12\x40"
        header[0x20:0x34] = b"THE LEGEND OF ZELDA "
        header[0x3B:0x3E] = b"CZL"
        path.write_bytes(header)

    @staticmethod
    def _write_oot3d(path: Path) -> None:
        data = bytearray(0x600)
        data[0x100:0x104] = b"NCSD"
        struct.pack_into("<I", data, 0x120, 1)
        data[0x300:0x304] = b"NCCH"
        data[0x350:0x35A] = b"CTR-P-AQEE"
        data[0x38F] = 0x04
        path.write_bytes(data)

    def test_caller_values_win_without_being_rewritten(self) -> None:
        (self.repo / ".env").write_text(
            "ZELDA3D_OOT3D_ROM=env.3ds\nZELDA3D_OOT_ROM=env.z64\n"
        )
        environment = rom_provision.resolve_rom_environment(
            self.repo,
            {
                rom_provision.OOT3D_NAME: "caller.3ds",
                rom_provision.OOT_NAME: "caller.z64",
            },
        )
        self.assertEqual(environment[rom_provision.OOT3D_NAME], "caller.3ds")
        self.assertEqual(environment[rom_provision.OOT_NAME], "caller.z64")

    def test_env_relative_paths_are_resolved_from_repo(self) -> None:
        (self.repo / ".env").write_text("ZELDA3D_OOT3D_ROM=roms/game.3ds\n")
        environment = rom_provision.resolve_rom_environment(self.repo, {})
        self.assertEqual(
            environment[rom_provision.OOT3D_NAME],
            str((self.repo / "roms" / "game.3ds").resolve()),
        )

    def test_canonical_drop_in_wins_over_alphabetical_glob(self) -> None:
        self._write_oot3d(self.repo / "aaa.3ds")
        canonical = self.repo / "oot3d.3ds"
        self._write_oot3d(canonical)
        environment = rom_provision.resolve_rom_environment(self.repo, {})
        self.assertEqual(
            environment[rom_provision.OOT3D_NAME], str(canonical.resolve())
        )

    def test_provision_creates_app_directory_and_safe_symlink(self) -> None:
        rom = self.repo / "game.z64"
        self._write_oot_n64(rom)
        app_dir = self.repo / "build" / "soh"
        link = rom_provision.provision_n64_extraction_rom(
            app_dir, {rom_provision.OOT_NAME: str(rom)}
        )
        self.assertEqual(link, app_dir / "zelda3d-source.z64")
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.resolve(), rom.resolve())

    def test_existing_archive_skips_extraction_link(self) -> None:
        rom = self.repo / "game.z64"
        self._write_oot_n64(rom)
        app_dir = self.repo / "app"
        app_dir.mkdir()
        (app_dir / "oot.o2r").touch()
        self.assertIsNone(
            rom_provision.provision_n64_extraction_rom(
                app_dir, {rom_provision.OOT_NAME: str(rom)}
            )
        )

    def test_missing_n64_rom_does_not_block_prebuilt_archive_path(self) -> None:
        app_dir = self.repo / "app"
        self.assertIsNone(rom_provision.provision_n64_extraction_rom(app_dir, {}))
        self.assertFalse(app_dir.exists())

    def test_existing_unrelated_extraction_link_is_left_untouched(self) -> None:
        first = self.repo / "first.z64"
        second = self.repo / "second.z64"
        self._write_oot_n64(first)
        self._write_oot_n64(second)
        app_dir = self.repo / "app"
        app_dir.mkdir()
        destination = app_dir / "zelda3d-source.z64"
        destination.symlink_to(first)
        self.assertIsNone(
            rom_provision.provision_n64_extraction_rom(
                app_dir, {rom_provision.OOT_NAME: str(second)}
            )
        )
        self.assertEqual(destination.resolve(), first.resolve())

    def test_missing_oot3d_rom_names_the_supported_sources(self) -> None:
        with self.assertRaisesRegex(
            rom_provision.RomProvisionError, "set ZELDA3D_OOT3D_ROM"
        ):
            rom_provision.require_oot3d_rom(self.repo, {})


if __name__ == "__main__":
    unittest.main()
