"""Independent re-verification of evidence-backed animation overrides."""

from __future__ import annotations

import os
import struct
import zipfile
from typing import Dict, Optional

from mm_animmap_archive import Gar, Mm3dActors
from mm_animmap_inventory import object_to_gar
from mm_animmap_matching import VERIFIED_OVERRIDES
from mm_animmap_paths import REPO


def verify_overrides(o2r_path: Optional[str] = None) -> int:
    """Re-derive VERIFIED_OVERRIDES from the assets. Exits non-zero if it cannot check, rather
    than reporting a pass it did not earn.

    The check is duration equality: N64 animation frameCount (u16 @ 0x44 of the o2r resource)
    against MM3D csab duration (u32 @ 0x34). Both sides are read fresh; nothing is taken from the
    comment above the table."""
    o2r = o2r_path or os.path.join(REPO, "2ship", "mm.o2r")
    if not os.path.exists(o2r):
        print(
            "VERIFY: REFUSING -- no N64 archive at %s, so the N64 side of every override is "
            "UNCHECKED. Build 2ship/mm.o2r first. Checked 0 of %d overrides."
            % (o2r, sum(len(v) for v in VERIFIED_OVERRIDES.values()))
        )
        return 2
    z = zipfile.ZipFile(o2r)
    actors = Mm3dActors()
    bad = checked = 0
    for obj, table in sorted(VERIFIED_OVERRIDES.items()):
        gar = (object_to_gar(obj, set(actors.actors)) or [None])[0]
        durations = _csab_durations(actors, gar) if gar else {}
        if not durations:
            print(
                "VERIFY: REFUSING -- no csab durations for %s (gar=%s); %d overrides UNCHECKED."
                % (obj, gar, len(table))
            )
            return 2
        for sym, clip in sorted(table.items()):
            key = "objects/%s/%s" % (obj, sym)
            try:
                fc = struct.unpack_from("<H", z.read(key), 0x44)[0]
            except (KeyError, struct.error) as e:
                print("VERIFY: FAIL %s -- cannot read N64 frameCount (%s)" % (key, e))
                bad += 1
                continue
            got = durations.get(clip)
            checked += 1
            # TOLERANCE 1, and it is measured rather than assumed. The gameplay_keep doors match
            # EXACTLY (88 vs 88); object_delf runs fc = duration + 1 across every pair in the actor
            # (24/23, 24/23, 56/55, 56/55). So the two sides do not share one frame-count convention,
            # and an equality check would fail a correct override. What the check must catch is a
            # mapping to the WRONG animation, and those are not off by one: the annotation this
            # override corrects pointed at a clip 33 frames away.
            if got is None or abs(got - fc) > 1:
                print(
                    "VERIFY: FAIL %s -> %s: N64 frameCount %s vs csab duration %s (delta %s)"
                    % (sym, clip, fc, got, "n/a" if got is None else abs(got - fc))
                )
                bad += 1
            else:
                print(
                    "VERIFY: ok   %-36s -> %-24s fc=%d dur=%d (delta %d)"
                    % (sym, clip, fc, got, abs(got - fc))
                )
    print("VERIFY: checked %d override(s), %d failed." % (checked, bad))
    return 1 if bad else 0


def _csab_durations(actors: "Mm3dActors", gar: str) -> Dict[str, int]:
    fe = actors.actors.get(gar)
    if fe is None:
        return {}
    out: Dict[str, int] = {}
    for e in Gar(actors.rom.read(fe)).entries:
        p = (e.path or e.name or "").replace("\\", "/")
        if p.endswith(".csab") and len(e.data) >= 0x38:
            out[p.split("/")[-1][:-5]] = struct.unpack_from("<I", e.data, 0x34)[0]
    return out
