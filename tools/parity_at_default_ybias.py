#!/usr/bin/env python3
"""parity_at_default_ybias.py -- live discriminator for the ported Camera_CalcAtDefault extra-Y block.

WHAT THIS IS A CHECK ON
    The ported producer in `Shipwright/soh/src/zelda3d/behaviors/camera/at_default.cpp`, against the
    behaviour recovered from OoT3D's FUN_00250AD0 / FUN_00338AC8 and recorded in
    `oot3d-decomp/docs/camera_calc_at_default.md`. Nothing here re-derives that recovery, and nothing
    here tunes the port: the recovered constants are inputs to a replay, and the live trace is the
    thing under test.

WHY A DISCRIMINATOR IS NEEDED AT ALL
    The RE is closed; what was missing was a live observable able to FAIL. The open uncertainty was
    threshold TIMING: the recovered decay is 400 accumulator units per authored (30 Hz) update while
    the host runs Player_Update at 20 Hz, so a 20 Hz observer sees the decay only through the 30:20
    accumulator. A measurement that samples at 20 Hz can alias that away, so "it looks right at the
    frames we happened to look at" is not evidence. `check` therefore replays the recovered rule
    frame by frame and requires EXACT agreement, and it reports the decay rate as accumulator units
    per AUTHORED UPDATE -- the one normalization under which a 20 Hz observer and a 30 Hz observer must
    agree, and the quantity that separates "400 per authored update" from "400 per host update".

WHAT WOULD MAKE THIS SAY THE PORT IS WRONG
    Any of these, all falsifiers rather than tuned thresholds:
      1. a live frame whose accumulator or active latch differs from the recovered rule's replay of
         the previous frame (wrong decay, wrong scale, wrong reset, missing branch);
      2. a latch that does not set on exactly the frames carrying rise >= 9.0 with the walk/run,
         static-floor and branch-ownership terms the producer itself recorded;
      3. a measured decay that is not 400 units per authored update (12000/s) -- for example 8000/s,
         which is what dropping the 30:20 accumulator produces;
      4. a clearing host-update index that differs from the index the recovered rule predicts for the
         rise that set the latch. Each event also reports the index a dropped-accumulator port would
         have cleared at, so the margin this trace can resolve is a measured number;
      5. (cross-engine, `compare`) a host decay rate that disagrees with the oracle's.

WHAT WOULD MAKE IT SAY NOTHING (never PASS)
    A trace with no rise at 9 units, or with a rise that never satisfies the action/floor terms, is
    INCONCLUSIVE. The predicate under test did not run, so there is no result.

THE CONTROL THAT PROVES THE CHECK CAN FAIL
    `tools/test_parity_at_default_ybias.py` runs the same `check_trace` the live command runs over
    traces built from a WRONG decay (400 units per host update, i.e. the accumulator dropped) and
    requires FAIL naming the disagreeing frame, alongside positive controls that require PASS. The
    live control -- rebuilding soh_core with the accumulator removed and re-running `host` -- is
    recorded in docs/re-frontier.md under camera.calc-at-default-ybias.

USAGE
    tools/parity_at_default_ybias.py host [--frames 60] [--json out.json]
    tools/parity_at_default_ybias.py check <trace.csv> [--hz 20]
    tools/parity_at_default_ybias.py oracle [--frames 60] [--trace out.csv]
    tools/parity_at_default_ybias.py compare <host.csv> <oracle.csv>
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

# --- recovered constants (oot3d-decomp/docs/camera_calc_at_default.md; NOT derived here) ---
DECAY_PER_AUTHORED_UPDATE = 400.0
RISE_SCALE = 100.0
MINIMUM_RISE = 9.0
BIAS_SCALE = -0.01
SLOPE_FLOOR_TYPES = (4, 7, 12)
AUTHORED_UPDATES_PER_SECOND = 30.0
HOST_UPDATES_PER_SECOND = 20.0

SCRATCH = REPO / "scratch" / "atdefault"

# Temple of Time (`warp 0x60`), the dais staircase behind the Door of Time. Measured through
# `floorgrid`: flat (ny = 1.000) treads 20 units deep with 9-11 unit risers, so a free run up it
# produces rise >= 9 updates on a non-slope, non-dynamic floor -- the only combination the recovered
# predicate admits.
STAIRS_ENTRANCE = 0x0060
STAIRS_X = 120
STAIRS_Z = 1400
STAIRS_Y = -40.0
STAIRS_YAW_DEG = 180

# Oracle player-field offsets, from oot3d-decomp/docs/camera_calc_at_default.md.
ORACLE_WORLD_POS_Y = 0x2C
ORACLE_PREV_POS_Y = 0x10C
ORACLE_WORLD_POS_X = 0x28
ORACLE_WORLD_POS_Z = 0x30
ORACLE_PREV_POS_X = 0x108
ORACLE_PREV_POS_Z = 0x110
ORACLE_YAW = 0x36
ORACLE_ACTION_FUNC = 0x1708
ORACLE_ACCUMULATOR = 0x1760
ORACLE_STATE_WORD = 0x29B8
ORACLE_ACTIVE_BIT = 0x100
ORACLE_WALK_RUN_ACTION = 0x004BA378
ORACLE_GET_ITEM_ACTION = 0x004BC22C

# One `retro_run` is 1/60 s and OoT3D's game logic ticks at 30 fps, so TWO retro_runs are one
# producer update (oot3d-decomp/docs/gameplay_firstdiv.md, "Speed 3x divergence resolved").
ORACLE_RETRO_RUNS_PER_UPDATE = 2


@dataclass
class Sample:
    """One producer update, as read back out of the ported module's own state."""

    frame: int
    active: int
    branch_owned: int
    rise: float
    accumulator: float
    walk_run: int
    get_item: int
    floor_type: int
    static_floor: int
    authored_updates: int
    y_bias: float


