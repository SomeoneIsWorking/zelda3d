#!/usr/bin/env python3
"""Inventory mesh-ID groups in one exact MM3D Player body CMB.

The runtime renderer can select CMB groups by ``mesh_id``, but the five Player
forms use different ID sets. This tool reads one exact GAR member so a form can
be swept without conflating same-named or auxiliary CMBs.

Usage:
  mm_player_cmb_dump.py ARCHIVE_PATH CMB_MEMBER_PATH

Example:
  mm_player_cmb_dump.py /actors/zelda2_link_child_new.gar.lzs \
      child/model/link_child.cmb
"""

from __future__ import annotations

import sys
from collections import defaultdict
from dataclasses import dataclass, field

import cmb
from mm_animmap_archive import Gar, GarFile, Mm3dActors


@dataclass
class MeshIdSummary:
    mesh_count: int = 0
    materials: set[int] = field(default_factory=set)
    textures: set[str] = field(default_factory=set)


def exact_cmb_member(archive: Gar, member_path: str) -> GarFile:
    matches = [entry for entry in archive.entries if entry.path == member_path]
    if len(matches) != 1:
        raise ValueError(
            f"expected one exact CMB member {member_path!r}; found {len(matches)}"
        )
    entry = matches[0]
    if entry.type != "cmb" and not entry.path.lower().endswith(".cmb"):
        raise ValueError(f"exact member is not a CMB: {member_path!r}")
    if entry.data[:4] != b"cmb ":
        raise ValueError(f"exact member has no CMB magic: {member_path!r}")
    return entry


def summarize_mesh_ids(model: cmb.Cmb) -> dict[int, MeshIdSummary]:
    summaries: dict[int, MeshIdSummary] = defaultdict(MeshIdSummary)
    for mesh in model.meshes:
        summary = summaries[mesh.mesh_id]
        summary.mesh_count += 1
        summary.materials.add(mesh.material_index)
        texture_index = model.material_texture(mesh.material_index)
        if 0 <= texture_index < len(model.textures):
            summary.textures.add(model.textures[texture_index].name)
    return dict(summaries)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} ARCHIVE_PATH CMB_MEMBER_PATH", file=sys.stderr)
        return 2

    archive_path, member_path = argv[1:]
    actors = Mm3dActors()
    try:
        archive_file = actors.rom.get(archive_path)
    except KeyError:
        raise SystemExit(f"archive not found: {archive_path}")
    archive = Gar(actors.rom.read(archive_file))
    entry = exact_cmb_member(archive, member_path)
    model = cmb.Cmb(entry.data)
    summaries = summarize_mesh_ids(model)

    print(f"archive={archive_path}")
    print(f"member={member_path}")
    print(
        f"cmb={model.name} version={model.version} bones={len(model.bones)} "
        f"meshes={len(model.meshes)} sepds={len(model.sepds)} "
        f"materials={len(model.materials)} textures={len(model.textures)} "
        f"mesh_ids={len(summaries)}"
    )
    for mesh_id, summary in sorted(summaries.items()):
        print(
            f"mid={mesh_id:2d} meshes={summary.mesh_count:2d} "
            f"materials={','.join(map(str, sorted(summary.materials)))} "
            f"textures={','.join(sorted(summary.textures)) or '-'}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
