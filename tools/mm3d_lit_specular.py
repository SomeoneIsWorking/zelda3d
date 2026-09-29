#!/usr/bin/env python3
"""Capture MM3D's per-slot PICA light specular on a fragment-lit draw.

**The input this is after.** The fragment-lighting reduction (`tools/pica_lighting_registers.py`)
established that the captured configuration needs zero optional shading terms, so PICA's
FRAGMENT_SECONDARY is a FLAT sum of `light.specular_0 + light.specular_1` with no `N·H` term, and
for MM3D's opening slots that sum is (1.082, 0.894, 0.780) -- it clamps to near-white, where the
host was adding black.

Acting on that needs the per-slot LIGHT specular, and it is the one thing still missing:

* the MATERIAL side is recovered and now parsed (`+0xAC` / `+0xB0`,
  `Shipwright/cmb3d/asset/cmb_color_block.cpp`);
* the LIGHT side's producer is `FUN_004093f8` (48 bytes) -> `FUN_0040d1a8`, which is **not** in the
  decompiled set.

So this measures the value directly on real hardware instead of decompiling the producer, and --
the point of the tool -- **tests whether the value already appears in the recovered MM3D scene
lighting table.** If it does, the producer is the environment record and the port needs no new
reverse engineering at all. If it does not, the table is not the source and that is worth knowing
before anyone builds on it.

Why MM3D and not OoT3D: MM3D's opening is fragment-lit on 75-84% of its draws, and
`tools/mm3d_oracle_state.py` reproduces the state from the ROM. OoT3D's title has **0 of 69**
fragment-lit draws (measured by `tools/lit_pica_capture.py`), so it cannot supply this fixture at
any state reachable without a save.

Controls, because "the number looks like a light colour" is not evidence:
* the capture is taken only on a draw whose AUTHORITATIVE `regs.lighting.disable` says lighting is
  on, and every rejection is reported rather than silently skipped;
* the tool prints the reduced-term list for the captured config, so a capture from a configuration
  that actually needs a LUT is visibly not being read through the reduced form;
* the table search is over BOTH games' tables and reports what it scanned and matched, so a miss
  is a measurement and not an absence of looking.

Usage:
    source .env && tools/mm3d_lit_specular.py
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_transport import Harness  # noqa: E402

sys.path.insert(0, str(REPO / "tools"))
from pica_lighting_registers import (  # noqa: E402
    load_register_map,
    predict_fragment_lighting,
    reduce_lighting,
)

STATE = REPO / "scratch" / "harness" / "save" / "mm3d" / "lost_woods.state"
OUT = REPO / "scratch" / "mm3d_lit_specular"

# The provisioned MM3D image, the harness binary and the state path all live where their owner
# puts them, and are re-exported rather than re-typed. This module mistyped the ROM directory once
# already (`oracle_roms` for `oracle-roms`) and then the binary path, and BOTH failures read as
# "MM3D is unavailable" -- a silent negative of exactly the kind this project keeps paying for.
# One owner, imported.
from mm3d_oracle_state import BINARY, ORACLE_ROMS  # noqa: E402

WARM_RUNS = 2
PROBE_RUNS = 4
SETTLE_RUNS = 60

DRAW = re.compile(r"^draw ", re.M)


def f16(raw: int) -> float:
    """IEEE 754 binary16 -> float. `LightSrc`'s direction is float16 (regs_lighting.h:142-148)."""
    sign = -1.0 if raw >> 15 else 1.0
    exponent = (raw >> 10) & 0x1F
    fraction = raw & 0x3FF
    if exponent == 0:
        return sign * (fraction / 1024.0) * 2.0**-14
    if exponent == 31:
        return sign * (float("inf") if fraction == 0 else float("nan"))
    return sign * (1.0 + fraction / 1024.0) * 2.0 ** (exponent - 15)


def oracle_rom() -> Path | None:
    if not ORACLE_ROMS.is_dir():
        return None
    return next((p for p in sorted(ORACLE_ROMS.glob("*.3ds")) if p.is_file()), None)


def boot(rom: Path) -> Harness:
    """Boot the harness on MM3D's provisioned image, exactly as its state owner does.

    This mirrors `tools/mm3d_oracle_state.py:boot`. It did NOT at first: this module called
    `harness_process.spawn`, whose first parameter is a savestate path and not a command line, so
    the ROM path was passed to `loadstate` and reported as `reason=unreadable` -- a third failure
    in one file with the same root cause, which is not re-deriving the harness contract from the
    owner that already encodes it.
    """
    os.environ.setdefault("ZELDA3D_HEADLESS", "1")
    os.environ.setdefault("ZELDA3D_HARNESS_RES_FACTOR", "1")
    os.environ.setdefault("ZELDA3D_HARNESS_TEXPACK", "off")
    environment = dict(os.environ)
    from harness_headless_display import prepare_headless_display

    prepare_headless_display(REPO, environment)
    OUT.mkdir(parents=True, exist_ok=True)
    environment["HARNESS_STDERR"] = str(OUT / "stderr.log")
    return Harness([BINARY, str(rom)], environment)


