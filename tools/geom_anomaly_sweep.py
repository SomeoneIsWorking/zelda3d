#!/usr/bin/env python3
"""geom_anomaly_sweep.py — AUTOMATICALLY surface mis-rendered geometry in a scene (no eyeball, no
manual roomwarp/grep).

For a scene it cold-boots SoH3D headless, walks EVERY room (roomwarp), and collects two things the
running engine already computes:
  * `autostate` — every auto-replaced object's derived world scale (= N64 actor height / OoT3D-CMB
    height) + the measured N64 height + its zar.
  * `geomscan all` — each draw's actual world-space AABB.
It then cross-checks each replaced object's RENDERED world extent against the object's TRUE extent
parsed offline from its .zar CMB, and flags anomalies AUTOMATICALLY by two scene-relative signals
(so it adapts to any scene instead of a hand-picked absolute threshold):
  * SCALE OUTLIER — log10(derived scale) is a robust-MAD outlier vs the scene's other replaced
    objects (a door scaled 13x while everything else scales <1x stands out on its own).
  * ROOM-SIZED    — the object's rendered extent is a large fraction of the room mesh extent
    (a prop as big as the room it sits in).
  * NAN / non-finite render.

Output is a ranked anomaly report. Exit code 1 if any anomaly is flagged (usable in CI/parity gates).

Usage:
  SOH3D_3DS_ROM=<rom> tools/geom_anomaly_sweep.py <entrance> [--prefix jyasinzou] [--keep]
  e.g. tools/geom_anomaly_sweep.py 130            # Spirit Temple
"""
import os, sys, re, math, json, subprocess, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))


def repl(cmd):
    r = subprocess.run([os.path.join(REPO, "tools", "soh3d_repl.py"), "cmd", cmd],
                       cwd=REPO, capture_output=True, text=True)
    return r.stdout


def offline_cmb_maxext(zar_path, _cache={}):
    if zar_path in _cache:
        return _cache[zar_path]
    import zar as zarmod, cmb as cmbmod
    from ctr_romfs import CtrRom
    if "_rom" not in _cache:
        _cache["_rom"] = CtrRom(os.environ["SOH3D_3DS_ROM"])
    rom = _cache["_rom"]
    val = None
    try:
        z = zarmod.Zar(rom.read(rom.get(zar_path)))
        cmbb = None
        for f in z.files:
            nm = f if isinstance(f, str) else getattr(f, "name", str(f))
            if nm.lower().endswith(".cmb"):
                cmbb = z.read(f); break
        if cmbb is not None:
            m = cmbmod.Cmb(cmbb)
            mn = [math.inf] * 3; mx = [-math.inf] * 3
            for _si, _mt, verts in m.triangles():
                for _i, pos, _n, _uv in verts:
                    for k in range(3):
                        mn[k] = min(mn[k], pos[k]); mx[k] = max(mx[k], pos[k])
            if all(math.isfinite(v) for v in mn + mx):
                val = max(mx[k] - mn[k] for k in range(3))
    except Exception:
        val = None
    _cache[zar_path] = val
    return val


def parse_autostate(text, into):
    # auto[0x16d] /actor/zelda_jya_door.zar state=2 scale=13.20816 n64h=2113.3 model=2006
    for m in re.finditer(r"auto\[(0x[0-9a-fA-F]+)\]\s+(\S+)\s+state=(-?\d+)\s+scale=([\d.]+)\s+n64h=([\d.]+)\s+model=(\d+)", text):
        mid = int(m.group(6))
        into[mid] = {"objid": m.group(1), "zar": m.group(2), "scale": float(m.group(4)),
                     "n64h": float(m.group(5)), "model": mid}


def parse_geomscan(text, by_model, by_zar):
    # ['  ', 'HUGE ', 'NAN '] model=N ext=(x,y,z) maxext=M wmin=(...) <zar|?>
    for m in re.finditer(r"model=(\d+)\s+ext=\([\d.-]+,[\d.-]+,[\d.-]+\)\s+maxext=([\d.-]+)\s+wmin=\([^)]*\)\s+(\S+)", text):
        mid = int(m.group(1)); mx = float(m.group(2)); zar = m.group(3)
        for store, key in ((by_model, mid), (by_zar, zar if zar != "?" else None)):
            if key is None:
                continue
            d = store.setdefault(key, {"rmaxext": 0.0, "nan": False})
            if mx != mx:
                d["nan"] = True
            else:
                d["rmaxext"] = max(d["rmaxext"], mx)