@dataclass
class Discrepancy:
    frame: int
    field: str
    observed: float
    expected: float

    def describe(self) -> str:
        return (
            f"frame {self.frame}: {self.field} observed {self.observed:.4f}, "
            f"recovered rule {self.expected:.4f}"
        )


@dataclass
class Verdict:
    label: str
    rows: int = 0
    observed_hz: float = HOST_UPDATES_PER_SECOND
    max_rise: float = 0.0
    qualifying_rises: int = 0
    events: list[dict] = field(default_factory=list)
    discrepancies: list[Discrepancy] = field(default_factory=list)
    skipped_reason: str = ""

    @property
    def ok(self) -> bool:
        return not self.skipped_reason and not self.discrepancies

    @property
    def status(self) -> str:
        if self.skipped_reason:
            return "INCONCLUSIVE"
        return "PASS" if self.ok else "FAIL"

    def to_dict(self) -> dict:
        return {
            "label": self.label,
            "status": self.status,
            "rows": self.rows,
            "observed_hz": self.observed_hz,
            "max_rise": self.max_rise,
            "qualifying_rises": self.qualifying_rises,
            "events": self.events,
            "discrepancies": [d.__dict__ for d in self.discrepancies],
            "skipped_reason": self.skipped_reason,
        }


def uses_extra_y_branch(floor_type: int, is_get_item_action: bool) -> bool:
    """The recovered branch predicate (FUN_00250AD0): slope floors 4/7/12 stay with the stock
    accumulator unless the current action is the get-item presentation."""
    return floor_type not in SLOPE_FLOOR_TYPES or is_get_item_action


def recovered_step(previous: Sample, sample: Sample) -> tuple[int, float]:
    """Replay one update of the recovered producer. Returns (active, accumulator) AFTER this update.

    `previous` is the preceding update's post-state as measured; `sample` supplies the inputs this
    update recorded. The 30:20 tick count is the ported module's own; an oracle trace passes
    authored_updates=1 on every row because OoT3D has one update per authored tick.
    """
    active = previous.active
    accumulator = previous.accumulator
    if not uses_extra_y_branch(sample.floor_type, sample.get_item != 0) or not sample.branch_owned:
        return active, accumulator

    if not active:
        accumulator = 0.0
    else:
        accumulator -= DECAY_PER_AUTHORED_UPDATE * sample.authored_updates
        if accumulator <= 0.0:
            accumulator = 0.0
            active = 0
    if sample.walk_run and sample.rise >= MINIMUM_RISE and sample.static_floor == 1:
        active = 1
        accumulator += sample.rise * RISE_SCALE
    return active, accumulator


