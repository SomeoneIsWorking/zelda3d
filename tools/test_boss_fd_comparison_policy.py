"""Build and execute the pure BossFd comparison-policy falsifiers."""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
HARNESS = REPO / "tools" / "soh3d_harness"
OUTPUT = REPO / "scratch" / "bin" / "boss_fd_comparison_policy_test"


class BossFdComparisonPolicyTests(unittest.TestCase):
    def test_production_policy_positive_negative_tolerance_and_wrap(self) -> None:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        compiler = os.environ.get("CXX", "c++")
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                f"-I{HARNESS}",
                f"-I{REPO / 'Shipwright' / 'soh' / 'src'}",
                str(HARNESS / "boss_fd_profile_validation.cpp"),
                str(HARNESS / "boss_fd_comparison_policy.cpp"),
                str(HARNESS / "tests" / "boss_fd_comparison_policy_test.cpp"),
                "-o",
                str(OUTPUT),
            ],
            cwd=REPO,
            check=True,
        )
        subprocess.run([str(OUTPUT)], cwd=REPO, check=True)


if __name__ == "__main__":
    unittest.main()
