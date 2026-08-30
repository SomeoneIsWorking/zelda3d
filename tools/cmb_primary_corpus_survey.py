#!/usr/bin/env python3
"""Survey CmbVShader PRIMARY inputs across the OoT3D CMB corpus.

The shared CmbVShader chooses PRIMARY from two independent material/geometry
facts: ``IsVertexLighting`` and ``HasColor``.  In the unlit branch, a present
color attribute replaces ``MatDiffuseColor``; a missing color attribute leaves
``MatDiffuseColor`` as PRIMARY (CmbVShader words 112--120).

This tool reports the exact meshes for which the authored diffuse fallback is
observable: unlit, no color attribute data, and non-white material diffuse.
It reads the user-supplied ROM offline and never starts the oracle.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass

from cmb import Cmb, MODE_ARRAY, MODE_CONSTANT
from cmb_corpus import iter_cmbs


@dataclass(frozen=True)
class Candidate:
    label: str
    material_index: int
    mesh_id: int
    diffuse: tuple[int, int, int, int]


def _material_offset(cmb: Cmb, material_index: int) -> int:
    stride = 0x15C if cmb.version <= 6 else 0x16C
    return cmb.mats_ptr + 0x0C + material_index * stride


def _has_color(cmb: Cmb, sepd_index: int) -> bool:
    color = cmb.sepds[sepd_index].attrs["color"]
    if color.mode == MODE_CONSTANT:
        return True
    if color.mode != MODE_ARRAY:
        return False
    return cmb.vatr["color"][1] > 0


def candidates(label: str, data: bytes) -> list[Candidate]:
    cmb = Cmb(data)
    found: set[Candidate] = set()
    for mesh in cmb.meshes:
        if _has_color(cmb, mesh.sepd_index):
            continue
        material_offset = _material_offset(cmb, mesh.material_index)
        if data[material_offset + 1] != 0:  # IsVertexLighting
            continue
        diffuse = tuple(data[material_offset + 0xA8 : material_offset + 0xAC])
        if diffuse == (255, 255, 255, 255):
            continue
        found.add(Candidate(label, mesh.material_index, mesh.mesh_id, diffuse))
    return sorted(found, key=lambda item: (item.material_index, item.mesh_id))


def main() -> int:
    matches: list[Candidate] = []
    scanned = 0
    failed = 0
    try:
        corpus = iter_cmbs()
        for label, data in corpus:
            scanned += 1
            try:
                matches.extend(candidates(label, data))
            except (AssertionError, IndexError, KeyError, ValueError):
                failed += 1
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 2

    for match in matches:
        rgba = ",".join(str(channel) for channel in match.diffuse)
        print(f"{match.label} mat={match.material_index} mesh={match.mesh_id} diffuse={rgba}")
    print(f"files={scanned} candidates={len(matches)} parse_failures={failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