def load_trace(path: Path) -> list[Sample]:
    rows: list[Sample] = []
    with path.open(newline="") as handle:
        for record in csv.DictReader(handle):
            rows.append(
                Sample(
                    frame=int(record["frame"]),
                    active=int(record["active"]),
                    branch_owned=int(record["branchOwned"]),
                    rise=float(record["rise"]),
                    accumulator=float(record["accumulator"]),
                    walk_run=int(record["walkRun"]),
                    get_item=int(record["getItem"]),
                    floor_type=int(record["floorType"]),
                    static_floor=int(record["staticFloor"]),
                    authored_updates=int(record["authoredUpdates"]),
                    y_bias=float(record["yBias"]),
                )
            )
    return rows


def write_trace(path: Path, rows: list[Sample]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "frame",
                "active",
                "branchOwned",
                "rise",
                "accumulator",
                "walkRun",
                "getItem",
                "floorType",
                "staticFloor",
                "authoredUpdates",
                "yBias",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row.frame,
                    row.active,
                    row.branch_owned,
                    f"{row.rise:.4f}",
                    f"{row.accumulator:.4f}",
                    row.walk_run,
                    row.get_item,
                    row.floor_type,
                    row.static_floor,
                    row.authored_updates,
                    f"{row.y_bias:.5f}",
                ]
            )


def qualifies(sample: Sample) -> bool:
    """True when this update satisfies the recovered rise predicate AND the extra-Y branch owns
    unk_6C4 for it. Without the branch term a slope-floor rise would be counted as an event of a
    producer that did not run."""
    return bool(
        sample.branch_owned
        and sample.walk_run
        and sample.static_floor == 1
        and sample.rise >= MINIMUM_RISE
    )


def decay_tail(rows: list[Sample], index: int) -> dict | None:
    """MEASURE the decay that follows the rise at `index` from the observed accumulator.

    Each unsaturating update removes DECAY_PER_AUTHORED_UPDATE * its tick count, so the rate is
    `removed / ticks` -- free of the 30:20 phase, and the quantity that separates "400 per authored
    update" (12000/s) from "400 per host update" (8000/s). Updates that drove the accumulator to zero
    are excluded, because the clamp and not the decay set their magnitude; a qualifying rise leaves
    at least 900 accumulator units while one host update spends at most two ticks (800), so there is
    always at least one unsaturating update and the rate is measurable from live data.

    This deliberately MEASURES the trace rather than replaying the rule: a port with the wrong decay
    has to produce a wrong number here, which a rule replay would silently agree with.

    Returns None when another qualifying rise interrupts the tail, because then the decay and the new
    rise are not separable from the accumulator alone and a rate would be meaningless.
    """
    frames = 0
    ticks = 0
    removed = 0.0
    previous = rows[index].accumulator
    for offset in range(1, len(rows) - index):
        sample = rows[index + offset]
        if qualifies(sample):
            return None
        frames += 1
        if sample.accumulator <= 0.0 or sample.active == 0:
            break
        removed += previous - sample.accumulator
        ticks += sample.authored_updates
        previous = sample.accumulator

    per_update = (removed / ticks) if ticks else 0.0
    return {
        "host_updates_to_clear": frames,
        "authored_ticks_in_tail": ticks,
        "removed_units": round(removed, 4),
        "decay_units_per_authored_update": round(per_update, 4),
        "decay_units_per_second": round(per_update * AUTHORED_UPDATES_PER_SECOND, 2),
    }


def clear_offset(rows: list[Sample], index: int, per_host_update: bool) -> int | None:
    """Host-update offset at which a latch set at `index` clears, under one decay hypothesis.

    `per_host_update=False` is the recovered rule: 400 per AUTHORED update, so the tick schedule the
    trace recorded decides how fast the accumulator drains. `per_host_update=True` is the dropped-
    accumulator port: exactly 400 per host update however many authored ticks it consumed. Reporting
    both is what makes the 20 Hz observer's discriminating power a measured number rather than an
    assertion: the gap between them is the margin this trace can actually resolve.
    """
    remaining = rows[index].accumulator
    for offset in range(1, len(rows) - index):
        sample = rows[index + offset]
        if qualifies(sample):
            return None
        ticks = 1 if per_host_update else sample.authored_updates
        remaining -= min(DECAY_PER_AUTHORED_UPDATE * ticks, remaining)
        if remaining <= 0.0:
            return offset
    return None


