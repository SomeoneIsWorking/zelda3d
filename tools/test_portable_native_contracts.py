"""Compile and execute asset-free Zelda3D native policy seams with Clang."""

from __future__ import annotations

import os
import subprocess
import unittest
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build" / "ci-native"


@dataclass(frozen=True)
class NativeContract:
    name: str
    sources: tuple[Path, ...]
    include_directories: tuple[Path, ...]


CONTRACTS = (
    NativeContract(
        name="boss_fd2_animation_policy",
        sources=(
            REPO / "tools" / "boss_fd2_animation_policy_test.cpp",
            REPO
            / "Shipwright"
            / "soh"
            / "src"
            / "zelda3d"
            / "behaviors"
            / "actor"
            / "boss_fd2_animation_policy.cpp",
        ),
        include_directories=(
            REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "behaviors" / "actor",
        ),
    ),
    NativeContract(
        name="boss_fd2_material_controller",
        sources=(REPO / "tools" / "boss_fd2_material_controller_test.cpp",),
        include_directories=(
            REPO / "Shipwright" / "soh" / "src" / "zelda3d" / "behaviors" / "actor",
        ),
    ),
    NativeContract(
        name="mm3d_player_animation_policy",
        sources=(
            REPO / "tools" / "mm3d_player_animation_policy_test.cpp",
            REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_player_animation_policy.cpp",
        ),
        include_directories=(REPO / "2ship",),
    ),
)


def compile_and_run(contract: NativeContract) -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    executable = BUILD / (contract.name + (".exe" if os.name == "nt" else ""))
    command = [
        os.environ.get("CXX", "clang++"),
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
    ]
    for include_directory in contract.include_directories:
        command.extend(("-I", str(include_directory)))
    command.extend(str(source) for source in contract.sources)
    command.extend(("-o", str(executable)))
    subprocess.run(command, cwd=REPO, check=True)
    subprocess.run([str(executable)], cwd=REPO, check=True)


class PortableNativeContractTests(unittest.TestCase):
    def test_asset_free_native_contracts(self) -> None:
        for contract in CONTRACTS:
            with self.subTest(contract=contract.name):
                compile_and_run(contract)


if __name__ == "__main__":
    unittest.main()
