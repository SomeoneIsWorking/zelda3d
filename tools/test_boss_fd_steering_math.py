#!/usr/bin/env python3
"""Compile and execute the BossFd OoT3D steering-math regression test."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_SOURCE = REPO_ROOT / "tools/soh3d_harness/tests/boss_fd_steering_math_test.cpp"
PRODUCTION_SOURCE = (
    REPO_ROOT / "Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/steering_math.cpp"
)
OUTPUT = REPO_ROOT / "scratch/bin/boss_fd_steering_math_test"


class BossFdSteeringMathTest(unittest.TestCase):
    def test_exact_oot3d_table_interpolation_and_angle_wrap(self) -> None:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        compiler = os.environ.get("CXX", "clang++")
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{REPO_ROOT}",
                str(TEST_SOURCE),
                str(PRODUCTION_SOURCE),
                "-o",
                str(OUTPUT),
            ],
            cwd=REPO_ROOT,
            check=True,
        )
        subprocess.run([str(OUTPUT)], cwd=REPO_ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