def check_trace(rows: list[Sample], hz: float = HOST_UPDATES_PER_SECOND, label: str = "trace") -> Verdict:
    """Replay the recovered rule over a recorded trace and report every disagreement.

    An empty or rise-free trace is INCONCLUSIVE, never PASS: a check that cannot observe the
    behaviour it claims to check has measured nothing. `hz` only labels the observation rate; the
    decay rate is normalized by authored ticks.
    """
    verdict = Verdict(label=label, rows=len(rows), observed_hz=hz)
    if not rows:
        verdict.skipped_reason = "trace is empty"
        return verdict

    verdict.max_rise = max(row.rise for row in rows)
    verdict.qualifying_rises = sum(1 for row in rows if qualifies(row))
    if verdict.max_rise < MINIMUM_RISE:
        verdict.skipped_reason = (
            f"no rise reached the recovered {MINIMUM_RISE}-unit threshold "
            f"(max {verdict.max_rise:.3f}); the predicate under test never ran"
        )
        return verdict
    if verdict.qualifying_rises == 0:
        verdict.skipped_reason = (
            f"a {verdict.max_rise:.3f}-unit rise occurred but never with walk/run on a static floor the "
            "extra-Y branch owns; the predicate's action/floor terms were never satisfied"
        )
        return verdict

    tolerance = 1e-3 * max(1.0, max(abs(row.accumulator) for row in rows))
    for index in range(1, len(rows)):
        previous, sample = rows[index - 1], rows[index]
        expected_active, expected_accumulator = recovered_step(previous, sample)
        if sample.active != expected_active:
            verdict.discrepancies.append(
                Discrepancy(sample.frame, "active", sample.active, float(expected_active))
            )
        if abs(sample.accumulator - expected_accumulator) > tolerance:
            verdict.discrepancies.append(
                Discrepancy(sample.frame, "accumulator", sample.accumulator, expected_accumulator)
            )
        expected_bias = expected_accumulator * BIAS_SCALE if expected_active else 0.0
        if abs(sample.y_bias - expected_bias) > tolerance:
            verdict.discrepancies.append(
                Discrepancy(sample.frame, "yBias", sample.y_bias, expected_bias)
            )

    for index, sample in enumerate(rows):
        if not qualifies(sample):
            continue
        tail = decay_tail(rows, index)
        if tail is None:
            continue
        authored_clear = clear_offset(rows, index, per_host_update=False)
        host_clear = clear_offset(rows, index, per_host_update=True)
        observed_clear = tail["host_updates_to_clear"]
        verdict.events.append(
            {
                "rise_frame": sample.frame,
                "rise": round(sample.rise, 4),
                "accumulator_after_rise": round(sample.accumulator, 4),
                "observed_clear_host_updates": observed_clear,
                "predicted_clear_host_updates": {
                    "recovered_400_per_authored_update": authored_clear,
                    "dropped_accumulator_400_per_host_update": host_clear,
                },
                # How many host updates (50 ms each) this event separates the recovered rule from the
                # dropped-accumulator port. 0 would mean the trace cannot tell them apart.
                "discrimination_margin_updates": (
                    abs(host_clear - authored_clear)
                    if authored_clear is not None and host_clear is not None
                    else None
                ),
                **tail,
            }
        )
    return verdict


# ---------------------------------------------------------------------------
# host driver
# ---------------------------------------------------------------------------
def _repl(command: str, timeout: float = 20.0) -> str:
    from zelda3d_repl import send

    return send(command, timeout=timeout)


