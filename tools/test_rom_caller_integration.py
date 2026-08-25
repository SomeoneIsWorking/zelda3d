#!/usr/bin/env python3
"""ROM-policy integration tests for non-launcher runtime callers."""

from __future__ import annotations

import unittest
from unittest import mock

import gpu_validation_test
import render_smoke_test


class RomCallerIntegrationTests(unittest.TestCase):
    def _verify_smoke_caller(self, module: object) -> None:
        resolved = {
            "UNRELATED": "preserved internally",
            "ZELDA3D_OOT3D_ROM": "/roms/oot3d.3ds",
            "ZELDA3D_OOT_ROM": "/roms/oot.z64",
        }
        with (
            mock.patch.object(
                module, "resolve_rom_environment", return_value=resolved.copy()
            ) as resolve,
            mock.patch.object(
                module, "provision_n64_extraction_rom"
            ) as provision,
        ):
            roms = module.provision_roms()
        resolve.assert_called_once_with(module.REPO, module.os.environ)
        provision.assert_called_once_with(module.SOH_DIR, mock.ANY)
        self.assertEqual(
            roms,
            {
                "ZELDA3D_OOT3D_ROM": "/roms/oot3d.3ds",
                "ZELDA3D_OOT_ROM": "/roms/oot.z64",
            },
        )

    def test_render_smoke_uses_python_rom_owner(self) -> None:
        self._verify_smoke_caller(render_smoke_test)

    def test_gpu_validation_uses_python_rom_owner(self) -> None:
        self._verify_smoke_caller(gpu_validation_test)


if __name__ == "__main__":
    unittest.main()
