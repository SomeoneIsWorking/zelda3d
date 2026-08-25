#!/usr/bin/env python3

import unittest
from pathlib import Path

from clang_verifier.source_structure import LEGACY_LINE_LIMITS


REPO = Path(__file__).resolve().parents[1]
FAST = REPO / "Shipwright/libultraship/src/fast"
ENTRY = FAST / "zelda3d_sdl3gpu.cpp"
INTERNAL = FAST / "zelda3d_sdl3gpu_internal.h"
LIFECYCLE = FAST / "zelda3d_sdl3gpu_lifecycle.cpp"
PASS = FAST / "zelda3d_sdl3gpu_pass.cpp"
PIPELINES = FAST / "zelda3d_sdl3gpu_pipelines.cpp"
RESOURCES = FAST / "zelda3d_sdl3gpu_resources.cpp"
SOURCES = (ENTRY, INTERNAL, LIFECYCLE, PASS, PIPELINES, RESOURCES)

METHOD_OWNERS = {
    "releaseGpuResources": LIFECYCLE,
    "getSampler": RESOURCES,
    "uploadTexture": RESOURCES,
    "ensureUploaded": RESOURCES,
    "ensureUnifiedUploaded": RESOURCES,
    "applyPendingEvict": RESOURCES,
    "ensureResources": PIPELINES,
    "getPipeline": PIPELINES,
    "getUnifiedPipeline": PIPELINES,
    "makeShader": PIPELINES,
    "ensureOverlayDepthResources": PIPELINES,
    "groupBounds": PASS,
    "RequestEvictRange": PASS,
    "ClearOverlayDepth": PASS,
    "BeginPass": PASS,
    "DrawModel": PASS,
    "EndPass": PASS,
    "GeomScanDump": PASS,
}


class Sdl3GpuRendererStructureTest(unittest.TestCase):
    def test_each_method_has_one_responsibility_owner(self) -> None:
        contents = {path: path.read_text() for path in SOURCES if path.suffix == ".cpp"}
        for method, expected in METHOD_OWNERS.items():
            with self.subTest(method=method):
                owners = [path for path, source in contents.items() if f"::{method}(" in source]
                self.assertEqual(owners, [expected])

    def test_entrypoint_is_only_the_stable_c_abi_adapter(self) -> None:
        entry = ENTRY.read_text()
        self.assertIn('extern "C" void Zelda3D_Sg_DrawModel', entry)
        self.assertNotIn("Zelda3DRenderer::", entry)

    def test_focused_renderer_owners_respect_source_limit(self) -> None:
        for path in SOURCES:
            with self.subTest(path=path.name):
                self.assertLessEqual(len(path.read_text().splitlines()), 1200)

    def test_renderer_entrypoint_no_longer_needs_legacy_ceiling(self) -> None:
        relative = "Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp"
        self.assertNotIn(relative, LEGACY_LINE_LIMITS)


if __name__ == "__main__":
    unittest.main()
