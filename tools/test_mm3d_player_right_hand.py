"""Compile and execute the MM3D Player right-hand policy and typed adapter with Clang."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch" / "mm_graphics_port" / "bin"


class Mm3dPlayerRightHandTests(unittest.TestCase):
    def test_retail_right_hand_policy(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_right_hand_policy_test"
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO / "2ship"),
                str(REPO / "tools" / "mm3d_player_right_hand_policy_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_right_hand_policy.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)

    def test_typed_player_adapter(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_right_hand_adapter_test"
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
                str(REPO / "tools" / "mm3d_player_right_hand_adapter_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_right_hand.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_right_hand_policy.cpp"),
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
    def test_retail_linkb_pairing_and_mesh_inventory(self) -> None:
        sys.path.insert(0, str(REPO / "tools"))
        from mm_animmap_archive import Gar, Mm3dActors

        actors = Mm3dActors()
        archive_file = actors.rom.get("/actors/zelda2_link_new.gar.lzs")
        archive = Gar(actors.rom.read(archive_file))
        actors.rom.fp.close()
        self.assertEqual(len(archive.entries), 1694)
        self.assertTrue(all(entry.type == "csab" for entry in archive.entries[:847]))
        self.assertTrue(all(entry.type == "linkb" for entry in archive.entries[847:]))
        for animation_id, stem in ((0x26B, "pn_drink"), (0x26C, "pn_drinkend")):
            csab = archive.entries[animation_id]
            linkb = archive.entries[animation_id + 847]
            self.assertEqual(csab.path, f"nuts/anim/{stem}.csab")
            self.assertEqual(linkb.path, f"nuts/anim/{stem}.linkb")
            self.assertEqual(csab.data[:4], b"csab")
            self.assertEqual(linkb.data[:4], b"lkb\x01")

        inventory_checks = (
            ("/actors/zelda2_link_boy_new.gar.lzs", "boy/model/link_demon.cmb", (4, 5)),
            ("/actors/zelda2_link_goron_new.gar.lzs", "goron/model/link_goron.cmb", (4, 5)),
            ("/actors/zelda2_link_zora_new.gar.lzs", "zora/model/link_zora.cmb", (4, 5)),
            ("/actors/zelda2_link_nuts_new.gar.lzs", "nuts/model/link_deknuts.cmb", (3, 4)),
            (
                "/actors/zelda2_link_child_new.gar.lzs",
                "child/model/link_child.cmb",
                (2, 9, 10, 11, 22, 23, 25),
            ),
        )
        for archive_path, member_path, mesh_ids in inventory_checks:
            result = subprocess.run(
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
            self.assertIn(f"required_mesh_ids=PASS count={len(mesh_ids)}", result.stdout)


if __name__ == "__main__":
    unittest.main()
