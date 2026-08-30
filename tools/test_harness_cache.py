"""Tests for deterministic oracle cache identity and artifact reuse."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import harness_cache


class OracleCacheArtifactTests(unittest.TestCase):
    def test_patch_body_change_rotates_cache_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            patch_doc = root / "AZAHAR_PATCH.md"
            savestate.write_bytes(b"state")
            patch_doc.write_text("# Patch\nbody one\n")
            with mock.patch.object(harness_cache, "AZAHAR_PATCH_MD", patch_doc):
                first, _ = harness_cache.cache_key(savestate)
                patch_doc.write_text("# Patch\nbody two\n")
                second, _ = harness_cache.cache_key(savestate)
            self.assertNotEqual(first, second)

    def test_artifact_round_trip_preserves_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            savestate = root / "state.bin"
            source = root / "oracle.log"
            savestate.write_bytes(b"state")
            source.write_bytes(b"draw n=38\nPIXELXY draw=38\n")
            with mock.patch.object(harness_cache, "CACHE_ROOT", root / "cache"):
                cache = harness_cache.OracleCache(savestate)
                args = {"entrance": 0x305, "camera": "700 100 0 45", "xy": "240,195"}
                stored = cache.put_artifact("bossfd2-oracle-log", args, source)

                self.assertEqual(cache.get_artifact("bossfd2-oracle-log", args), stored)
                self.assertEqual(stored.read_bytes(), source.read_bytes())
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
                    harness_cache.OracleCache(savestate).put_artifact(
                        "missing", {}, root / "does-not-exist.log"
                    )


if __name__ == "__main__":
    unittest.main()