def parse_draws(log_text: str) -> list[dict[str, str]]:
    """Parse `draw n=<id> ... picaLit=<0|1>` lines.

    The id token is `n=`, not `draw=`; matching the wrong one yields zero draws and reads as
    "nothing is fragment-lit" when the log is in fact full of them.
    """
    draws: dict[int, dict[str, str]] = {}
    for line in log_text.splitlines():
        if not line.startswith("draw "):
            continue
        found = re.search(r"\bn=(\d+)", line)
        if not found:
            continue
        record = {"id": found.group(1)}
        for key in ("picaLit", "fLit", "vLit", "hasCol"):
            match = re.search(rf"\b{key}=(\S+)", line)
            if match:
                record[key] = match.group(1)
        draws[int(record["id"])] = record
    return [draws[k] for k in sorted(draws)]


def require(record: dict, key: str) -> str:
    """Read a capture field, refusing to default.

    A missing key defaulted to `"0"` here on the first run, because the capture spells the
    fields `specular0`/`specular1` and this read `specular_0`/`specular_1`. Every colour then
    came back (0, 0, 0) and the tool reported it -- which reads as "MM3D has no specular", the
    exact conclusion this tool exists to check. A zero that means "I looked in the wrong place"
    is indistinguishable from a zero that means "the game submitted zero", so the key's absence
    is an error here rather than a default.
    """
    if key not in record:
        raise KeyError(
            f"capture record has no {key!r}; available keys: {sorted(record)}")
    return record[key]


def colour_to_unit(raw: str) -> tuple[int, int, int] | None:
    """`LightColor` is 10 bits per channel packed little-endian, scaled by 1/255 (regs_lighting.h:88)."""
    try:
        value = int(raw, 16)
    except ValueError:
        return None
    return (value & 0x3FF, (value >> 10) & 0x3FF, (value >> 20) & 0x3FF)


def search_tables(values: dict[str, tuple[int, int, int]]) -> None:
    """Does the captured specular appear in a recovered scene-lighting table?

    A hit would mean the producer is the environment record and no new RE is needed. A miss is
    equally informative, so the scanned extent is printed either way.
    """
    tables = [
        REPO / "Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc",
        REPO / "2ship/2s2h/zelda3d/mm3d_scene_lighting.inc",
    ]
    triples = {v for v in values.values() if v != (0, 0, 0)}
    for table in tables:
        if not table.is_file():
            print(f"    {table.name}: ABSENT")
            continue
        text = table.read_text(errors="replace")
        # A table entry is a parenthesised integer triple; compare in the same 0..255-ish domain
        # the capture reports, tolerating the /255 scaling used in the .inc.
        rows = re.findall(r"\(?\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)?", text)
        scaled = {(round(int(r) / 255.0 * 255), round(int(g) / 255.0 * 255), round(int(b) / 255.0 * 255))
                  for r, g, b in rows}
        hits = sorted(triples & (scaled | {s for s in scaled}))
        print(f"    {table.name}: scanned {len(rows)} triples, matched {len(hits)}"
              + (f" -> {hits}" if hits else ""))


