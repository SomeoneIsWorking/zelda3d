"""Tests for deterministic oracle cache identity and artifact reuse."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import harness_cache


class OracleCacheArtifactTests(unittest.TestCase):
    def cache(self, root: Path, savestate: Path) -> harness_cache.OracleCache:
        environment = {
            "ZELDA3D_HARNESS_TEXPACK": "off",
        }
        with mock.patch.object(harness_cache, "REPO_ROOT", root):
            return harness_cache.OracleCache(savestate, environment=environment)

    def test_patch_body_change_rotates_cache_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            patch_doc = root / "AZAHAR_PATCH.md"
            savestate.write_bytes(b"state")
            patch_doc.write_text("# Patch\nbody one\n")
            environment = {"ZELDA3D_HARNESS_TEXPACK": "off"}
            with (
                mock.patch.object(harness_cache, "AZAHAR_PATCH_MD", patch_doc),
                mock.patch.object(harness_cache, "REPO_ROOT", root),
            ):
                first, _ = harness_cache.cache_key(savestate, environment=environment)
                patch_doc.write_text("# Patch\nbody two\n")
                second, _ = harness_cache.cache_key(savestate, environment=environment)
            self.assertNotEqual(first, second)

    def test_repo_environment_rom_and_pack_manifest_are_cache_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            rom = root / "game.3ds"
            pack = root / "textures"
            texture = pack / "tex1_abc.png"
            savestate.write_bytes(b"state")
            rom.write_bytes(b"rom one")
            pack.mkdir()
            texture.write_bytes(b"texture one")
            (root / ".env").write_text("ZELDA3D_OOT3D_ROM=game.3ds\n")
            with mock.patch.object(harness_cache, "REPO_ROOT", root):
                first, metadata = harness_cache.cache_key(savestate, environment={})
                texture.write_bytes(b"texture two is different")
                second, _ = harness_cache.cache_key(savestate, environment={})
                rom.write_bytes(b"rom two is different")
                third, _ = harness_cache.cache_key(savestate, environment={})

            self.assertNotIn("norom", first)
            self.assertEqual(metadata["rom_path"], str(rom))
            self.assertEqual(metadata["texture_pack"]["mode"], "on")
            self.assertNotEqual(first, second)
            self.assertNotEqual(second, third)

    def test_artifact_round_trip_preserves_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            source = root / "oracle.log"
            savestate.write_bytes(b"state")
            source.write_bytes(b"draw n=38\nPIXELXY draw=38\n")
            with mock.patch.object(harness_cache, "CACHE_ROOT", root / "cache"):
                cache = self.cache(root, savestate)
                args = {"entrance": 0x305, "camera": "700 100 0 45", "xy": "240,195"}
                stored = cache.put_artifact("bossfd2-oracle-log", args, source)

                self.assertEqual(cache.get_artifact("bossfd2-oracle-log", args), stored)
                self.assertEqual(stored.read_bytes(), source.read_bytes())
                self.assertEqual(cache.stats()["n_artifacts"], 1)
                self.assertIsNone(
                    cache.get_artifact(
                        "bossfd2-oracle-log", {**args, "xy": "390,230"}
                    )
                )

    def test_missing_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            savestate.write_bytes(b"state")
            with mock.patch.object(harness_cache, "CACHE_ROOT", root / "cache"):
                with self.assertRaises(FileNotFoundError):
                    self.cache(root, savestate).put_artifact(
                        "missing", {}, root / "does-not-exist.log"
                    )


if __name__ == "__main__":
    unittest.main()
