"""Authoritative filesystem layout for the embedded OoT3D harness tools."""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HARNESS_LAUNCHER = REPO_ROOT / "tools" / "soh3d_harness.py"
CACHE_ROOT = REPO_ROOT / "scratch" / "oracle_cache"
AZAHAR_PATCH_MD = REPO_ROOT / "tools" / "soh3d_harness" / "AZAHAR_PATCH.md"
GAMEPLAY_STATE = REPO_ROOT / "scratch" / "gameplay_settled.state"
