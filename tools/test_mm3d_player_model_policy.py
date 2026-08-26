#!/usr/bin/env python3
"""Compile and execute the production MM3D Player form-model policy with Clang."""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / "scratch" / "mm_graphics_port" / "bin"


class Mm3dPlayerModelPolicyTests(unittest.TestCase):
    def test_retail_form_model_table(self) -> None:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        binary = SCRATCH / "mm3d_player_model_policy_test"
        compiler = os.environ.get("CXX", "clang++")
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(REPO / "2ship"),
                str(REPO / "tools" / "mm3d_player_model_policy_test.cpp"),
                str(REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_model_policy.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
