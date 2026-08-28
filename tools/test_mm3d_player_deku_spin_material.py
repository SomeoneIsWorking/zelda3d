#!/usr/bin/env python3
"""Focused policy, typed-adapter, integration, and retail-asset checks."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MM = REPO / "2ship" / "2s2h" / "zelda3d"
SCRATCH = REPO / "scratch" / "mm_graphics_port" / "bin"


def compile_and_run(name: str, sources: list[Path], include_runtime: bool = False) -> None:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    binary = SCRATCH / name
    command = [
        os.environ.get("CXX", "clang++"),
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-macro-redefined",
        "-I",
        str(REPO / "2ship"),
    ]
    if include_runtime:
        for include_dir in (
            REPO / "2ship" / "include",
            REPO / "2ship" / "assets",
            REPO / "2ship" / "2s2h",
            REPO / "2ship" / "src",
            REPO / "Shipwright" / "libultraship" / "include",
        ):
            command.extend(["-I", str(include_dir)])
        command.extend(["-DNON_MATCHING", "-DNON_EQUIVALENT"])
    command.extend(map(str, sources))
    command.extend(["-o", str(binary)])
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)


class Mm3dPlayerDekuSpinMaterialTests(unittest.TestCase):
    def test_policy(self) -> None:
        compile_and_run(
            "mm3d_player_deku_spin_material_policy_test",
            [
                REPO / "tools" / "mm3d_player_deku_spin_material_policy_test.cpp",
                MM / "mm3d_player_deku_spin_material_policy.cpp",
                MM / "mm3d_player_model_policy.cpp",
            ],
        )

    def test_typed_adapter(self) -> None:
        compile_and_run(
            "mm3d_player_deku_spin_material_adapter_test",
            [
                REPO / "tools" / "mm3d_player_deku_spin_material_adapter_test.cpp",
                MM / "mm3d_player_deku_spin_material.cpp",
                MM / "mm3d_player_deku_spin_material_policy.cpp",
                MM / "mm3d_player_model_policy.cpp",
            ],
            include_runtime=True,
        )

    def test_production_draw_uses_typed_adapter_before_submit(self) -> None:
        source = (MM / "mm3d_player.c").read_text()
        adapter = "Zelda3D_MM_PlayerDekuSpinMaterialOverride(player, &dekuSpinMaterial)"
        submit = "Zelda3D_GL_SetMatConstOverride(modelId, dekuSpinMaterial.materialIndex"
        self.assertIn(adapter, source)
        self.assertIn(submit, source)
        self.assertLess(source.index(adapter), source.index(submit))
        self.assertLess(source.index(submit), source.index("Zelda3D_GL_SetMidMask(modelId, meshMask)"))

    @unittest.skipUnless(
        os.environ.get("ZELDA3D_MM3D_ROM"),
        "ZELDA3D_MM3D_ROM is required for the retail Deku CMB check",
    )
    def test_retail_deku_material_consumers(self) -> None:
        sys.path.insert(0, str(REPO / "tools"))
        import cmb
        from mm_animmap_archive import Gar, Mm3dActors

        actors = Mm3dActors()
        archive = Gar(actors.rom.read(actors.rom.get("/actors/zelda2_link_nuts_new.gar.lzs")))
        data = next(entry.data for entry in archive.entries if entry.path == "nuts/model/link_deknuts.cmb")
        actors.rom.fp.close()

        model = cmb.Cmb(data)
        material_index = 6
        self.assertEqual(
            {mesh.mesh_id for mesh in model.meshes if mesh.material_index == material_index},
            {11, 12, 13},
        )
        texture_index = model.material_texture(material_index)
        self.assertEqual(model.textures[texture_index].name, "link_nuts_f00")

        mats_offset = struct.unpack_from("<I", data, 0x2C)[0]
        material_count = struct.unpack_from("<I", data, mats_offset + 8)[0]
        material = mats_offset + 0x0C + material_index * 0x16C
        self.assertEqual(tuple(data[material + 0xC4 : material + 0xC8]), (0, 0, 0, 0))

        combiner_base = mats_offset + 0x0C + material_count * 0x16C
        stage_count = struct.unpack_from("<I", data, material + 0x120)[0]
        uses_constant_four = False
        for stage in range(stage_count):
            combiner_index = struct.unpack_from("<H", data, material + 0x124 + stage * 2)[0]
            combiner = combiner_base + combiner_index * 0x28
            sources = struct.unpack_from("<3H", data, combiner + 0x0C) + struct.unpack_from(
                "<3H", data, combiner + 0x18
            )
            constant_index = struct.unpack_from("<I", data, combiner + 0x24)[0]
            uses_constant_four |= 0x8576 in sources and constant_index == 4
        self.assertTrue(uses_constant_four)


if __name__ == "__main__":
    unittest.main()
