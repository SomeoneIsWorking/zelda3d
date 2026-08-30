"""Persistent cache for deterministic embedded-Azahar frames and probes.

The cache identity is the savestate bytes, ROM bytes, and documented Azahar
patch set. Changing any of those inputs creates a separate context under the
gitignored ``scratch/oracle_cache`` tree instead of serving stale evidence.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import time
from pathlib import Path
from typing import Any

from harness_paths import AZAHAR_PATCH_MD, CACHE_ROOT


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _patch_marker() -> str:
    """Return a compact cache discriminator for the complete Azahar patch contract."""
    if not AZAHAR_PATCH_MD.exists():
        return "nopatchmd"
    content = AZAHAR_PATCH_MD.read_bytes()
    headings = re.findall(r"^#{1,2}\s.*$", content.decode(errors="replace"), re.MULTILINE)
    digest = hashlib.sha256(content).hexdigest()[:8]
    return f"p{len(headings)}-{digest}"


def cache_key(savestate: Path, rom: Path | None = None) -> tuple[str, dict[str, Any]]:
    """Compute a cache key and the complete metadata used to derive it."""
    savestate = Path(savestate)
    savestate_sha = _sha256_file(savestate)[:16] if savestate.exists() else "nostate"
    rom_path = Path(rom) if rom else None
    if rom_path is None and os.environ.get("ZELDA3D_OOT3D_ROM"):
        rom_path = Path(os.environ["ZELDA3D_OOT3D_ROM"])
    rom_sha = _sha256_file(rom_path)[:16] if rom_path and rom_path.exists() else "norom"
    patch = _patch_marker()
    key = f"{savestate_sha}_{rom_sha}_{patch}"
    return key, {
        "key": key,
        "savestate_path": str(savestate),
        "savestate_sha256_16": savestate_sha,
        "rom_path": str(rom_path) if rom_path else None,
        "rom_sha256_16": rom_sha,
        "azahar_patch_marker": patch,
        "azahar_patch_md": str(AZAHAR_PATCH_MD),
    }


class OracleCache:
    """Store deterministic oracle frames, probes, and raw artifacts by input identity."""

    def __init__(self, savestate: Path, rom: Path | None = None):
        self.key, self.meta = cache_key(savestate, rom)
        self.dir = CACHE_ROOT / self.key
        self.frames_dir = self.dir / "frames"
        self.probes_dir = self.dir / "probes"
        self.artifacts_dir = self.dir / "artifacts"
        self.index_path = self.dir / "index.json"
        self._index: dict[str, Any] | None = None

    def _load_index(self) -> dict[str, Any]:
        if self._index is not None:
            return self._index
        if self.index_path.exists():
            self._index = json.loads(self.index_path.read_text())
        else:
            self._index = {"meta": self.meta, "frames": {}, "probes": {}}
        return self._index

    def _save_index(self) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        index = self._load_index()
        index["meta"] = self.meta
        self.index_path.write_text(json.dumps(index, indent=2, sort_keys=True))

    def get_frame(self, az_frame: int) -> Path | None:
        entry = self._load_index()["frames"].get(str(az_frame))
        if not entry:
            return None
        path = self.dir / entry["file"]
        return path if path.exists() else None

    def put_frame(self, az_frame: int, src_image_path: str | Path) -> Path:
        from PIL import Image

        index = self._load_index()
        self.frames_dir.mkdir(parents=True, exist_ok=True)
        destination = self.frames_dir / f"az{az_frame}.png"
        Image.open(src_image_path).convert("RGB").save(destination)
        index["frames"][str(az_frame)] = {
            "file": str(destination.relative_to(self.dir)),
            "captured": time.time(),
            "source": str(src_image_path),
        }
        self._save_index()
        return destination

    @staticmethod
    def _probe_key(probe_name: str, az_frame: int, args: dict | None) -> str:
        encoded_args = json.dumps(args or {}, sort_keys=True)
        digest = hashlib.sha256(encoded_args.encode()).hexdigest()[:10]
        return f"{probe_name}_{az_frame}_{digest}"

    def get_probe(
        self, probe_name: str, az_frame: int, args: dict | None = None
    ) -> Any | None:
        probe_key = self._probe_key(probe_name, az_frame, args)
        entry = self._load_index()["probes"].get(probe_key)
        if not entry:
            return None
        path = self.dir / entry["file"]
        return json.loads(path.read_text()) if path.exists() else None

    def put_probe(
        self, probe_name: str, az_frame: int, args: dict | None, data: Any
    ) -> None:
        index = self._load_index()
        probe_key = self._probe_key(probe_name, az_frame, args)
        self.probes_dir.mkdir(parents=True, exist_ok=True)
        destination = self.probes_dir / f"{probe_key}.json"
        destination.write_text(json.dumps(data, indent=2, sort_keys=True, default=str))
        index["probes"][probe_key] = {
            "file": str(destination.relative_to(self.dir)),
            "probe_name": probe_name,
            "az_frame": az_frame,
            "args": args or {},
            "captured": time.time(),
        }
        self._save_index()

    @staticmethod
    def _artifact_key(artifact_name: str, args: dict | None) -> str:
        """Return a stable, filesystem-safe key for one raw capture variant."""
        encoded = json.dumps(
            {"name": artifact_name, "args": args or {}},
            sort_keys=True,
            separators=(",", ":"),
            default=str,
        )
        digest = hashlib.sha256(encoded.encode()).hexdigest()[:12]
        readable = re.sub(r"[^A-Za-z0-9_.-]+", "_", artifact_name).strip("._")
        return f"{readable or 'artifact'}_{digest}"

    def get_artifact(
        self, artifact_name: str, args: dict | None = None
    ) -> Path | None:
        """Return a cached raw capture, or ``None`` when this exact variant is absent."""
        key = self._artifact_key(artifact_name, args)
        entry = self._load_index().get("artifacts", {}).get(key)
        if not entry:
            return None
        path = self.dir / entry["file"]
        return path if path.is_file() else None

    def put_artifact(
        self,
        artifact_name: str,
        args: dict | None,
        source_path: str | Path,
        suffix: str | None = None,
    ) -> Path:
        """Copy a raw capture into this key context and return its cache path."""
        source = Path(source_path)
        if not source.is_file():
            raise FileNotFoundError(source)
        index = self._load_index()
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)
        key = self._artifact_key(artifact_name, args)
        file_suffix = suffix if suffix is not None else source.suffix
        destination = self.artifacts_dir / f"{key}{file_suffix}"
        shutil.copyfile(source, destination)
        artifacts = index.setdefault("artifacts", {})
        artifacts[key] = {
            "file": str(destination.relative_to(self.dir)),
            "artifact_name": artifact_name,
            "args": args or {},
            "captured": time.time(),
            "source": str(source),
        }
        self._save_index()
        return destination

    def stats(self) -> dict[str, Any]:
        index = self._load_index()
        total_bytes = 0
        if self.dir.exists():
            for root, _dirs, files in os.walk(self.dir):
                for filename in files:
                    total_bytes += (Path(root) / filename).stat().st_size
        return {
            "key": self.key,
            "dir": str(self.dir),
            "n_frames": len(index["frames"]),
            "n_probes": len(index["probes"]),
            "bytes": total_bytes,
        }

    def invalidate(self) -> None:
        if self.dir.exists():
            shutil.rmtree(self.dir)
        self._index = None
