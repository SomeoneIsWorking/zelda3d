"""oracle_cache.py — persistent caches for deterministic OoT3D oracle output.

Two independent cache namespaces share the scratch/oracle_cache/ root
(gitignored — never commit ROM-derived cache contents):

  scratch/oracle_cache/warp/<ent>_<dayTime>.json
      The ORIGINAL warp-probe cache (this module's `warp()`/`invalidate()`
      functions, used by tools/market_scene_probe.py): memoizes
      `oot3d-decomp/tools/link_ctl.py warp <ent> <dayTime>` results —
      scene/head/pos/rot — since a warp costs ~3-5s of oracle time and is
      deterministic for a given (entrance, dayTime).

  scratch/oracle_cache/<savestate_sha16>_<rom_sha16>_<patch_marker>/
      The FRAME/PROBE cache (harness_ctl.OracleCache, driven from this
      file's CLI): memoizes embedded-Azahar (soh3d_harness) title-cutscene
      frame captures and structured probe output by az (Azahar) frame
      number, keyed additionally by the loaded savestate, the ROM, and the
      Azahar rendering patches in tools/soh3d_harness/AZAHAR_PATCH.md — see
      harness_ctl.cache_key(). Used by tools/title_ab.py's `ab` command and
      any future probe built on OracleCache. Managed via this file's CLI:

          tools/oracle_cache.py stats                  # entries, size, active key
          tools/oracle_cache.py warm 100 200 360 ...    # batch-capture frames
          tools/oracle_cache.py warm                    # warm the standard sweep
          tools/oracle_cache.py invalidate              # clear the CURRENT key

      See docs/parity-workflow.md "Oracle data cache" for the design writeup.

Invalidate the warp cache:
- delete the specific cache file, OR
- pass `refresh=True` to `warp()` to force a re-probe, OR
- call `invalidate_warp_cache()` / delete scratch/oracle_cache/warp/ to clear everything.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = os.path.expanduser("<oot3d-decomp>")
CACHE_DIR = os.path.join(REPO, "scratch/oracle_cache/warp")

sys.path.insert(0, os.path.join(REPO, "tools"))


# ---------------------------------------------------------------------------
# Warp-probe cache (original; scratch/oracle_cache/warp/) — unchanged API,
# consumed by tools/market_scene_probe.py.
# ---------------------------------------------------------------------------

def _cache_path(entrance, day_time):
    ent = entrance if isinstance(entrance, str) else f"0x{entrance:X}"
    dt = day_time if isinstance(day_time, str) else f"0x{day_time:04X}"
    ent_norm = ent.lower().replace("0x", "")
    dt_norm = dt.lower().replace("0x", "")
    os.makedirs(CACHE_DIR, exist_ok=True)
    return os.path.join(CACHE_DIR, f"{ent_norm}_{dt_norm}.json")


def _parse_link_ctl(stdout):
    # link_ctl.py final line: "scene=32 head=098f4010 pos=(-4.0,0.0,715.9) rot=(0,-32767,0)"
    m = re.search(
        r"scene=(\d+)\s+head=([0-9a-fA-F]+)\s+pos=\(([-\d.,]+)\)\s+rot=\(([-\d,]+)\)",
        stdout,
    )
    if not m:
        return None
    pos = tuple(float(x) for x in m.group(3).split(","))
    rot = tuple(int(x) for x in m.group(4).split(","))
    return {
        "scene": int(m.group(1)),
        "head": m.group(2),
        "pos": pos,
        "rot": rot,
        "raw": stdout.strip().splitlines()[-1],
    }


def warp(entrance, day_time, refresh=False, timeout=30):
    """Warp the oracle to (entrance, dayTime). Returns a dict {scene, head, pos, rot, raw}.

    Cache hit skips the oracle entirely; miss drives link_ctl.py and stores the result.
    Callers that need the oracle in a specific memory state (post-warp for further RAM
    probes) should pass refresh=True — a cache hit does NOT touch Azahar.
    """
    path = _cache_path(entrance, day_time)
    if not refresh and os.path.exists(path):
        with open(path, "r") as f:
            data = json.load(f)
        data["_cache_hit"] = True
        return data

    ent = entrance if isinstance(entrance, str) else f"0x{entrance:X}"
    dt = day_time if isinstance(day_time, str) else f"0x{day_time:04X}"
    r = subprocess.run(
        ["python3", "tools/link_ctl.py", "warp", ent, dt],
        cwd=DECOMP, capture_output=True, text=True, timeout=timeout,
    )
    parsed = _parse_link_ctl(r.stdout)
    if parsed is None:
        # Do NOT cache a failed probe.
        return {"scene": None, "head": None, "pos": None, "rot": None,
                "raw": r.stdout.strip(), "_cache_hit": False, "_error": True}
    with open(path, "w") as f:
        json.dump(parsed, f, indent=2, sort_keys=True)
    parsed["_cache_hit"] = False
    return parsed


def invalidate_warp_cache(entrance=None, day_time=None):
    """Delete cached warp-probe entries. All args None = clear the whole warp cache."""
    if entrance is None and day_time is None:
        if os.path.isdir(CACHE_DIR):
            for f in os.listdir(CACHE_DIR):
                os.unlink(os.path.join(CACHE_DIR, f))
        return
    if entrance is not None and day_time is not None:
        path = _cache_path(entrance, day_time)
        if os.path.exists(path):
            os.unlink(path)
        return
    raise ValueError("pass both entrance+day_time or neither")


# Back-compat alias (old name).
invalidate = invalidate_warp_cache


# ---------------------------------------------------------------------------
# Frame/probe cache CLI (new) — thin wrapper over harness_ctl.OracleCache.
# ---------------------------------------------------------------------------

# The standard title-frame sweep points used across title_ab.py A/B and
# calibration sessions — pre-warming these covers the common case.
DEFAULT_SWEEP = [100, 200, 360, 500, 700, 764, 1000, 1300, 1522, 1700, 1900]
WARN_BYTES = 2 * 1024 ** 3  # 2 GB


def _hc():
    import harness_ctl
    return harness_ctl


def _savestate():
    return Path(REPO) / "scratch" / "title_settled.state"


def _step_chunked(h, n, chunk=100):
    remaining = n
    while remaining > 0:
        k = min(chunk, remaining)
        h.send(f"run {k}")
        remaining -= k


def cmd_stats(args) -> None:
    hc = _hc()
    cache = hc.OracleCache(_savestate())
    s = cache.stats()
    print(f"active key:  {s['key']}")
    print(f"dir:         {s['dir']}")
    print(f"frames:      {s['n_frames']}")
    print(f"probes:      {s['n_probes']}")
    print(f"size:        {s['bytes'] / 1e6:.1f} MB")
    if s["bytes"] > WARN_BYTES:
        print(f"WARNING: cache exceeds {WARN_BYTES / 1e9:.1f} GB — consider "
              f"`invalidate` for stale key contexts.", file=sys.stderr)

    if hc.CACHE_ROOT.exists():
        contexts = sorted(d for d in hc.CACHE_ROOT.iterdir() if d.is_dir() and d.name != "warp")
        if contexts:
            print(f"\nall frame/probe cache-key contexts on disk ({len(contexts)}):")
            for d in contexts:
                marker = "  <== current key" if d.name == cache.key else ""
                size = sum(f.stat().st_size for f in d.rglob("*") if f.is_file())
                print(f"  {d.name}  ({size / 1e6:.1f} MB){marker}")


def cmd_warm(args) -> None:
    if not os.environ.get("ZELDA3D_OOT3D_ROM"):
        sys.exit("ZELDA3D_OOT3D_ROM not set — run `source .env` first")
    savestate = _savestate()
    if not savestate.exists():
        sys.exit(f"missing {savestate} — a title_settled.state save-state is required")

    hc = _hc()
    cache = hc.OracleCache(savestate)
    frames = sorted(set(args.frames)) if args.frames else list(DEFAULT_SWEEP)
    missing = [f for f in frames if cache.get_frame(f) is None]
    if not missing:
        print(f"[oracle_cache] all {len(frames)} requested frame(s) already cached "
              f"(key={cache.key})")
        return

    print(f"[oracle_cache] warming {len(missing)}/{len(frames)} frame(s) "
          f"(key={cache.key}): {missing}")
    log_dir = Path(REPO) / "scratch" / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", str(log_dir / "oracle_cache_harness.log"))

    h = hc.spawn(save_state=str(savestate))
    tmp = Path(REPO) / "scratch" / "oracle_cache" / "_warm_tmp"
    tmp.parent.mkdir(parents=True, exist_ok=True)
    try:
        cur = 0
        for target in missing:
            step = target - cur
            if step > 0:
                _step_chunked(h, step)
            cur = target
            h.send_multiline(f"snapshot {tmp}")
            captured = cache.put_frame(target, str(tmp) + ".az.ppm")
            print(f"[oracle_cache]   az={target} -> {captured}")
    finally:
        h.quit()

    s = cache.stats()
    print(f"[oracle_cache] warm complete: {s['n_frames']} frame(s) cached, "
          f"{s['bytes'] / 1e6:.1f} MB total (key={cache.key})")


def cmd_invalidate(args) -> None:
    hc = _hc()
    cache = hc.OracleCache(_savestate())
    s = cache.stats()
    print(f"[oracle_cache] invalidating key={cache.key} "
          f"({s['n_frames']} frames, {s['n_probes']} probes, {s['bytes'] / 1e6:.1f} MB) "
          f"at {cache.dir}")
    cache.invalidate()
    print("[oracle_cache] done")


def cmd_warp_debug(args) -> None:
    result = warp(args.entrance, args.day_time, refresh=args.refresh)
    print(json.dumps(result, indent=2, sort_keys=True))


def main(argv) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    st = sub.add_parser("stats", help="print frame/probe cache entries+size for the current key context")
    st.set_defaults(func=cmd_stats)

    wm = sub.add_parser("warm", help="batch-capture az frames into the cache in one harness session")
    wm.add_argument("frames", type=int, nargs="*",
                     help="az frame numbers to warm (default: the standard sweep points)")
    wm.set_defaults(func=cmd_warm)

    inv = sub.add_parser("invalidate", help="delete all frame/probe cache entries for the CURRENT key context")
    inv.set_defaults(func=cmd_invalidate)

    wp = sub.add_parser("warp", help="debug: warp-probe cache lookup (see warp()/market_scene_probe.py)")
    wp.add_argument("entrance")
    wp.add_argument("day_time")
    wp.add_argument("--refresh", action="store_true")
    wp.set_defaults(func=cmd_warp_debug)

    args = p.parse_args(argv)
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
