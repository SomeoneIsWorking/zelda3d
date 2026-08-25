#!/usr/bin/env python3

import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PIPELINE_OWNER = REPO / "Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_pipelines.cpp"
SHADERS = REPO / "Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_shaders.cpp"
SHADER_CONTRACT = REPO / "Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_shaders.h"

SHADER_OWNER_SYMBOLS = (
    "g_glslOnce",
    "CompileGlsl",
    "kVaryings",
    "kVertTemplate",
    "kFragTemplate",
    "kOverlayDepthFrag",
    "SG_UBO_COMMON_BODY",
    "SG_UBO_BONES_BODY",
)


class Sdl3GpuShaderStructureTest(unittest.TestCase):
    def test_shader_source_has_one_focused_owner(self) -> None:
        renderer = PIPELINE_OWNER.read_text()
        shaders = SHADERS.read_text()

        for symbol in SHADER_OWNER_SYMBOLS:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, shaders)
                self.assertNotIn(symbol, renderer)

    def test_backend_consumes_only_the_shader_contract(self) -> None:
        renderer = PIPELINE_OWNER.read_text()

        self.assertIn('#include "zelda3d_sdl3gpu_shaders.h"', renderer)
        self.assertIn("Zelda3DSdl3GpuShaders::Compile", renderer)
        self.assertIn("Zelda3DSdl3GpuShaders::BuildSources", renderer)
        self.assertIn("Zelda3DSdl3GpuShaders::OverlayDepthFragment", renderer)
        self.assertNotIn("inja::", renderer)

    def test_shader_module_respects_source_file_limit(self) -> None:
        for path in (SHADERS, SHADER_CONTRACT):
            with self.subTest(path=path.name):
                self.assertLessEqual(len(path.read_text().splitlines()), 1200)


if __name__ == "__main__":
    unittest.main()
