"""oracle_cache.py — memoize deterministic OoT3D oracle probes.

The oracle (Azahar OoT3D) is deterministic for a given (entrance, dayTime): warping there
produces the same scene / spawn coords every time until we manually change save state or
inputs. Probes like tools/market_scene_probe.py hit the oracle for the same (ent, dayTime)
key across sessions and re-runs, and each warp costs ~3-5 seconds of oracle time.

This module wraps `oot3d-decomp/tools/link_ctl.py warp <ent> <dayTime>` with a JSON cache
at `scratch/oracle_cache/warp/<ent>_<dayTime>.json`. Callers get the cached result
instantly on hit; on miss it drives the oracle and stores the result.

Invalidate:
- delete the specific cache file, OR
- pass `refresh=True` to force a re-probe, OR
- delete scratch/oracle_cache/warp/ to clear everything.

The cache is per-repo (scratch/ is gitignored). Do NOT commit the cache directory.
"""
import json, os, re, subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = os.path.expanduser("~/repo/oot3d-decomp")
CACHE_DIR = os.path.join(REPO, "scratch/oracle_cache/warp")


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


def invalidate(entrance=None, day_time=None):
    """Delete cached entries. All args None = clear the whole warp cache."""
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


if __name__ == "__main__":
    # Debug: `python3 tools/oracle_cache.py 0xB1 0x8001` — print cached-or-fetched result.
    import sys
    ent = sys.argv[1] if len(sys.argv) > 1 else "0xB1"
    dt = sys.argv[2] if len(sys.argv) > 2 else "0x8001"
    refresh = "--refresh" in sys.argv
    print(json.dumps(warp(ent, dt, refresh=refresh), indent=2, sort_keys=True))