def drive_host(
    frames: int,
    trace_path: Path,
    x: int = STAIRS_X,
    z: int = STAIRS_Z,
    yaw: int = STAIRS_YAW_DEG,
) -> Verdict:
    """Put Link at `x, z` facing `yaw` degrees, run him forward under `freeze`, and record one
    producer sample per 20 Hz host update."""
    import time

    _repl("walkhold 0")
    _repl("freeze 0")
    _repl(f"warp 0x{STAIRS_ENTRANCE:x}")
    time.sleep(5.0)
    _repl("freeze 1")
    _repl("gcam 1")
    _repl(f"tpf {x} {z} {yaw}")
    _repl("step 20")
    _repl(f"walkhold {frames + 30} 0 127")
    reply = _repl(f"atdefault trace {frames} {trace_path.resolve()}", timeout=300.0)
    _repl("walkhold 0")
    if "->" not in reply:
        raise RuntimeError(f"atdefault trace did not run: {reply!r}")
    return check_trace(load_trace(trace_path), label=f"host@{trace_path.name}")


# ---------------------------------------------------------------------------
# oracle driver
# ---------------------------------------------------------------------------
def _u32(harness, address: int) -> int:
    reply = harness.send(f"r32 0x{address:08x}")
    if not reply.startswith("ok"):
        raise RuntimeError(f"oracle r32 0x{address:08x} failed: {reply}")
    return int(reply.split()[-1], 0)


def _f32(harness, address: int) -> float:
    return struct.unpack("<f", struct.pack("<I", _u32(harness, address)))[0]


def _write_f32(harness, address: int, value: float) -> None:
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    reply = harness.send(f"w32 0x{address:08x} 0x{bits:08x}")
    if not reply.startswith("ok"):
        raise RuntimeError(f"oracle w32 0x{address:08x} failed: {reply}")


def _oracle_player(harness) -> int:
    reply = harness.send("az_playerinfo")
    match = re.search(r"addr=(0x[0-9a-fA-F]+)", reply)
    if not match:
        raise RuntimeError(f"az_playerinfo did not resolve a Player address: {reply!r}")
    return int(match.group(1), 16)


def trace_oracle(frames: int, trace_path: Path) -> list[Sample]:
    """Record the oracle's producer, one row per authored 30 Hz update, on the same staircase.

    Every field is read from the recovered addresses, so nothing here re-derives the producer: the
    oracle's own latch word (`+0x29B8 & 0x100`), its own accumulator (`+0x1760`), and its own
    world.pos.y / prevPos.y pair that FUN_00250AD0 differenced.
    """
    import time

    os.environ.setdefault("ZELDA3D_HARNESS_NO_CAPTURE", "1")
    from harness_gameplay import boot_to_gameplay
    from harness_process import spawn

    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance=STAIRS_ENTRANCE, settle_frames=180):
            raise RuntimeError(
                "the oracle never reached gameplay: scratch/gameplay_settled.<marker>.state is absent "
                "(docs/issues/0023), so the OoT3D producer cannot be observed at all"
            )
        player = _oracle_player(harness)
        for offset, value in (
            (ORACLE_WORLD_POS_X, float(STAIRS_X)),
            (ORACLE_WORLD_POS_Y, STAIRS_Y),
            (ORACLE_WORLD_POS_Z, float(STAIRS_Z)),
            (ORACLE_PREV_POS_X, float(STAIRS_X)),
            (ORACLE_PREV_POS_Y, STAIRS_Y),
            (ORACLE_PREV_POS_Z, float(STAIRS_Z)),
        ):
            _write_f32(harness, player + offset, value)
        harness.send(f"w16 0x{player + ORACLE_YAW:08x} 0x8000")
        harness.send("analog 0 -32000")
        harness.send("run 20", per_line_timeout=300.0)
        time.sleep(0.2)

        rows: list[Sample] = []
        for frame in range(frames):
            harness.send(f"run {ORACLE_RETRO_RUNS_PER_UPDATE}", per_line_timeout=300.0)
            action = _u32(harness, player + ORACLE_ACTION_FUNC)
            accumulator = _f32(harness, player + ORACLE_ACCUMULATOR)
            active = 1 if _u32(harness, player + ORACLE_STATE_WORD) & ORACLE_ACTIVE_BIT else 0
            rows.append(
                Sample(
                    frame=frame,
                    active=active,
                    branch_owned=1,
                    rise=_f32(harness, player + ORACLE_WORLD_POS_Y)
                    - _f32(harness, player + ORACLE_PREV_POS_Y),
                    accumulator=accumulator,
                    walk_run=1 if action == ORACLE_WALK_RUN_ACTION else 0,
                    get_item=1 if action == ORACLE_GET_ITEM_ACTION else 0,
                    floor_type=0,
                    static_floor=1,
                    authored_updates=1,
                    y_bias=accumulator * BIAS_SCALE if active else 0.0,
                )
            )
        harness.send("analog 0 0")
        write_trace(trace_path, rows)
        return rows
    finally:
        try:
            harness.quit()
        finally:
            try:
                harness.proc.kill()
            except Exception:  # noqa: BLE001 - the harness may already be gone
                pass


