"""Authoritative filesystem layout for the embedded OoT3D harness tools."""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HARNESS_LAUNCHER = REPO_ROOT / "tools" / "soh3d_harness.py"
CACHE_ROOT = REPO_ROOT / "scratch" / "oracle_cache"
AZAHAR_RENDER_CONTRACT = REPO_ROOT / "tools" / "soh3d_harness" / "AZAHAR_RENDER_CONTRACT"


def azahar_render_contract_marker(contract: Path = AZAHAR_RENDER_CONTRACT) -> str:
    """Read the declared emulator contract that determines savestate compatibility."""
    if not contract.is_file():
        raise RuntimeError(f"missing Azahar render contract: {contract}")
    for line in contract.read_text().splitlines():
        marker = line.strip()
        if not marker or marker.startswith("#"):
            continue
        if not all(character.isalnum() or character in "._-" for character in marker):
            raise RuntimeError(f"invalid Azahar render contract marker: {marker!r}")
        return marker
    raise RuntimeError(f"Azahar render contract has no marker: {contract}")


# A savestate is serialized Azahar state, not just a deterministic input checkpoint. Keep it
# separate per declared emulator contract so a serializer change never loads incompatible bytes.
TITLE_STATE = REPO_ROOT / "scratch" / f"title_settled.{azahar_render_contract_marker()}.state"
TITLE_STATE_METADATA = REPO_ROOT / "scratch" / f"title_settled.{azahar_render_contract_marker()}.json"
GAMEPLAY_STATE = REPO_ROOT / "scratch" / f"gameplay_settled.{azahar_render_contract_marker()}.state"
