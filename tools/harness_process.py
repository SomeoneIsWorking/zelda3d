"""Child-process construction for the embedded OoT3D harness."""

from __future__ import annotations

from collections.abc import Mapping

from harness_paths import HARNESS_LAUNCHER
from harness_transport import Harness


def spawn(save_state: str | None = None, environment: Mapping[str, str] | None = None) -> Harness:
    """Start the launcher and optionally load a state after it becomes ready."""
    if not HARNESS_LAUNCHER.exists():
        raise RuntimeError(f"soh3d_harness launcher not found: {HARNESS_LAUNCHER}")
    harness = Harness([str(HARNESS_LAUNCHER)], environment)
    if save_state:
        response = harness.send(f"loadstate {save_state}")
        if not response.startswith("ok"):
            harness.close()
            raise RuntimeError(f"loadstate failed: {response}")
    return harness
