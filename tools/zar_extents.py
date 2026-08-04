#!/usr/bin/env python3
"""List every CMB in a ZAR with its local bounding box — the table needed to identify WHICH mesh an
actor draws (docs/multi_cmb_zar_risk.md, the three-axis method).

    tools/zar_extents.py zelda_hidan_objects            # by object name, read straight from the ROM
    tools/zar_extents.py /actor/zelda_hidan_objects.zar # by romfs path
    tools/zar_extents.py --n64 407,120,120              # rank meshes by fit to an N64 draw's extents

The ROM comes from $ZELDA3D_OOT3D_ROM (see .env). Nothing is written to disk.

WHY: every routing row so far has re-derived these numbers by hand, and two rows shipped the WRONG
MESH off a plausible Japanese name (m_Hsyarin "wheel" for a propeller; syokudai_ki vs isi). The
measurement overrules the name, so the measurement should be one command.

REFUSES rather than reporting an empty table: an unreadable ROM, a ZAR that is not in the ROM, or a
ZAR with no CMBs each exit non-zero saying what was searched. A silent "(no meshes)" is
indistinguishable from "I never looked".
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctr_romfs  # noqa: E402
import zar as zarmod  # noqa: E402
import cmb as cmbmod  # noqa: E402


def bbox(data):
    """Local-space bbox of a CMB's drawn triangles, or None if it has no geometry."""
    c = cmbmod.Cmb(data)
    xs, ys, zs = [], [], []
    for _, _, tri in c.triangles():
        for _, pos, _, _ in tri:
            xs.append(pos[0]); ys.append(pos[1]); zs.append(pos[2])
    if not xs:
        return None
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def main():
    args = [a for a in sys.argv[1:]]
    n64 = None
    if "--n64" in args:
        i = args.index("--n64")
        try:
            n64 = tuple(float(v) for v in args[i + 1].split(","))
            if len(n64) != 3:
                raise ValueError
        except (IndexError, ValueError):
            sys.exit("--n64 wants three comma-separated numbers: HEIGHT,FOOTX,FOOTZ (world units)")
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    name = args[0]

    rom = os.environ.get("ZELDA3D_OOT3D_ROM")
    if not rom or not os.path.exists(rom):
        sys.exit(f"ZELDA3D_OOT3D_ROM is unset or missing ({rom!r}) -- SEARCHED NOTHING. "
                 f"Source the repo .env first.")
    r = ctr_romfs.CtrRom(rom)

    path = name if name.startswith("/") else f"/actor/{name}.zar"
    if not path.endswith(".zar"):
        path += ".zar"
    try:
        fe = r.get(path)
    except Exception:
        cands = [f.path for f in r.iter_files()
                 if f.path.endswith(".zar") and name.strip("/").lower() in f.path.lower()]
        msg = f"{path} is not in the ROM. Searched every romfs entry."
        if cands:
            msg += " Near matches:\n  " + "\n  ".join(sorted(cands)[:12])
        sys.exit(msg)

    z = zarmod.Zar(r.read(fe))
    cmbs = [f for f in z.files if f.name.lower().endswith(".cmb")]
    if not cmbs:
        sys.exit(f"{path}: {len(z.files)} files, but NONE is a .cmb -- nothing to measure here.")

    rows = []
    skipped = []
    for f in cmbs:
        try:
            bb = bbox(z.read(f))
        except Exception as exc:                      # a parse failure is REPORTED, never swallowed
            skipped.append((f.name, f"parse failed: {exc}"))
            continue
        if bb is None:
            skipped.append((f.name, "no drawn triangles"))
            continue
        x0, x1, y0, y1, z0, z1 = bb
        rows.append((f.name, x1 - x0, y1 - y0, z1 - z0, y0))

    print(f"{path}: {len(z.files)} files, {len(cmbs)} CMBs, {len(rows)} measured")
    print(f"{'cmb':38} {'sizeX':>9} {'sizeY':>9} {'sizeZ':>9} {'minY':>9}")
    for nm, sx, sy, sz, my in sorted(rows, key=lambda r: -r[2]):
        print(f"{nm[:38]:38} {sx:9.1f} {sy:9.1f} {sz:9.1f} {my:9.1f}")

    if skipped:
        print(f"\nNOT measured ({len(skipped)}) -- these are BLIND SPOTS, not absences:")
        for nm, why in skipped:
            print(f"  {nm:38} {why}")

    if n64:
        h, fx, fz = n64
        print(f"\nRanked against an N64 draw of height={h} footprint={fx}x{fz}.")
        print("A correct mesh gives the SAME ratio on all three axes (that ratio is the actor's own")
        print("scale). Disagreement across axes is either a wrong mesh (gross) or Grezzo re-authoring")
        print("(modest and consistent) -- see docs/multi_cmb_zar_risk.md. Spread is max/min of the three.")
        scored = []
        for nm, sx, sy, sz, my in rows:
            if min(sx, sy, sz) < 1e-3:
                continue
            rh, rx, rz = h / sy, fx / sx, fz / sz
            spread = max(rh, rx, rz) / min(rh, rx, rz)
            scored.append((spread, nm, rh, rx, rz))
        if not scored:
            print("  no mesh has non-zero extent on all three axes -- cannot rank.")
        for spread, nm, rh, rx, rz in sorted(scored)[:12]:
            tag = ("  <== all three agree" if spread < 1.05 else
                   ("  (modest -- possible re-authoring)" if spread < 1.30 else ""))
            print(f"  spread {spread:6.2f}x  {nm[:34]:34} h={rh:.5f} x={rx:.5f} z={rz:.5f}{tag}")


if __name__ == "__main__":
    main()
