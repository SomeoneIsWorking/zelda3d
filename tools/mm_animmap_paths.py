"""Repository paths and local environment loading for MM animation-map tools."""

from __future__ import annotations

import os
from pathlib import Path

from repo_environment import apply_repo_environment

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OBJ_DIR = os.path.join(REPO, "2ship", "assets", "objects")


def load_env() -> None:
    apply_repo_environment(Path(REPO), os.environ)