def compare(host_rows: list[Sample], oracle_rows: list[Sample]) -> dict:
    """The cross-engine statement: the decay in accumulator units per AUTHORED UPDATE must agree,
    even though the two engines run the producer at 20 Hz and 30 Hz and see different rises."""
    host = check_trace(host_rows, hz=HOST_UPDATES_PER_SECOND, label="host")
    oracle = check_trace(oracle_rows, hz=AUTHORED_UPDATES_PER_SECOND, label="oracle")
    expected = DECAY_PER_AUTHORED_UPDATE * AUTHORED_UPDATES_PER_SECOND
    host_rates = [event["decay_units_per_second"] for event in host.events]
    oracle_rates = [event["decay_units_per_second"] for event in oracle.events]
    return {
        "expected_units_per_second": expected,
        "host_units_per_second": host_rates,
        "oracle_units_per_second": oracle_rates,
        "host_status": host.status,
        "oracle_status": oracle.status,
        "agrees": bool(host_rates)
        and bool(oracle_rates)
        and all(abs(rate - expected) < 1.0 for rate in host_rates + oracle_rates),
        "host_events": host.events,
        "oracle_events": oracle.events,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    host = sub.add_parser("host", help="drive the live host up the staircase and check the trace")
    host.add_argument("--frames", type=int, default=60)
    host.add_argument("--x", type=int, default=STAIRS_X)
    host.add_argument("--z", type=int, default=STAIRS_Z)
    host.add_argument("--yaw", type=int, default=STAIRS_YAW_DEG)
    host.add_argument("--trace", default=str(SCRATCH / "host_stairs.csv"))
    host.add_argument("--json", default=None)

    check = sub.add_parser("check", help="evaluate a recorded trace against the recovered rule")
    check.add_argument("trace")
    check.add_argument("--hz", type=float, default=HOST_UPDATES_PER_SECOND)
    check.add_argument("--json", default=None)

    oracle = sub.add_parser("oracle", help="record the oracle producer up the same staircase")
    oracle.add_argument("--frames", type=int, default=60)
    oracle.add_argument("--trace", default=str(SCRATCH / "oracle_stairs.csv"))

    both = sub.add_parser("compare", help="compare a host trace and an oracle trace")
    both.add_argument("host_trace")
    both.add_argument("oracle_trace")

    args = parser.parse_args(argv)

    if args.cmd == "host":
        SCRATCH.mkdir(parents=True, exist_ok=True)
        trace = Path(args.trace)
        verdict = drive_host(args.frames, trace, x=args.x, z=args.z, yaw=args.yaw)
        print(json.dumps(verdict.to_dict(), indent=2))
        if args.json:
            Path(args.json).write_text(json.dumps(verdict.to_dict(), indent=2) + "\n")
        return 0 if verdict.ok else 1

    if args.cmd == "check":
        verdict = check_trace(load_trace(Path(args.trace)), hz=args.hz, label=Path(args.trace).name)
        print(json.dumps(verdict.to_dict(), indent=2))
        if args.json:
            Path(args.json).write_text(json.dumps(verdict.to_dict(), indent=2) + "\n")
        return 0 if verdict.ok else 1

    if args.cmd == "oracle":
        SCRATCH.mkdir(parents=True, exist_ok=True)
        rows = trace_oracle(args.frames, Path(args.trace))
        verdict = check_trace(rows, hz=AUTHORED_UPDATES_PER_SECOND, label="oracle")
        print(json.dumps(verdict.to_dict(), indent=2))
        return 0 if verdict.ok else 1

    result = compare(load_trace(Path(args.host_trace)), load_trace(Path(args.oracle_trace)))
    print(json.dumps(result, indent=2))
    return 0 if result["agrees"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))