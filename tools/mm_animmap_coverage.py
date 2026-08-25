"""Single immutable authority for MM skinned-animation coverage buckets."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from enum import Enum

from mm_animmap_matching import Match
from mm_animmap_types import ActorResult


class CoverageBucket(str, Enum):
    FULL = "full"
    PARTIAL = "partial"
    ZERO = "zero"
    EXCLUDED = "excluded"


@dataclass(frozen=True)
class ActorCoverage:
    result: ActorResult
    bucket: CoverageBucket
    matched: int
    mappable_unmatched: tuple[Match, ...]


@dataclass(frozen=True)
class CoverageSummary:
    actors: tuple[ActorCoverage, ...]
    full: tuple[str, ...]
    partial: tuple[str, ...]
    zero: tuple[str, ...]
    excluded: int


def _is_mappable_unmatched(match: Match) -> bool:
    if match.clip:
        return False
    return not (match.why.startswith("non-skeletal") or "absent from MM3D" in match.why)


def classify_coverage(results: Sequence[ActorResult]) -> CoverageSummary:
    actors = []
    for result in results:
        matched = sum(1 for match in result.matches if match.clip)
        unmatched = tuple(
            match for match in result.matches if _is_mappable_unmatched(match)
        )
        if not result.gar or not result.clips:
            bucket = CoverageBucket.EXCLUDED
        elif matched == 0:
            bucket = CoverageBucket.ZERO
        elif unmatched:
            bucket = CoverageBucket.PARTIAL
        else:
            bucket = CoverageBucket.FULL
        actors.append(ActorCoverage(result, bucket, matched, unmatched))

    frozen = tuple(actors)
    return CoverageSummary(
        actors=frozen,
        full=tuple(
            sorted(
                item.result.obj for item in frozen if item.bucket is CoverageBucket.FULL
            )
        ),
        partial=tuple(
            sorted(
                item.result.obj
                for item in frozen
                if item.bucket is CoverageBucket.PARTIAL
            )
        ),
        zero=tuple(
            sorted(
                item.result.obj for item in frozen if item.bucket is CoverageBucket.ZERO
            )
        ),
        excluded=sum(1 for item in frozen if item.bucket is CoverageBucket.EXCLUDED),
    )
