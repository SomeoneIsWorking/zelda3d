#!/usr/bin/env python3
"""Compile and run the MM3D Player left-hand policy and adapter with Clang."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch" / "mm_graphics_port" / "bin"


class Mm3dPlayerLeftHandTests(unittest.TestCase):
    def test_retail_bottle_material_policy(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_bottle_material_policy_test"
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO / "2ship"),
                str(REPO / "tools" / "mm3d_player_bottle_material_policy_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_bottle_material_policy.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_left_hand_policy.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)

    def test_retail_left_hand_policy(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_left_hand_policy_test"
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO / "2ship"),
                str(REPO / "tools" / "mm3d_player_left_hand_policy_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_left_hand_policy.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)

    def test_typed_player_adapter(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_left_hand_adapter_test"
        include_dirs = [
            REPO / "2ship",
            REPO / "2ship" / "include",
            REPO / "2ship" / "assets",
            REPO / "2ship" / "2s2h",
            REPO / "2ship" / "src",
            REPO / "Shipwright" / "libultraship" / "include",
        ]
        command = [
            os.environ.get("CXX", "clang++"),
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-macro-redefined",
            "-DNON_MATCHING",
            "-DNON_EQUIVALENT",
        ]
        for include_dir in include_dirs:
            command.extend(["-I", str(include_dir)])
        command.extend(
            [
                str(REPO / "tools" / "mm3d_player_left_hand_adapter_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_left_hand.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_bottle_material_policy.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_left_hand_policy.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_sheath_policy.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_model_policy.cpp"),
                "-o",
                str(binary),
            ]
        )
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(
        os.environ.get("ZELDA3D_MM3D_ROM"),
        "ZELDA3D_MM3D_ROM is required for retail archive checks",
    )
    def test_retail_animation_ids_and_mesh_inventory(self) -> None:
        sys.path.insert(0, str(REPO / "tools"))
        from mm_animmap_archive import Gar, Mm3dActors

        actors = Mm3dActors()
        archive_file = actors.rom.get("/actors/zelda2_link_new.gar.lzs")
        archive = Gar(actors.rom.read(archive_file))
        actors.rom.fp.close()
        expected_clips = {
            0x5A: "boy/anim/link_bottle_bug_in.csab",
            0x5C: "boy/anim/link_bottle_bug_out.csab",
            0x60: "boy/anim/link_bottle_fish_in.csab",
            0x62: "boy/anim/link_bottle_fish_out.csab",
            0x107: "boy/anim/link_normal_free2freeB.csab",
            0x26B: "nuts/anim/pn_drink.csab",
            0x26C: "nuts/anim/pn_drinkend.csab",
            0x26D: "nuts/anim/pn_drinkstart.csab",
            0x2C0: "zora/anim/pz_gakkiplay.csab",
            0x2C1: "zora/anim/pz_gakkistart.csab",
            0x2C2: "zora/anim/pz_gakkiwait.csab",
            0x2D3: "zora/anim/pz_gakki_demo.csab",
        }
        for animation_id, member_path in expected_clips.items():
            self.assertEqual(archive.entries[animation_id].path, member_path)

        inventory_checks = (
            ("/actors/zelda2_link_boy_new.gar.lzs", "boy/model/link_demon.cmb", (1, 2, 6, 7, 8)),
            ("/actors/zelda2_link_goron_new.gar.lzs", "goron/model/link_goron.cmb", (1, 2, 8, 9)),
            ("/actors/zelda2_link_zora_new.gar.lzs", "zora/model/link_zora.cmb", (1, 2, 8, 9, 10)),
            ("/actors/zelda2_link_nuts_new.gar.lzs", "nuts/model/link_deknuts.cmb", (1, 6, 7)),
            (
                "/actors/zelda2_link_child_new.gar.lzs",
                "child/model/link_child.cmb",
                (0, 12, 14, 16, 18, 20, 21, 24, 27),
            ),
        )
        for archive_path, member_path, mesh_ids in inventory_checks:
            subprocess.run(
                [
                    sys.executable,
                    str(REPO / "tools" / "mm_player_cmb_dump.py"),
                    archive_path,
                    member_path,
                    "--require-mids",
                    *map(str, mesh_ids),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

        material_checks = (
            ("/actors/zelda2_link_boy_new.gar.lzs", "boy/model/link_demon.cmb", 6, 6),
            ("/actors/zelda2_link_goron_new.gar.lzs", "goron/model/link_goron.cmb", 3, 8),
            ("/actors/zelda2_link_zora_new.gar.lzs", "zora/model/link_zora.cmb", 4, 8),
            ("/actors/zelda2_link_nuts_new.gar.lzs", "nuts/model/link_deknuts.cmb", 3, 6),
            ("/actors/zelda2_link_child_new.gar.lzs", "child/model/link_child.cmb", 5, 0),
        )
        actors = Mm3dActors()
        for archive_path, member_path, material_index, bottle_mesh_id in material_checks:
            archive = Gar(actors.rom.read(actors.rom.get(archive_path)))
            data = next(entry.data for entry in archive.entries if entry.path == member_path)
            mats_offset = struct.unpack_from("<I", data, 0x2C)[0]
            material_count = struct.unpack_from("<I", data, mats_offset + 8)[0]
            material_offset = mats_offset + 0x0C + material_index * 0x16C
            texture_index = struct.unpack_from("<h", data, material_offset + 0x10)[0]
            model = __import__("cmb").Cmb(data)
            self.assertEqual(model.textures[texture_index].name, "p_bin_00")
            self.assertEqual(
                {mesh.mesh_id for mesh in model.meshes if mesh.material_index == material_index},
                {bottle_mesh_id},
            )

            combiner_base = mats_offset + 0x0C + material_count * 0x16C
            stage_count = struct.unpack_from("<I", data, material_offset + 0x120)[0]
            uses_constant_zero = False
            for stage in range(stage_count):
                combiner_index = struct.unpack_from("<H", data, material_offset + 0x124 + stage * 2)[0]
                combiner = combiner_base + combiner_index * 0x28
                rgb_sources = struct.unpack_from("<3H", data, combiner + 0x0C)
                alpha_sources = struct.unpack_from("<3H", data, combiner + 0x18)
                constant_index = struct.unpack_from("<I", data, combiner + 0x24)[0]
                uses_constant_zero |= 0x8576 in rgb_sources + alpha_sources and constant_index == 0
            self.assertTrue(uses_constant_zero)
        actors.rom.fp.close()


if __name__ == "__main__":
    unittest.main()
