"""Persistent cache for deterministic embedded-Azahar frames and probes.

The cache identity is the savestate bytes, ROM bytes, declared render-affecting
Azahar patch contract, and resolved texture-pack manifest. Changing any input
creates a separate context under the gitignored ``scratch/oracle_cache`` tree
instead of serving stale graphics evidence.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import time
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from harness_paths import AZAHAR_RENDER_CONTRACT, CACHE_ROOT, REPO_ROOT, azahar_render_contract_marker
from repo_environment import apply_repo_environment


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _patch_marker() -> str:
    """Return the declared cache discriminator for render-affecting Azahar changes only."""
    return azahar_render_contract_marker(AZAHAR_RENDER_CONTRACT)


def _runtime_environment(environment: Mapping[str, str] | None = None) -> dict[str, str]:
    resolved = dict(os.environ if environment is None else environment)
    apply_repo_environment(REPO_ROOT, resolved)
    return resolved


def _resolved_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else REPO_ROOT / path


def _texture_pack_marker(
    environment: Mapping[str, str], rom_path: Path | None
) -> tuple[str, dict[str, Any]]:
    disabled = {"0", "off", "none"}
    harness_mode = environment.get("ZELDA3D_HARNESS_TEXPACK", "on").lower()
    explicit = environment.get("ZELDA3D_TEXPACK")
    if harness_mode in disabled or (explicit and explicit.lower() in disabled):
        return "tpoff", {"mode": "off", "root": None}

    candidates: list[tuple[str, Path]] = []
    if explicit:
        candidates.append(("explicit", _resolved_path(explicit)))
    else:
        candidates.append(("repo", REPO_ROOT / "textures"))
        if rom_path is not None:
            candidates.append(("rom", rom_path.parent / "textures"))

    for origin, root in candidates:
        if not root.is_dir():
            continue
        files = sorted(path for path in root.rglob("*") if path.is_file())
        if not any(path.name.startswith("tex1_") for path in files):
            continue
        digest = hashlib.sha256()
        total_bytes = 0
        for path in files:
            stat = path.stat()
            total_bytes += stat.st_size
            digest.update(str(path.relative_to(root)).encode())
            digest.update(f"\0{stat.st_size}\0{stat.st_mtime_ns}\0".encode())
        short_digest = digest.hexdigest()[:12]
        return f"tp{len(files)}-{short_digest}", {
            "mode": "on",
            "root": str(root.resolve()),
            "root_origin": origin,
            "manifest_files": len(files),
            "manifest_bytes": total_bytes,
            "manifest_sha256_12": short_digest,
        }
    return "tpnone", {"mode": "off", "root": None, "reason": "no-valid-pack"}


def cache_key(
    savestate: Path,
    rom: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> tuple[str, dict[str, Any]]:
    """Compute a cache key and the complete metadata used to derive it."""
    savestate = Path(savestate)
    runtime_environment = _runtime_environment(environment)
    savestate_sha = _sha256_file(savestate)[:16] if savestate.exists() else "nostate"
    rom_path = Path(rom) if rom else None
    if rom_path is None and runtime_environment.get("ZELDA3D_OOT3D_ROM"):
        rom_path = _resolved_path(runtime_environment["ZELDA3D_OOT3D_ROM"])
    rom_sha = _sha256_file(rom_path)[:16] if rom_path and rom_path.exists() else "norom"
    patch = _patch_marker()
    texture_pack, texture_pack_meta = _texture_pack_marker(runtime_environment, rom_path)
    key = f"{savestate_sha}_{rom_sha}_{patch}_{texture_pack}"
    return key, {
        "key": key,
        "savestate_path": str(savestate),
        "savestate_sha256_16": savestate_sha,
        "rom_path": str(rom_path) if rom_path else None,
        "rom_sha256_16": rom_sha,
        "azahar_patch_marker": patch,
        "azahar_render_contract": str(AZAHAR_RENDER_CONTRACT),
        "texture_pack": texture_pack_meta,
    }


def frame_inputs_compatible(first: Mapping[str, Any], second: Mapping[str, Any]) -> bool:
    """Return whether two contexts share every pixel input except the Azahar patch contract.

    This is intentionally narrower than cache-key equality. It supports an explicit, provenance-
    preserving frame adoption after an observer-only patch change; callers must still establish
    that the patch did not alter rendering.
    """

    def identity(metadata: Mapping[str, Any]) -> tuple[Any, ...]:
        texture_pack = metadata.get("texture_pack", {})
        return (
            metadata.get("savestate_sha256_16"),
            metadata.get("rom_sha256_16"),
            texture_pack.get("mode"),
            texture_pack.get("manifest_files"),
            texture_pack.get("manifest_bytes"),
            texture_pack.get("manifest_sha256_12"),
        )

    return identity(first) == identity(second)


class OracleCache:
    """Store deterministic oracle frames, probes, and raw artifacts by input identity."""

    def __init__(
        self,
        savestate: Path,
        rom: Path | None = None,
        environment: Mapping[str, str] | None = None,
    ):
        self.key, self.meta = cache_key(savestate, rom, environment)
        self.dir = CACHE_ROOT / self.key
        self.frames_dir = self.dir / "frames"
        self.probes_dir = self.dir / "probes"
        self.artifacts_dir = self.dir / "artifacts"
        self.index_path = self.dir / "index.json"
        self._index: dict[str, Any] | None = None

    @classmethod
    def open_existing_context(cls, context_key: str) -> "OracleCache":
        """Open an existing cache context without recomputing its input identity.

        Historical oracle artifacts must remain attached to the contract that produced
        them.  This deliberately refuses arbitrary paths and missing indexes rather
        than silently creating a context under the current Azahar contract.
        """
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", context_key):
            raise ValueError("invalid oracle cache context key")
        directory = CACHE_ROOT / context_key
        index_path = directory / "index.json"
        if not index_path.is_file():
            raise FileNotFoundError(index_path)
        instance = cls.__new__(cls)
        instance.key = context_key
        instance.dir = directory
        instance.frames_dir = directory / "frames"
        instance.probes_dir = directory / "probes"
        instance.artifacts_dir = directory / "artifacts"
        instance.index_path = index_path
        instance._index = json.loads(index_path.read_text())
        instance.meta = dict(instance._index.get("meta", {}))
        return instance

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

    def adopt_frame(self, source_context: Path, az_frame: int) -> Path:
        """Copy one frame from a pixel-compatible context and record its provenance.

        The caller is responsible for asserting that the source/current patch difference is
        observer-only. Pixel inputs are still checked here so ROM, savestate, and texture-pack
        mismatches cannot be adopted accidentally.
        """
        source_context = Path(source_context)
        source_index_path = source_context / "index.json"
        if not source_index_path.is_file():
            raise FileNotFoundError(source_index_path)
        source_index = json.loads(source_index_path.read_text())
        if not frame_inputs_compatible(self.meta, source_index.get("meta", {})):
            raise ValueError("source cache context has different frame inputs")
        source_entry = source_index.get("frames", {}).get(str(az_frame))
        if source_entry is None:
            raise KeyError(f"source cache has no az frame {az_frame}")
        source_path = source_context / source_entry["file"]
        destination = self.put_frame(az_frame, source_path)
        entry = self._load_index()["frames"][str(az_frame)]
        entry["adopted_from_key"] = source_context.name
        entry["adopted_source"] = str(source_path)
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
            "n_artifacts": len(index.get("artifacts", {})),
            "bytes": total_bytes,
        }

    def invalidate(self) -> None:
        if self.dir.exists():
            shutil.rmtree(self.dir)
        self._index = None
