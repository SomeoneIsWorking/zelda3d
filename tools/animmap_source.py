#!/usr/bin/env python3
"""Shared SOURCE OF TRUTH for the N64-animation -> OoT3D-CSAB replacement table.

Both generators read from here so the game table and the charcompare tool index can never
diverge:
  - tools/gen_animmap_inc.py      -> Shipwright/soh/src/zelda3d/zelda3d_animmap.inc   (game)
  - tools/gen_charcompare_index.py -> Shipwright/soh/charcompare/charcompare_index.inc (tool)

Inputs:
  - tools/skeldata/animmap.json            : auto-matched `best` CSAB per N64 anim (the bulk).
  - tools/skeldata/charcompare_overrides.tsv : hand-verified corrections that OVERRIDE `best`,
    keyed by (zar, n64anim). Tab-separated: <zar>\\t<n64anim>\\t<csab>; #-comments / blanks ok.

`load()` overlays the overrides onto animmap.json and returns the resolved table. Unused
override keys (no matching row) are reported so a typo in the TSV can't silently do nothing.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANIMMAP = os.path.join(REPO, "tools/skeldata/animmap.json")
OVERRIDES = os.path.join(REPO, "tools/skeldata/charcompare_overrides.tsv")


def load_overrides(path=OVERRIDES):
    """Parse the shared overrides TSV. Returns {(zar, n64anim): csab}."""
    ov = {}
    if not os.path.exists(path):
        return ov
    for ln, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            raise ValueError("%s:%d: need 3 tab-separated cols (<zar>\\t<n64anim>\\t<csab>), got %r"
                             % (path, ln, line))
        ov[(parts[0], parts[1])] = parts[2]
    return ov


def load(animmap_path=ANIMMAP, overrides_path=OVERRIDES, warn=True):
    """Overlay overrides onto animmap.json. Returns (entries, n_overrides_applied).

    entries: list (sorted by zar) of {zar, object, rows:[{n64, otr, frameCount, csab,
    auto, overridden}]}, where csab = override if present else animmap `best`."""
    am = json.load(open(animmap_path))
    ov = load_overrides(overrides_path)
    used = set()
    n_ov = 0
    out = []
    for zar in sorted(am):
        d = am[zar]
        rows = []
        for r in d["rows"]:
            auto = r.get("best") or ""
            key = (zar, r["n64"])
            if key in ov:
                csab = ov[key]
                used.add(key)
                n_ov += 1
                overridden = True
            else:
                csab = auto
                overridden = False
            rows.append(dict(n64=r["n64"], otr=r["otr"],
                             frameCount=int(r["frameCount"] or 0),
                             csab=csab, auto=auto, overridden=overridden))
        out.append(dict(zar=zar, object=d["object"], rows=rows))
    unused = sorted(set(ov) - used)
    if unused and warn:
        sys.stderr.write("WARNING: %d override(s) matched no animmap.json row (typo?):\n" % len(unused))
        for zar, n64 in unused:
            sys.stderr.write("  (%s, %s)\n" % (zar, n64))
    return out, n_ov


if __name__ == "__main__":
    entries, n_ov = load()
    nrows = sum(len(e["rows"]) for e in entries)
    print("%d zars, %d anim rows, %d overrides applied" % (len(entries), nrows, n_ov))
