#!/usr/bin/env python3
"""Responsibility and ownership gate for the Fast3D interpreter."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FAST = REPO / "Shipwright/libultraship/src/fast"

OWNERS = {
    "interpreter.cpp": ("Interpreter::Init", "Interpreter::Run", "Interpreter::Destroy"),
    "interpreter_combiner.cpp": (
        "Interpreter::GenerateCC",
        "Interpreter::LookupOrCreateColorCombiner",
        "gfx_cc_get_features",
        "gfx_shader_cache_clear",
    ),
    "interpreter_texture.cpp": (
        "Interpreter::TextureCacheLookup",
        "Interpreter::ImportTexture",
        "Interpreter::ImportTextureMask",
        "gfx_texture_cache_clear",
    ),
    "interpreter_rsp.cpp": (
        "Interpreter::GfxSpMatrix",
        "Interpreter::GfxSpVertex",
        "Interpreter::GfxSpTri1",
    ),
    "interpreter_rdp.cpp": (
        "Interpreter::GfxDpSetTile",
        "Interpreter::GfxDpTextureRectangle",
        "Interpreter::GfxDpFillRectangle",
    ),
    "interpreter_display_list_commands.cpp": (
        "gfx_mtx_handler_f3dex2",
        "gfx_dl_handler_common",
        "gfx_tri1_handler_f3dex2",
    ),
    "interpreter_rdp_commands.cpp": (
        "gfx_set_combine_handler_rdp",
        "gfx_tex_rect_and_flip_handler_rdp",
    ),
    "interpreter_texture_commands.cpp": (
        "gfx_set_timg_handler_rdp",
        "gfx_load_block_handler_rdp",
        "gfx_register_blended_texture_handler_custom",
    ),
    "interpreter_framebuffer_commands.cpp": (
        "gfx_set_fb_handler_custom",
        "gfx_read_fb_handler_custom",
    ),
    "interpreter_zelda3d_commands.cpp": (
        "gfx_zelda3d_draw_handler_custom",
        "gfx_zelda3d_measure_handler_custom",
        "gfx_zelda3d_hudflush_handler_custom",
    ),
    "interpreter_s2dex_commands.cpp": (
        "gfx_bg_copy_handler_s2dex",
        "gfx_obj_rectangle_handler_s2dex",
    ),
    "interpreter_dispatch.cpp": ("GfxGetOpcodeName", "GfxStep"),
    "interpreter_geometry_observation.cpp": (
        "GeometryObservationOnSourceVertex",
        "GeometryObservationOnTriangle",
        "GeometryObservationBeginFrame",
    ),
    "interpreter_runtime_state.cpp": (
        "GetInterpreterInstance",
        "GetUcodeAttribute",
        "PushCurrentDirectory",
        "gfx_set_target_ucode",
    ),
    "interpreter_execution_stack.cpp": (
        "Interpreter::SegAddr",
        "GfxExecStack::start",
        "gfx_push_current_dir",
    ),
    "interpreter_framebuffer.cpp": (
        "Interpreter::CreateFrameBuffer",
        "Interpreter::CopyFrameBuffer",
        "Interpreter::GetPixelDepth",
    ),
    "interpreter_blended_textures.cpp": (
        "gfx_check_image_signature",
        "Interpreter::RegisterBlendedTexture",
        "Interpreter::ClearBlendedTextures",
    ),
}


class InterpreterStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sources = {name: (FAST / name).read_text() for name in OWNERS}

    def test_representative_operations_have_one_owner(self) -> None:
        for expected_name, symbols in OWNERS.items():
            for symbol in symbols:
                with self.subTest(symbol=symbol):
                    definition = re.compile(
                        rf"^(?:extern \"C\"\s+)?[\w:<>&* ]+\b{re.escape(symbol)}\([^;{{]*\)\s*(?:const\s*)?\{{",
                        re.MULTILINE,
                    )
                    owners = [name for name, source in self.sources.items() if definition.search(source)]
                    self.assertEqual(owners, [expected_name])

    def test_every_interpreter_source_is_bounded(self) -> None:
        for path in sorted(FAST.glob("interpreter*.cpp")):
            with self.subTest(path=path.name):
                self.assertLessEqual(len(path.read_text().splitlines()), 1200, path)

    def test_command_handlers_are_declared_and_defined_once(self) -> None:
        contract = (FAST / "interpreter_command_handlers.h").read_text()
        declared = set(re.findall(r"^bool (gfx_[A-Za-z0-9_]+)\(F3DGfx\*\* command\);$", contract, re.MULTILINE))
        definitions: dict[str, list[str]] = {}
        for path in sorted(FAST.glob("interpreter_*commands.cpp")):
            name = path.name
            for symbol in re.findall(r"^bool (gfx_[A-Za-z0-9_]+)\(F3DGfx\*\* [A-Za-z0-9_]+\)", self.sources[name], re.MULTILINE):
                definitions.setdefault(symbol, []).append(name)
        self.assertEqual(declared, set(definitions))
        self.assertTrue(declared)
        self.assertEqual({symbol: owners for symbol, owners in definitions.items() if len(owners) != 1}, {})

    def test_lifecycle_composer_does_not_reabsorb_execution_owners(self) -> None:
        composer = self.sources["interpreter.cpp"]
        for forbidden in (
            "Interpreter::GenerateCC(",
            "Interpreter::ImportTexture(",
            "Interpreter::GfxSpVertex(",
            "Interpreter::GfxDpSetTile(",
            "class UcodeHandler",
            "bool gfx_",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, composer)

    def test_geometry_observer_does_not_own_rendering(self) -> None:
        observer = self.sources["interpreter_geometry_observation.cpp"]
        self.assertNotIn("mRapi", observer)
        self.assertNotIn("Interpreter::Gfx", observer)


if __name__ == "__main__":
    unittest.main()
