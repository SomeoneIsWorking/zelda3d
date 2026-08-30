#!/usr/bin/env python3
"""Survey CmbVShader PRIMARY inputs across the OoT3D CMB corpus.

The shared CmbVShader chooses PRIMARY from two independent material/geometry
facts: ``IsVertexLighting`` and ``HasColor``.  In the unlit branch, a present
color attribute replaces ``MatDiffuseColor``; a missing color attribute leaves
``MatDiffuseColor`` as PRIMARY (CmbVShader words 112--120).

This tool reports both binary-authored branches where absent color data matters:

* unlit meshes whose PRIMARY is the non-white MatDiffuseColor; and
* vertex-lit meshes whose authored MatDiffuse alpha reaches PRIMARY alpha.

It reads the user-supplied ROM offline and never starts the oracle.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass

from cmb import Cmb, MODE_ARRAY, MODE_CONSTANT
from cmb_corpus import iter_cmbs
from tev_corpus_survey import parse_mats, slots_used


@dataclass(frozen=True)
class Candidate:
    label: str
    material_index: int
    mesh_id: int
    diffuse: tuple[int, int, int, int]


@dataclass(frozen=True)
class LitAlphaCandidate:
    label: str
    material_index: int
    mesh_id: int
    diffuse_alpha: int


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


def _uses_primary_alpha(stages: list[object]) -> bool:
    return any(
        stage.a_src[slot] == 0x8577
        for stage in stages
        for slot in range(slots_used(stage.a_op))
    )


def scan_candidates(label: str, data: bytes) -> tuple[list[Candidate], list[LitAlphaCandidate]]:
    cmb = Cmb(data)
    stages_by_material = {material: stages for material, _, _, _, stages in parse_mats(data)}
    unlit: set[Candidate] = set()
    lit_alpha: set[LitAlphaCandidate] = set()
    for mesh in cmb.meshes:
        if _has_color(cmb, mesh.sepd_index):
            continue
        material_offset = _material_offset(cmb, mesh.material_index)
        diffuse = tuple(data[material_offset + 0xA8 : material_offset + 0xAC])
        if data[material_offset + 1] == 0:  # IsVertexLighting
            if diffuse != (255, 255, 255, 255):
                unlit.add(Candidate(label, mesh.material_index, mesh.mesh_id, diffuse))
            continue
        if diffuse[3] == 255 or not _uses_primary_alpha(stages_by_material[mesh.material_index]):
            continue
        lit_alpha.add(LitAlphaCandidate(label, mesh.material_index, mesh.mesh_id, diffuse[3]))
    sort_key = lambda item: (item.material_index, item.mesh_id)
    return sorted(unlit, key=sort_key), sorted(lit_alpha, key=sort_key)


def candidates(label: str, data: bytes) -> list[Candidate]:
    """Compatibility entry point for the original unlit PRIMARY survey."""
    return scan_candidates(label, data)[0]


def lit_alpha_candidates(label: str, data: bytes) -> list[LitAlphaCandidate]:
    return scan_candidates(label, data)[1]


def main() -> int:
    unlit_matches: list[Candidate] = []
    lit_alpha_matches: list[LitAlphaCandidate] = []
    scanned = 0
    failed = 0
    try:
        corpus = iter_cmbs()
        for label, data in corpus:
            scanned += 1
            try:
                unlit, lit_alpha = scan_candidates(label, data)
                unlit_matches.extend(unlit)
                lit_alpha_matches.extend(lit_alpha)
            except (AssertionError, IndexError, KeyError, ValueError):
                failed += 1
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 2

    for match in unlit_matches:
        rgba = ",".join(str(channel) for channel in match.diffuse)
        print(f"unlit {match.label} mat={match.material_index} mesh={match.mesh_id} diffuse={rgba}")
    for match in lit_alpha_matches:
        print(
            f"lit-alpha {match.label} mat={match.material_index} mesh={match.mesh_id} "
            f"diffuse_alpha={match.diffuse_alpha}"
        )
    print(
        f"files={scanned} unlit_candidates={len(unlit_matches)} "
        f"lit_alpha_candidates={len(lit_alpha_matches)} parse_failures={failed}"
    )
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
