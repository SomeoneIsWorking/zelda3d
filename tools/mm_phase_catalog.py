#!/usr/bin/env python3
"""Authoritative deterministic scene catalog for the MM live phase tour."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Scene:
    name: str
    entrance: int
    scene_id: int


# Fixed spawn-0 entrances from EntranceSceneId in 2ship/include/z64scene.h,
# paired with runtime IDs in 2ship/include/tables/scene_table.h.
SCENES = (
    Scene("south_clock_town", 0xD800, 0x6F),
    Scene("east_clock_town", 0xD200, 0x6C),
    Scene("west_clock_town", 0xD400, 0x6D),
    Scene("north_clock_town", 0xD600, 0x6E),
    Scene("termina_field", 0x5400, 0x2D),
    Scene("romani_ranch", 0x6400, 0x35),
    Scene("great_bay_coast", 0x6800, 0x37),
    Scene("ikana_graveyard", 0x8000, 0x43),
    Scene("road_to_southern_swamp", 0x7A00, 0x40),
    Scene("woodfall", 0x8600, 0x46),
    Scene("woodfall_temple", 0x3000, 0x1B),
    Scene("great_bay_temple", 0x8C00, 0x49),
)
SCENES_BY_NAME = {scene.name: scene for scene in SCENES}


class SceneSelectionError(ValueError):
    pass


def select_scenes(names: str | None) -> tuple[Scene, ...]:
    if names is None:
        return SCENES

    selected: list[Scene] = []
    for name in (part.strip() for part in names.split(",")):
        if not name:
            continue
        try:
            scene = SCENES_BY_NAME[name]
        except KeyError as exc:
            known = ", ".join(SCENES_BY_NAME)
            raise SceneSelectionError(
                f"unknown scene '{name}' (choose from: {known})"
            ) from exc
        if scene not in selected:
            selected.append(scene)
    if not selected:
        raise SceneSelectionError("scene selection is empty")
    return tuple(selected)
