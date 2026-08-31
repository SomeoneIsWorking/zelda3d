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

    def test_render_contract_change_rotates_cache_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            render_contract = root / "AZAHAR_RENDER_CONTRACT"
            savestate.write_bytes(b"state")
            render_contract.write_text("render-one\n")
            environment = {"ZELDA3D_HARNESS_TEXPACK": "off"}
            with (
                mock.patch.object(harness_cache, "AZAHAR_RENDER_CONTRACT", render_contract),
                mock.patch.object(harness_cache, "REPO_ROOT", root),
            ):
                first, _ = harness_cache.cache_key(savestate, environment=environment)
                render_contract.write_text("render-two\n")
                second, _ = harness_cache.cache_key(savestate, environment=environment)
            self.assertNotEqual(first, second)

    def test_patch_document_change_does_not_rotate_render_contract_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            patch_doc = root / "AZAHAR_PATCH.md"
            render_contract = root / "AZAHAR_RENDER_CONTRACT"
            savestate.write_bytes(b"state")
            patch_doc.write_text("# Patch\nfirst note\n")
            render_contract.write_text("render-one\n")
            environment = {"ZELDA3D_HARNESS_TEXPACK": "off"}
            with (
                mock.patch.object(harness_cache, "AZAHAR_RENDER_CONTRACT", render_contract),
                mock.patch.object(harness_cache, "REPO_ROOT", root),
            ):
                first, _ = harness_cache.cache_key(savestate, environment=environment)
                patch_doc.write_text("# Patch\nsecond note\n")
                second, _ = harness_cache.cache_key(savestate, environment=environment)
            self.assertEqual(first, second)

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
            with (
                mock.patch.object(harness_cache, "CACHE_ROOT", root / "cache"),
                self.assertRaises(FileNotFoundError),
            ):
                self.cache(root, savestate).put_artifact("missing", {}, root / "does-not-exist.log")

    def test_open_existing_context_preserves_historical_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            source = root / "checkpoint.state"
            savestate.write_bytes(b"bootstrap state")
            source.write_bytes(b"controlled checkpoint")
            with mock.patch.object(harness_cache, "CACHE_ROOT", root / "cache"):
                original = self.cache(root, savestate)
                args = {"probe": "bossfd2", "frame": 29}
                stored = original.put_artifact("control-checkpoint", args, source)
                opened = harness_cache.OracleCache.open_existing_context(original.key)

            self.assertEqual(opened.key, original.key)
            self.assertEqual(opened.meta["key"], original.key)
            self.assertEqual(opened.get_artifact("control-checkpoint", args), stored)
            self.assertEqual(stored.read_bytes(), source.read_bytes())

    def test_frame_adoption_checks_inputs_and_records_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache_root = root / "cache"
            savestate = root / "state.bin"
            source_image = root / "source.png"
            savestate.write_bytes(b"state")

            from PIL import Image

            Image.new("RGB", (2, 2), (1, 2, 3)).save(source_image)
            with mock.patch.object(harness_cache, "CACHE_ROOT", cache_root):
                source = self.cache(root, savestate)
                source.put_frame(17, source_image)
                source_key = source.key

                with mock.patch.object(
                    harness_cache,
                    "_patch_marker",
                    return_value="observer-only-patch",
                ):
                    target = self.cache(root, savestate)
                    adopted = target.adopt_frame(cache_root / source_key, 17)

                self.assertEqual(Image.open(adopted).getpixel((0, 0)), (1, 2, 3))
                entry = target._load_index()["frames"]["17"]
                self.assertEqual(entry["adopted_from_key"], source_key)

    def test_frame_adoption_rejects_different_savestate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache_root = root / "cache"
            first_state = root / "first.state"
            second_state = root / "second.state"
            source_image = root / "source.png"
            first_state.write_bytes(b"first")
            second_state.write_bytes(b"second")

            from PIL import Image

            Image.new("RGB", (1, 1), (0, 0, 0)).save(source_image)
            with mock.patch.object(harness_cache, "CACHE_ROOT", cache_root):
                source = self.cache(root, first_state)
                source.put_frame(3, source_image)
                target = self.cache(root, second_state)
                with self.assertRaisesRegex(ValueError, "different frame inputs"):
                    target.adopt_frame(cache_root / source.key, 3)


if __name__ == "__main__":
    unittest.main()
