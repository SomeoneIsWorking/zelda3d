"""Build and execute the pure BossFd2 mane-root trajectory falsifiers."""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
HARNESS = REPO / "tools" / "soh3d_harness"
OUTPUT = REPO / "scratch" / "bin" / "boss_fd2_mane_root_control_test"


class BossFd2ManeRootControlTests(unittest.TestCase):
    def test_equal_motion_unobserved_motion_and_positive_control(self) -> None:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        compiler = os.environ.get("CXX", "c++")
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{HARNESS}",
                str(HARNESS / "tests" / "boss_fd2_mane_root_control_test.cpp"),
                "-o",
                str(OUTPUT),
            ],
            cwd=REPO,
            check=True,
        )
        subprocess.run([str(OUTPUT)], cwd=REPO, check=True)


if __name__ == "__main__":
    unittest.main()