def robust_outliers(vals):
    """Return per-index modified z-score (MAD based) for a list of values."""
    n = len(vals)
    if n < 3:
        return [0.0] * n
    s = sorted(vals); med = s[n // 2]
    devs = sorted(abs(v - med) for v in vals); mad = devs[n // 2]
    if mad == 0:
        return [0.0] * n
    return [0.6745 * (v - med) / mad for v in vals]


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); sys.exit(2)
    entrance = args[0]
    prefix = None
    keep = "--keep" in args
    if "--prefix" in args:
        prefix = args[args.index("--prefix") + 1]

    env = dict(os.environ, SOH3D_HEADLESS="1")
    subprocess.run([os.path.join(REPO, "tools", "soh3d_game.sh"), "start", str(entrance), "0x6000"],
                   cwd=REPO, env=env, capture_output=True, text=True)
    subprocess.run([os.path.join(REPO, "tools", "soh3d_repl.py"), "ready"], cwd=REPO, capture_output=True)
    time.sleep(2)
    try:
        rinfo = repl("roominfo")
        mr = re.search(r"rooms=(\d+)", rinfo)
        nrooms = int(mr.group(1)) if mr else 1
        print(f"scene entrance {entrance}: {nrooms} rooms — sweeping...", file=sys.stderr)

        auto = {}; geo = {}; geo_zar = {}; roomext = []
        for r in range(nrooms):
            repl(f"roomwarp {r}")
            time.sleep(0.3)
            parse_autostate(repl("autostate"), auto)
            gtext = repl("geomscan all")
            parse_geomscan(gtext, geo, geo_zar)
            # room mesh draws are the ones with no zar ("?"); track the max as the room extent
            for m in re.finditer(r"model=(\d+)\s+ext=\([\d.-]+,[\d.-]+,[\d.-]+\)\s+maxext=([\d.-]+)\s+wmin=\([^)]*\)\s+\?", gtext):
                roomext.append(float(m.group(2)))
    finally:
        if not keep:
            subprocess.run([os.path.join(REPO, "tools", "soh3d_game.sh"), "stop"],
                           cwd=REPO, env=env, capture_output=True)

    room_ref = max(roomext) if roomext else 0.0
    # Build the population of replaced objects with a known offline extent.
    rows = []
    for mid, a in auto.items():
        off = offline_cmb_maxext(a["zar"])
        # Prefer the engine's observed world AABB (joined by model id OR by zar path, since a zar can
        # load under more than one model id); fall back to scale x source extent.
        gm = geo.get(mid, {}); gz = geo_zar.get(a["zar"], {})
        rend = max(gm.get("rmaxext", 0.0), gz.get("rmaxext", 0.0), a["scale"] * (off or 0.0))
        nan = gm.get("nan", False) or gz.get("nan", False)
        rows.append({**a, "offline_ext": off, "rendered_ext": rend, "nan": nan})

    scales = [math.log10(r["scale"]) for r in rows if r["scale"] > 0]
    zmap = {}
    if scales:
        zs = robust_outliers(scales)
        for r, z in zip([r for r in rows if r["scale"] > 0], zs):
            zmap[r["model"]] = z

    for r in rows:
        z = zmap.get(r["model"], 0.0)
        flags = []
        if r["nan"]:
            flags.append("NAN")
        if abs(z) > 3.5:
            flags.append(f"SCALE-OUTLIER(z={z:+.1f})")
        if room_ref and r["rendered_ext"] > 0.6 * room_ref:
            flags.append(f"ROOM-SIZED({r['rendered_ext']:.0f} vs room {room_ref:.0f})")
        r["flags"] = flags
        r["z"] = z

    flagged = [r for r in rows if r["flags"]]
    flagged.sort(key=lambda r: (-len(r["flags"]), -abs(r["z"]), -r["rendered_ext"]))

    print(f"\n=== geom anomaly sweep: entrance {entrance} ({len(rows)} replaced objects, room ref ext {room_ref:.0f}) ===")
    if not flagged:
        print("no anomalies surfaced.")
    for r in flagged:
        off = f"{r['offline_ext']:.0f}" if r["offline_ext"] else "?"
        ratio = (r["rendered_ext"] / r["offline_ext"]) if r["offline_ext"] else float("nan")
        print(f"  ANOMALY {r['zar']} (model {r['model']})")
        print(f"    {' + '.join(r['flags'])}")
        print(f"    derived scale={r['scale']:.3f}  n64h={r['n64h']:.0f}  offline CMB ext={off}  "
              f"rendered ext={r['rendered_ext']:.0f}  (rendered/source={ratio:.1f}x)")
    # full table for context
    print("\n  all replaced objects (scale, rendered ext):")
    for r in sorted(rows, key=lambda r: -r["scale"]):
        print(f"    {r['zar']:34} scale={r['scale']:9.4f}  rendered={r['rendered_ext']:7.0f}  src={r['offline_ext']}")
    sys.exit(1 if flagged else 0)


if __name__ == "__main__":
    main()
