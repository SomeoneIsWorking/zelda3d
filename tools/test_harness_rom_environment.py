"""Tests for oracle-ROM identity and MM3D provisioning.

The policy being pinned is **identity by the image's own NCCH media ID, never by filename.** With both
3DS releases in play and each project's N64 counterpart sharing the drop-in directory, a filename glob
can hand one game's image to the other game's oracle — and every reading taken through it is then
confidently about the wrong game. That is the same failure shape this project keeps meeting: a
confident false negative rather than an error, so the cases that matter most are the ones where a
*plausible-looking* wrong ROM must be refused.

Also pinned: the MM3D oracle copy is made automatically, is byte-minimal, leaves the source untouched,
and is idempotent — because the alternative is a one-bit fix that lives only in somebody's notes.
"""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import ctr_oracle_rom as rom_mod  # noqa: E402
import harness_rom_environment as env_mod  # noqa: E402


def synth_ctr(media_id: bytes, flags: int, *, size: int = 0x40000) -> bytes:
    """A 3DS image with a given media ID and NCCH flags byte."""
    card = bytearray(size)
    card[rom_mod.NCCH_BASE + 0x100 : rom_mod.NCCH_BASE + 0x104] = b"NCCH"
    card[rom_mod.NCCH_MEDIA_ID_OFFSET : rom_mod.NCCH_MEDIA_ID_OFFSET + len(media_id)] = media_id
    card[rom_mod.NCCH_FLAGS_OFFSET] = flags
    return bytes(card)


OOT = rom_mod.OOT3D_MEDIA_ID
MM = rom_mod.MM3D_MEDIA_ID


