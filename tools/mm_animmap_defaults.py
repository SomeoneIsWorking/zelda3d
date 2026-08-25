"""Evidence-ordered default animation selection for MM actor archives."""

from __future__ import annotations

import re

from mm_animmap_inventory import symbol_of
from mm_animmap_types import ActorResult

_N64_IDLE_RE = (
    re.compile(r"Wait", re.IGNORECASE),
    re.compile(r"Idle", re.IGNORECASE),
    re.compile(r"Stand", re.IGNORECASE),
)


def resolve_default_anim(result: ActorResult) -> tuple[str | None, str]:
    """Return the strongest evidenced fallback clip and the evidence description."""
    wait = [clip for clip in sorted(result.clips) if "wait" in clip.lower()]
    if wait:
        return wait[0], "GAR *wait* clip"
    for pattern in _N64_IDLE_RE:
        for match in result.matches:
            if match.clip and pattern.search(symbol_of(match.symbol)):
                return (
                    match.clip,
                    f"clip mapped from N64 /{pattern.pattern}/ symbol {symbol_of(match.symbol)}",
                )
    if len(result.clips) == 1:
        return result.clips[0], "the archive's only clip"
    return None, "no idle determinable (no *wait* clip, no mapped N64 idle symbol)"
