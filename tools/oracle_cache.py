"""oracle_cache.py — persistent caches for deterministic OoT3D oracle output.

Two independent cache namespaces share the scratch/oracle_cache/ root
(gitignored — never commit ROM-derived cache contents):

  scratch/oracle_cache/warp/<ent>_<dayTime>.json
      The ORIGINAL warp-probe cache (this module's `warp()`/`invalidate()`
      functions, used by tools/market_scene_probe.py): memoizes
      a harness warp to (<ent>, <dayTime>) —
      scene/head/pos/rot — since a warp costs ~3-5s of oracle time and is
      deterministic for a given (entrance, dayTime).

  scratch/oracle_cache/<savestate_sha16>_<rom_sha16>_<patch_marker>_<texpack_marker>/
      The FRAME/PROBE cache (harness_cache.OracleCache, driven from this
      file's CLI): memoizes embedded-Azahar (soh3d_harness) title-cutscene
      frame captures and structured probe output by az (Azahar) frame
      number, keyed additionally by the loaded savestate, the ROM, the effective
      texture-pack manifest, and the Azahar rendering patches in
      tools/soh3d_harness/AZAHAR_PATCH.md — see
      harness_cache.cache_key(). Used by tools/title_ab.py's `ab` command and
      any future probe built on OracleCache. Managed via this file's CLI:

          tools/oracle_cache.py stats                  # entries, size, active key
          tools/oracle_cache.py warm 100 200 360 ...    # batch-capture frames
          tools/oracle_cache.py warm                    # warm the standard sweep
          tools/oracle_cache.py adopt-frame <key> 2010 --observer-only
          tools/oracle_cache.py import-artifact <key> <name> <source> --args-json '{}'
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
import sys
from pathlib import Path

from harness_cache import OracleCache
from harness_gameplay import GSAVECONTEXT_DAYTIME_VA, boot_to_gameplay
from harness_paths import CACHE_ROOT, TITLE_STATE
from harness_process import spawn

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = os.path.join(REPO, "oot3d-decomp")
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


def _probe_oracle(entrance, day_time, timeout):
    """Warp a fresh harness oracle to (entrance, dayTime) and read the Player back.

    Returns the same {scene, head, pos, rot, raw} shape the cache has always stored.
    """
    ent = entrance if isinstance(entrance, int) else int(str(entrance), 0)
    dt = day_time if isinstance(day_time, int) else int(str(day_time), 0)

    h = spawn(save_state=str(TITLE_STATE))
    try:
        if not boot_to_gameplay(h, entrance=ent):
            return None
        if dt:
            h.send(f"w16 0x{GSAVECONTEXT_DAYTIME_VA:08x} 0x{dt:04x}")
            h.send("run 30")
        scene = (h.send("scene") or "").strip()
        pos_r = (h.send("az_playerpos") or "").strip()
        anim_r = (h.send("az_linkanim") or "").strip()
    finally:
        h.close()

    m_s = re.search(r"ok 0x([0-9a-fA-F]+)", scene)
    m_p = re.search(r"pos=\(([-\d.,]+)\)\s+worldRy=(-?\d+)", pos_r)
    m_h = re.search(r"addr=0x([0-9a-fA-F]+)", anim_r)
    if not (m_s and m_p):
        return None
    return {
        "scene": int(m_s.group(1), 16),
        "head": m_h.group(1) if m_h else None,
        "pos": tuple(float(x) for x in m_p.group(1).split(",")),
        "rot": (0, int(m_p.group(2)), 0),
        "raw": f"{scene} | {pos_r}",
    }


def warp(entrance, day_time, refresh=False, timeout=300):
    """Warp the oracle to (entrance, dayTime). Returns {scene, head, pos, rot, raw}.

    Cache hit skips the oracle entirely; a miss boots a harness oracle and warps.
    Callers that need the oracle left in a specific memory state should not use this
    (the harness process is closed once the probe is read).
    """
    path = _cache_path(entrance, day_time)
    if not refresh and os.path.exists(path):
        with open(path, "r") as f:
            data = json.load(f)
        data["_cache_hit"] = True
        return data

    parsed = _probe_oracle(entrance, day_time, timeout)
    if parsed is None:
        return {"scene": None, "head": None, "pos": None, "rot": None,
                "raw": "oracle probe failed", "_cache_hit": False, "_error": True}
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
# Frame/probe cache CLI — thin wrapper over harness_cache.OracleCache.
# ---------------------------------------------------------------------------

# The standard title-frame sweep points used across title_ab.py A/B and
# calibration sessions — pre-warming these covers the common case.
DEFAULT_SWEEP = [100, 200, 360, 500, 700, 764, 1000, 1300, 1522, 1700, 1900]
WARN_BYTES = 2 * 1024 ** 3  # 2 GB


def _savestate():
    return TITLE_STATE


def _step_chunked(h, n, chunk=100):
    remaining = n
    while remaining > 0:
        k = min(chunk, remaining)
        h.send(f"run {k}")
        remaining -= k


def cmd_stats(args) -> None:
    cache = OracleCache(_savestate())
    s = cache.stats()
    print(f"active key:  {s['key']}")
    print(f"dir:         {s['dir']}")
    print(f"frames:      {s['n_frames']}")
    print(f"probes:      {s['n_probes']}")
    print(f"artifacts:   {s['n_artifacts']}")
    print(f"size:        {s['bytes'] / 1e6:.1f} MB")
    if s["bytes"] > WARN_BYTES:
        print(f"WARNING: cache exceeds {WARN_BYTES / 1e9:.1f} GB — consider "
              f"`invalidate` for stale key contexts.", file=sys.stderr)

    if CACHE_ROOT.exists():
        contexts = sorted(d for d in CACHE_ROOT.iterdir() if d.is_dir() and d.name != "warp")
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
        sys.exit(f"missing {savestate} — a current-contract title save-state is required")

    cache = OracleCache(savestate)
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

    h = spawn(save_state=str(savestate))
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
    cache = OracleCache(_savestate())
    s = cache.stats()
    print(f"[oracle_cache] invalidating key={cache.key} "
          f"({s['n_frames']} frames, {s['n_probes']} probes, {s['bytes'] / 1e6:.1f} MB) "
          f"at {cache.dir}")
    cache.invalidate()
    print("[oracle_cache] done")


def cmd_adopt_frame(args) -> None:
    if not args.observer_only:
        raise SystemExit(
            "refusing frame adoption without --observer-only; use it only when the Azahar patch "
            "difference cannot change rendered pixels"
        )
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.source_key):
        raise SystemExit("invalid source cache key")
    source_context = CACHE_ROOT / args.source_key
    cache = OracleCache(_savestate())
    if source_context.resolve().parent != CACHE_ROOT.resolve():
        raise SystemExit("source cache key escapes the cache root")
    for frame in sorted(set(args.frames)):
        try:
            destination = cache.adopt_frame(source_context, frame)
        except (FileNotFoundError, KeyError, ValueError) as error:
            raise SystemExit(f"cannot adopt az={frame}: {error}") from error
        print(
            f"[oracle_cache] adopted az={frame} from key={args.source_key} "
            f"into key={cache.key}: {destination}"
        )


def cmd_import_artifact(args) -> None:
    """Attach an existing raw capture to the exact historical context that produced it."""
    try:
        artifact_args = json.loads(args.args_json)
    except json.JSONDecodeError as error:
        raise SystemExit(f"invalid --args-json: {error}") from error
    if not isinstance(artifact_args, dict):
        raise SystemExit("--args-json must decode to an object")
    try:
        cache = OracleCache.open_existing_context(args.context_key)
    except (FileNotFoundError, ValueError) as error:
        raise SystemExit(f"cannot open context {args.context_key!r}: {error}") from error

    source = Path(args.source)
    if not source.is_file():
        raise SystemExit(f"artifact source is not a file: {source}")
    cached = cache.get_artifact(args.artifact_name, artifact_args)
    if cached is not None:
        if cached.read_bytes() != source.read_bytes():
            raise SystemExit(
                "refusing to overwrite an existing artifact with different bytes: "
                f"{cached}"
            )
        print(f"[oracle_cache] artifact already cached: {cached}")
        return
    destination = cache.put_artifact(args.artifact_name, artifact_args, source)
    print(f"[oracle_cache] imported artifact into key={cache.key}: {destination}")


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

    adopt = sub.add_parser(
        "adopt-frame",
        help="reuse frames after an explicitly observer-only Azahar patch change",
    )
    adopt.add_argument("source_key", help="existing cache context key")
    adopt.add_argument("frames", type=int, nargs="+", help="az frame numbers to adopt")
    adopt.add_argument(
        "--observer-only",
        action="store_true",
        help="assert that the patch difference cannot alter rendered pixels",
    )
    adopt.set_defaults(func=cmd_adopt_frame)

    artifact = sub.add_parser(
        "import-artifact",
        help="copy an existing raw artifact into its exact historical cache context",
    )
    artifact.add_argument("context_key", help="existing cache key that produced the artifact")
    artifact.add_argument("artifact_name", help="stable logical artifact name")
    artifact.add_argument("source", help="existing source file to preserve in that context")
    artifact.add_argument(
        "--args-json",
        default="{}",
        help="JSON object identifying the capture variant (default: {})",
    )
    artifact.set_defaults(func=cmd_import_artifact)

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
