"""Compile and execute the MM3D Player sheath policy with Clang."""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch" / "mm_graphics_port" / "bin"


class Mm3dPlayerSheathPolicyTests(unittest.TestCase):
    def test_retail_sheath_and_back_shield_mesh_masks(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_sheath_policy_test"
        subprocess.run(
            [
                os.environ.get("CXX", "clang++"),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO / "2ship"),
                str(REPO / "tools" / "mm3d_player_sheath_policy_test.cpp"),
                str(
                    REPO
                    / "2ship"
                    / "2s2h"
                    / "zelda3d"
                    / "mm3d_player_sheath_policy.cpp"
                ),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