class IdentityIsTheImageNotItsName(unittest.TestCase):
    def test_media_id_is_read_from_the_header(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            for media in (OOT, MM, b"CTR-P-ZZZZ"):
                path = Path(d) / f"{media.decode()}.3ds"
                path.write_bytes(synth_ctr(media, 0x04))
                self.assertEqual(rom_mod.read_media_id(path), media)

    def test_an_mm3d_image_renamed_to_oot3d_is_still_mm3d(self) -> None:
        """The failure this prevents: a plausible filename carrying the wrong game."""
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "oot3d.3ds"
            path.write_bytes(synth_ctr(MM, 0x04))
            self.assertEqual(rom_mod.read_media_id(path), MM)
            self.assertNotEqual(rom_mod.read_media_id(path), OOT)

    def test_a_non_ctr_drop_in_is_skipped_not_treated_as_an_error(self) -> None:
        """The drop-in directory also holds .z64 and other files; they are not 3DS images."""
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "oot.z64").write_bytes(b"\x80\x37\x12\x40" + b"\x00" * 64)
            (Path(d) / "notes.3ds").write_bytes(b"not a 3ds image at all")
            self.assertEqual(env_mod._ctr_images(Path(d)), {})

    def test_describe_reports_both_games_distinctly(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            oot = Path(d) / "a.3ds"
            oot.write_bytes(synth_ctr(OOT, 0x04))
            mm = Path(d) / "b.3ds"
            mm.write_bytes(synth_ctr(MM, 0x00))
            self.assertIn("CTR-P-AQEE", rom_mod.describe(oot))
            self.assertIn("no_crypto=1", rom_mod.describe(oot))
            self.assertIn("CTR-P-AJRE", rom_mod.describe(mm))
            self.assertIn("no_crypto=0", rom_mod.describe(mm))


class OoT3dProvisioningRefusesTheWrongGame(unittest.TestCase):
    def test_an_mm3d_only_drop_in_directory_does_not_become_the_oot3d_oracle(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "mm3d.3ds").write_bytes(synth_ctr(MM, 0x04))
            with self.assertRaises(RuntimeError) as caught:
                env_mod.provision_rom_environment(repo, {})
            self.assertIn("wrong game", str(caught.exception))

    def test_the_matching_drop_in_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "oot3d.3ds").write_bytes(synth_ctr(OOT, 0x04))
            (repo / "mm3d.3ds").write_bytes(synth_ctr(MM, 0x00))
            environment: dict[str, str] = {}
            env_mod.provision_rom_environment(repo, environment)
            self.assertEqual(
                Path(environment["ZELDA3D_OOT3D_ROM"]).read_bytes(), synth_ctr(OOT, 0x04)
            )

    def test_an_explicit_environment_rom_is_never_second_guessed(self) -> None:
        """An explicit path is the caller's decision; the policy only governs discovery."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "oot3d.3ds").write_bytes(synth_ctr(OOT, 0x04))
            explicit = repo / "chosen.3ds"
            explicit.write_bytes(synth_ctr(OOT, 0x04))
            environment = {"ZELDA3D_OOT3D_ROM": str(explicit)}
            env_mod.provision_rom_environment(repo, environment)
            self.assertEqual(environment["ZELDA3D_OOT3D_ROM"], str(explicit))

    def test_the_canonical_name_cannot_launder_an_mm3d_image(self) -> None:
        """Otherwise the media-ID check is decorative: the filename alone would still decide."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "oot3d.3ds").write_bytes(synth_ctr(MM, 0x04))
            with self.assertRaises(RuntimeError) as caught:
                env_mod.provision_rom_environment(repo, {})
            self.assertIn("wrong game", str(caught.exception))

    def test_a_non_ctr_file_at_the_canonical_name_is_still_accepted(self) -> None:
        """Not contradicted, so not refused: test fixtures and renamed N64 dumps must keep working."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "oot3d.3ds").write_bytes(b"fixture")
            environment: dict[str, str] = {}
            env_mod.provision_rom_environment(repo, environment)
            self.assertTrue(environment["ZELDA3D_OOT3D_ROM"].endswith("oot3d.3ds"))


class TheMm3dOracleCopyIsMadeAutomaticAndMinimal(unittest.TestCase):
    def test_a_clear_no_crypto_bit_produces_a_working_copy(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            source = repo / "mm3d.3ds"
            payload = synth_ctr(MM, 0x00)
            source.write_bytes(payload)
            environment: dict[str, str] = {}
            result = env_mod.provision_mm3d_oracle_rom(repo, environment)
            self.assertIsNotNone(result)
            assert result is not None
            # The copy differs from the source in exactly the one header byte.
            before, after = payload, result.read_bytes()
            differing = [i for i in range(len(before)) if before[i] != after[i]]
            self.assertEqual(differing, [rom_mod.NCCH_FLAGS_OFFSET])
            self.assertEqual(after[rom_mod.NCCH_FLAGS_OFFSET] >> 2 & 1, 1)
            # The source is untouched, and the emulator can tell the two games apart still.
            self.assertEqual(source.read_bytes(), payload)
            self.assertEqual(rom_mod.read_media_id(result), MM)

    def test_an_already_correct_image_is_used_in_place(self) -> None:
        """Nothing to correct, so nothing is copied — and no 1 GiB duplicate appears."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            source = repo / "mm3d.3ds"
            source.write_bytes(synth_ctr(MM, 0x04))
            environment: dict[str, str] = {}
            self.assertEqual(env_mod.provision_mm3d_oracle_rom(repo, environment), source)
            self.assertNotIn("ZELDA3D_MM3D_ORACLE_ROM", environment)
            self.assertFalse((repo / "scratch" / "oracle-roms").exists())

    def test_provisioning_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "mm3d.3ds").write_bytes(synth_ctr(MM, 0x00))
            first = env_mod.provision_mm3d_oracle_rom(repo, {})
            assert first is not None
            digest = hashlib.sha256(first.read_bytes()).hexdigest()
            again = env_mod.provision_mm3d_oracle_rom(repo, {})
            assert again is not None
            self.assertEqual(again, first)
            self.assertEqual(hashlib.sha256(again.read_bytes()).hexdigest(), digest)

    def test_a_stale_truncated_copy_is_replaced(self) -> None:
        """A half-written 1 GiB copy from an interrupted run must not be trusted as corrected."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "mm3d.3ds").write_bytes(synth_ctr(MM, 0x00))
            first = env_mod.provision_mm3d_oracle_rom(repo, {})
            assert first is not None
            first.write_bytes(b"truncated")
            again = env_mod.provision_mm3d_oracle_rom(repo, {})
            assert again is not None
            self.assertEqual(rom_mod.read_flags(again) >> 2 & 1, 1)

    def test_no_mm3d_present_is_not_an_error(self) -> None:
        """MM3D is optional; the harness must still provision for OoT3D alone."""
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            (repo / "oot3d.3ds").write_bytes(synth_ctr(OOT, 0x04))
            environment: dict[str, str] = {}
            env_mod.provision_rom_environment(repo, environment)
            self.assertIn("ZELDA3D_OOT3D_ROM", environment)
            self.assertNotIn("ZELDA3D_MM3D_ROM", environment)


if __name__ == "__main__":
    unittest.main()
