"""Tests for the oracle-ROM `no_crypto` header correction.

This is the single byte that decides whether Majora's Mask 3D is observable at all, so the properties
pinned here are the ones that make it safe and the ones that stop it being wrong quietly:

* it changes exactly ONE byte, and that byte is the NCCH `flags` field -- a content-decrypted image is
  already plaintext, so the correction describes reality rather than bypassing anything;
* it REFUSES an image whose partition 0 is not an NCCH, instead of flipping a bit at a guessed offset
  in an unknown layout;
* it is idempotent -- running it on an already-corrected image is a no-op, not a second change;
* `--check` writes nothing at all;
* and a control: the same reader on a known-good OoT3D image must report the bit already set, so a
  "0x04 for everything" bug cannot pass.

The layout constants are the thing most likely to rot, so they are asserted against the actual
on-disk header of a synthetic card rather than only against the code's own constants.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import ctr_oracle_rom as rom_mod  # noqa: E402


def synth_card(flags: int, *, magic: bytes = b"NCCH", size: int = 0x40000,
               magic_at: int | None = None) -> bytes:
    """A minimal 3DS card image with the given NCCH flags byte.

    `magic_at` defaults to the partition base. Both real dumps this project has put the magic
    0x100 PAST the base, and a tool that only accepts it at the base fails on a known-good image --
    which is exactly what happened, and the only reason it was caught is the OoT3D control case.
    """
    card = bytearray(size)
    at = rom_mod.NCCH_BASE if magic_at is None else magic_at
    card[at : at + 4] = magic
    card[rom_mod.NCCH_FLAGS_OFFSET] = flags
    return bytes(card)


class TheHeaderLayoutIsReadFromTheFile(unittest.TestCase):
    def test_flags_are_read_from_the_declared_offset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "card.3ds"
            for value in (0x00, 0x04, 0x3C, 0xFF):
                path.write_bytes(synth_card(value))
                self.assertEqual(rom_mod.read_flags(path), value)

    def test_the_magic_is_accepted_past_the_partition_base_for_a_dump_prefix(self) -> None:
        """Both real dumps carry a 0x100-byte prefix, so the magic is not at the base."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "card.3ds"
            path.write_bytes(synth_card(0x00, magic_at=rom_mod.NCCH_BASE + 0x100))
            self.assertEqual(rom_mod.read_flags(path), 0x00)

    def test_a_non_ncch_partition_is_refused_not_guessed(self) -> None:
        """Flipping a bit at a guessed offset in an unknown layout is how you corrupt a ROM."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "card.3ds"
            path.write_bytes(synth_card(0x00, magic=b"NAND"))
            with self.assertRaises(ValueError):
                rom_mod.read_flags(path)

    def test_a_short_file_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiny.3ds"
            path.write_bytes(b"\x00" * 16)
            with self.assertRaises(ValueError):
                rom_mod.read_flags(path)


class TheCorrectionIsMinimalAndIdempotent(unittest.TestCase):
    def test_exactly_one_byte_changes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "src.3ds"
            target = Path(directory) / "dst.3ds"
            payload = synth_card(0x00)
            source.write_bytes(payload)
            shutil.copyfile(source, target)
            before = target.read_bytes()
            was, now = rom_mod.set_no_crypto(target)
            after = target.read_bytes()
            self.assertEqual(was, 0x00)
            self.assertEqual(now, 0x04)
            differing = [i for i in range(len(before)) if before[i] != after[i]]
            self.assertEqual(differing, [rom_mod.NCCH_FLAGS_OFFSET], "more than one byte changed")

    def test_the_source_is_never_modified(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "src.3ds"
            target = Path(directory) / "dst.3ds"
            source.write_bytes(synth_card(0x00))
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            shutil.copyfile(source, target)
            rom_mod.set_no_crypto(target)
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), digest)

    def test_running_twice_changes_nothing_the_second_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dst.3ds"
            path.write_bytes(synth_card(0x00))
            rom_mod.set_no_crypto(path)
            first = path.read_bytes()
            was, now = rom_mod.set_no_crypto(path)
            self.assertEqual(was, 0x04)
            self.assertEqual(now, 0x04)
            self.assertEqual(path.read_bytes(), first)

    def test_other_flag_bits_are_preserved(self) -> None:
        """The bit is OR-ed in, not assigned: other flags in the same byte are not the tool's to clear."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dst.3ds"
            path.write_bytes(synth_card(0x38))  # no_crypto clear, three other bits set
            _was, now = rom_mod.set_no_crypto(path)
            self.assertEqual(now, 0x38 | 0x04)
            self.assertEqual(now & ~0x04, 0x38)


class TheControlIsAMenuWithNothingToFlip(unittest.TestCase):
    def test_an_already_correct_image_reports_the_bit_set(self) -> None:
        """A "0x04 for every image" reader would pass every other test and break a good ROM."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "good.3ds"
            path.write_bytes(synth_card(0x04))
            self.assertEqual(rom_mod.read_flags(path) >> 2 & 1, 1)

    def test_a_real_oot3d_image_is_already_correct(self) -> None:
        """Skip unless the OoT3D ROM is present; the point is the on-disk header, not the constants."""
        rom = os.environ.get("ZELDA3D_OOT3D_ROM")
        if not rom or not Path(rom).is_file():
            self.skipTest("no OoT3D ROM in the environment")
        flags = rom_mod.read_flags(Path(rom))
        self.assertEqual(flags >> 2 & 1, 1, "OoT3D's dump is the known-good control: it must load")
        self.assertEqual(rom_mod.read_media_id(Path(rom)), rom_mod.OOT3D_MEDIA_ID)

    def test_a_real_mm3d_image_is_recognised_as_mm3d(self) -> None:
        """The provisioning policy keys on the media ID, so the real image must resolve to MM3D."""
        rom = os.environ.get("ZELDA3D_MM3D_ROM")
        if not rom or not Path(rom).is_file():
            self.skipTest("no MM3D ROM in the environment")
        self.assertEqual(rom_mod.read_media_id(Path(rom)), rom_mod.MM3D_MEDIA_ID)
        self.assertNotEqual(rom_mod.read_media_id(Path(rom)), rom_mod.OOT3D_MEDIA_ID)


class TheCommandLineRefusesRatherThanGuesses(unittest.TestCase):
    def test_check_writes_nothing(self) -> None:
        import contextlib
        import io

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "src.3ds"
            target = Path(directory) / "dst.3ds"
            source.write_bytes(synth_card(0x00))
            with contextlib.redirect_stdout(io.StringIO()):
                rc = rom_mod.main([str(source), str(target), "--check"])
            self.assertEqual(rc, 0)
            self.assertFalse(target.exists(), "--check created the destination")

    def test_a_non_ncch_image_exits_nonzero(self) -> None:
        import contextlib
        import io

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "src.3ds"
            target = Path(directory) / "dst.3ds"
            source.write_bytes(synth_card(0x00, magic=b"NAND"))
            with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                rc = rom_mod.main([str(source), str(target)])
            self.assertEqual(rc, 2)

    def test_a_missing_source_exits_nonzero(self) -> None:
        import contextlib
        import io

        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            rc = rom_mod.main(["/nonexistent.3ds", "/tmp/also-nonexistent.3ds"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main()
