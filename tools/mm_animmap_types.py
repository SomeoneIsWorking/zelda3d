"""Shared immutable data records for MM animation-map generation."""

from __future__ import annotations

from dataclasses import dataclass

from mm_animmap_matching import Match


@dataclass(frozen=True)
class ActorResult:
    obj: str
    gar: str | None
    clips: tuple[str, ...]
    matches: tuple[Match, ...]
    # Informational only: never borrow another actor's clips for matching.
    alt_gar_hint: str | None = None