def main() -> int:
    rom = oracle_rom()
    if rom is None:
        print(f"no provisioned MM3D oracle ROM under {ORACLE_ROMS}", file=sys.stderr)
        return 2
    if not STATE.is_file():
        print(f"no state at {STATE}; run `tools/mm3d_oracle_state.py drive` first", file=sys.stderr)
        return 2

    harness = boot(rom)
    try:
        reply = harness.send(f"loadstate {STATE}", per_line_timeout=900.0)
        if not reply.strip().endswith("ok"):
            print(f"loadstate failed: {reply.strip()}", file=sys.stderr)
            return 1
        harness.send(f"run {WARM_RUNS * PROBE_RUNS}", per_line_timeout=3600.0)

        # Discover a fragment-lit draw on the AUTHORITATIVE register.
        log = OUT / "draws.log"
        log.unlink(missing_ok=True)
        harness.send(f"vsuni_log {log}", per_line_timeout=60.0)
        harness.send(f"run {PROBE_RUNS}", per_line_timeout=3600.0)
        harness.send("vsuni_log off", per_line_timeout=60.0)

        draws = parse_draws(log.read_text(errors="replace")) if log.is_file() else []
        lit = [d for d in draws if d.get("picaLit") == "1"]
        print(f"[mm3d-lit-specular] draws discovered: {len(draws)} | picaLit=1: {len(lit)}")
        if not draws:
            print("  no draws parsed -- the log format is not what this expects", file=sys.stderr)
            return 1
        if not lit:
            print("  no fragment-lit draw; this state cannot supply the fixture", file=sys.stderr)
            return 1

        # Capture the highest-id lit draw, then let it be submitted.
        target = lit[-1]
        path = OUT / "lighting.json"
        path.unlink(missing_ok=True)
        reply = harness.send(f"lighting_capture {target['id']} {path}", per_line_timeout=120.0)
        print(f"[mm3d-lit-specular] armed on draw n={target['id']}: {reply.strip()}")
        harness.send(f"run {SETTLE_RUNS}", per_line_timeout=3600.0)
        harness.send("lighting_capture off", per_line_timeout=60.0)

        if not path.is_file():
            print("  capture produced no file: the armed draw was never submitted", file=sys.stderr)
            return 1
        data = json.loads(path.read_text())
        print(json.dumps(data, indent=2)[:4000])

        config0 = int(str(require(data, "config0")), 16)
        config1 = int(str(require(data, "config1")), 16)
        reduction = reduce_lighting(config0, config1, load_register_map())
        print(f"\n[reduced form] {reduction.summary()}")

        # Capture a spread of lit draws, not one. The first live capture showed specular0
        # bit-identical to diffuse on both of a single draw's slots, which is the kind of
        # coincidence a two-sample reading cannot distinguish from a rule. Every arming and every
        # miss is reported, so a draw that stops being submitted shows up as a miss rather than
        # quietly shrinking the denominator.
        spread = int(os.environ.get("MM3D_LIT_DRAWS", "8"))
        agree = disagree = missing = 0
        for order, record in enumerate(reversed(lit[-spread:])):
            draw_id = record["id"]
            path = OUT / f"lighting_{draw_id}.json"
            path.unlink(missing_ok=True)
            reply = harness.send(f"lighting_capture {draw_id} {path}", per_line_timeout=120.0)
            harness.send(f"run 8", per_line_timeout=3600.0)
            harness.send("lighting_capture off", per_line_timeout=60.0)
            if not path.is_file():
                missing += 1
                print(f"  draw n={draw_id}: capture MISS (armed but never submitted)")
                continue
            cap = json.loads(path.read_text())
            for slot in require(cap, "lights"):
                spec0 = colour_to_unit(require(slot, "specular0")) or (0, 0, 0)
                diff = colour_to_unit(require(slot, "diffuse")) or (0, 0, 0)
                if spec0 == (0, 0, 0) and diff == (0, 0, 0):
                    continue
                if spec0 == diff:
                    agree += 1
                else:
                    disagree += 1
                    print(f"  draw n={draw_id} slot {slot.get('index')}: specular0={spec0} != diffuse={diff}")
            print(f"  draw n={draw_id}: captured (config0={cap.get('config0')} config1={cap.get('config1')})")

        observed_total = agree + disagree
        print(f"\n[control] specular0 == diffuse on {agree} of {observed_total} non-zero slots"
              + (f" ({agree / observed_total:.1%})" if observed_total else "")
              + f", {disagree} disagreeing, {missing} capture(s) missed")
        if observed_total == 0:
            print("  NO non-zero slot observed: the agreement claim is UNTESTED, not confirmed")
        slots = require(data, "lights")
        if not slots:
            print("  capture carries no light records", file=sys.stderr)
            return 1
        observed: dict[str, tuple[int, int, int]] = {}
        port_slots = []
        active_slots: list[int] = []
        for slot in slots:
            index = slot.get("index", "?")
            diffuse = colour_to_unit(require(slot, "diffuse")) or (0, 0, 0)
            spec0 = colour_to_unit(require(slot, "specular0")) or (0, 0, 0)
            spec1 = colour_to_unit(require(slot, "specular1")) or (0, 0, 0)
            ambient = colour_to_unit(require(slot, "ambient")) or (0, 0, 0)
            xy_raw = require(slot, "xy")
            z_raw = require(slot, "z")
            print(f"  slot {index}: diffuse={diffuse} specular0={spec0} specular1={spec1} ambient={ambient}")
            observed[f"slot{index}.specular0"] = spec0
            observed[f"slot{index}.specular1"] = spec1
            if spec0 != (0, 0, 0) or spec1 != (0, 0, 0):
                active_slots.append(index)
            x = f16(int(xy_raw, 16) & 0xFFFF)
            y = f16((int(xy_raw, 16) >> 16) & 0xFFFF)
            z = f16(int(z_raw, 16) & 0xFFFF)
            port_slots.append({
                "diffuse": tuple(c / 255.0 for c in diffuse),
                "specular_0": tuple(c / 255.0 for c in spec0),
                "specular_1": tuple(c / 255.0 for c in spec1),
                "ambient": tuple(c / 255.0 for c in ambient),
                "position": (x, y, z),
            })

        for normal in ((1, 0, 0), (-1, 0, 0), (0, 0, 1)):
            out = predict_fragment_lighting(port_slots, normal=normal)
            print(f"  reduced N={normal}: primary={tuple(round(v,4) for v in out['primary'])} "
                  f"secondary={tuple(round(v,4) for v in out['secondary'])}")

        print("\n[is the light specular already in a recovered table?]")
        search_tables(observed)
        print(f"\n[summary] disable={data.get('disable')} "
              f"max_light_index={data.get('max_light_index')} "
              f"light_enable={data.get('light_enable')} "
              f"slot_mapping={data.get('slot_mapping')}")
        print(f"[summary] slots with a non-zero specular: {active_slots or 'none'}")
        return 0
    finally:
        harness.close()


if __name__ == "__main__":
    raise SystemExit(main())
