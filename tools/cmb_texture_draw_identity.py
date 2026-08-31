#!/usr/bin/env python3
"""Join enabled CMB source textures to PICA draw-log texture descriptors.

The module deliberately stops short of claiming a draw identity from a matching
size or PICA texture format.  A caller must read the exact guest texture bytes
and use :func:`match_guest_payloads` before it can associate an oracle draw
with a CMB source texture.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass

from cmb import Cmb
from cmb_corpus import iter_cmbs
from cmb_fragment_lighting_survey import scan_materials
from oracle_compare import GLFMT_TO_PICA

DRAW_TEXTURE_RE = re.compile(
    r"^draw n=(?P<draw>\d+) .*\btex0=(?P<address>[0-9a-fA-F]+)/"
    r"(?P<width>\d+)x(?P<height>\d+)/f(?P<format>\d+)\b"
)


@dataclass(frozen=True)
class SourceTexture:
    label: str
    texture_name: str
    width: int
    height: int
    pica_format: int
    payload: bytes

    @property
    def digest(self) -> str:
        return hashlib.sha256(self.payload).hexdigest()

    @property
    def descriptor(self) -> tuple[int, int, int, int]:
        return self.width, self.height, self.pica_format, len(self.payload)


@dataclass(frozen=True)
class LoggedTexture:
    draw: int
    address: int
    width: int
    height: int
    pica_format: int


def source_textures(
    archive_path: str, *, require_enabled_fragment_primary: bool = True
) -> list[SourceTexture]:
    """Return source textures from an archive, optionally narrowed by material use.

    ``archive_path`` is the ROM-side ZAR path, so the source set is named by
    stable game data rather than a scene-dependent actor address.
    """
    textures: list[SourceTexture] = []
    for label, data in iter_cmbs():
        if label != archive_path and not label.startswith(f"{archive_path}:"):
            continue
        records = scan_materials(label, data)
        if require_enabled_fragment_primary and not any(
            record.enabled and record.primary_uses for record in records
        ):
            continue
        cmb = Cmb(data)
        for texture in cmb.textures:
            pica_format = GLFMT_TO_PICA.get(texture.gl_format)
            if pica_format is None:
                continue
            payload_start = cmb.texdata_ptr + texture.data_offset
            payload = cmb.data[payload_start : payload_start + texture.data_len]
            if len(payload) != texture.data_len:
                raise ValueError(
                    f"{label}:{texture.name}: truncated texture payload "
                    f"({len(payload)} != {texture.data_len})"
                )
            textures.append(
                SourceTexture(
                    label=label,
                    texture_name=texture.name,
                    width=texture.width,
                    height=texture.height,
                    pica_format=pica_format,
                    payload=payload,
                )
            )
    if not textures:
        raise RuntimeError(
            f"source archive {archive_path!r} scanned 0 "
            f"{'enabled fragment-primary ' if require_enabled_fragment_primary else ''}textures"
        )
    return textures


def enabled_fragment_source_textures(archive_path: str) -> list[SourceTexture]:
    """Return textures from CMBs with an enabled fragment-primary material."""
    return source_textures(archive_path, require_enabled_fragment_primary=True)


def logged_texture_descriptors(lines: list[str]) -> list[LoggedTexture]:
    """Parse every texture-0 descriptor from an oracle PICA uniform log."""
    descriptors: list[LoggedTexture] = []
    for line in lines:
        match = DRAW_TEXTURE_RE.match(line)
        if match is None:
            continue
        descriptors.append(
            LoggedTexture(
                draw=int(match.group("draw")),
                address=int(match.group("address"), 16),
                width=int(match.group("width")),
                height=int(match.group("height")),
                pica_format=int(match.group("format")),
            )
        )
    if not descriptors:
        raise RuntimeError("oracle draw log scanned 0 texture-0 descriptors")
    return descriptors


def descriptor_candidates(
    logged: list[LoggedTexture], sources: list[SourceTexture]
) -> list[LoggedTexture]:
    """Return unique draw textures compatible with one exact source byte length."""
    descriptors = {source.descriptor[:3] for source in sources}
    candidates: dict[tuple[int, int], LoggedTexture] = {}
    for texture in logged:
        if (texture.width, texture.height, texture.pica_format) not in descriptors:
            continue
        candidates[(texture.draw, texture.address)] = texture
    return sorted(candidates.values(), key=lambda texture: (texture.draw, texture.address))


def match_guest_payloads(
    candidates: list[LoggedTexture],
    sources: list[SourceTexture],
    guest_payloads: dict[int, bytes],
) -> list[tuple[LoggedTexture, SourceTexture]]:
    """Return only exact source/guest raw-byte matches.

    The descriptor remains part of the comparison: equal raw bytes from another
    PICA format are not a source identity.
    """
    matches: list[tuple[LoggedTexture, SourceTexture]] = []
    by_descriptor: dict[tuple[int, int, int], list[SourceTexture]] = {}
    for source in sources:
        by_descriptor.setdefault(source.descriptor[:3], []).append(source)
    for candidate in candidates:
        payload = guest_payloads.get(candidate.address)
        if payload is None:
            continue
        for source in by_descriptor.get(
            (candidate.width, candidate.height, candidate.pica_format), []
        ):
            if payload == source.payload:
                matches.append((candidate, source))
    return matches
