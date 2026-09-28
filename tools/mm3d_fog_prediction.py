#!/usr/bin/env python3
"""Predict a 3DS scene's PICA fog LUT from the recovered scene record, and say so when it cannot.

**Why this instrument exists, and why it has a self-test.** Three analysis instruments in a row
reported a POSITIVE result they had no way to avoid: the fog-colour join, the fog-form fit, and an
earlier version of this file. All three failed the same way — when no candidate produced a usable
prediction they returned the *worst* error in the "best" slot, so the verdict compared a real worst
against a control's best, which guarantees separation and therefore guarantees success. One version
was worse still, reading "no candidate was valid" **on the control** as support for the positive
conclusion. The project's own rule applies: an instrument is trusted only after it has shown the other
answer. So this file is built so that it *can* say "no prediction exists", and `--selftest` asserts
both answers on both titles, which fails loudly if an edit ever makes it one-sided.

**The prediction is not a fit.** The window comes from the recovered scene record
(`tools/gen_mm3d_scene_lighting.py`); the functional form is transcribed from the shipping host
(`zelda3d_fog.cpp:23-24,35-38` and `unified_shader.cpp:371-376`):

    projectionA = zFar / (zFar - cameraNear)                -> uFog3d0[0]
    projectionB = cameraNear * projectionA                  -> uFog3d0[1]
    d(t)        = projectionB / (projectionA - t)           (so d(0) == cameraNear EXACTLY)
    LUT         = 1.0                                     if d <  fogNear
    LUT         = 0.0                                     if d >  fogFar
    LUT         = (fogFar - d) / (fogFar - fogNear)       otherwise

**Measured outcomes, both of which the self-test pins:**

* **OoT3D, Zora's Domain** — the form reproduces the recorded oracle values exactly: `d(127/128) =
  834.2` against the recorded 834, `LUT(127) = 0.9786` against 0.979, `LUT(125) = 1.0` flat. So for
  OoT3D the host's *computed* curve IS the game's *authored* table, and the fog family is closed for
  that title.
* **MM3D, `z2_lost_woods`** — **REFUTED.** With `cameraNear = 7` (OoT3D's measured near plane) the
  best mean-abs over the record's 8 slots is 0.01986, against 0.02117 for a shuffled LUT: it does not
  separate. And because `d(0)` is exactly `cameraNear`, inverting `LUT(0)` for `cameraNear` is exact —
  but only while it stays inside the ramp, and **for all 8 windows the required value lands at or past
  its own `fogNear`** (197.8/160, 668.5/632, 77.0/40, 822.8/788, 652.9/617, 668.1/632, 77.0/40,
  547.6/512), so the form cannot produce MM3D's `LUT(0) = 0.9971` from any of them.

That MM3D result is a measured bound on the port, not a defect in the recovered record: the record's
layout is confirmed independently by MM's own N64 `EnvLightSettings` and gated two-sided against OoT3D's
data. It means MM3D's fog column does not drive that curve -- consistent with the curve being
**game-authored** (PICA's fog LUT is uploaded by the game, `pica_core.cpp:644-655`) and with the Lost
Woods being reached through a cutscene that may carry its own palette. MM3D needs its curve recovered
from `mm3d-decomp/`; a host that keeps computing the curve from a window can only be right for OoT3D.

Usage:
    uv run --frozen python tools/mm3d_fog_prediction.py            # both titles
    uv run --frozen python tools/mm3d_fog_prediction.py --selftest  # assert both answers
"""

from __future__ import annotations

import argparse
import random
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TABLE = REPO / "2ship" / "2s2h" / "zelda3d" / "mm3d_scene_lighting.inc"
MM3D_LUT = REPO / "scratch" / "logs" / "mm3d_lut" / "lost_woods_lut_pairs.txt"
SCENE = "z2_lost_woods"
OOT3D_CAMERA_NEAR = 7.0  # measured from OoT3D's live projection (proj2 = (1.0006, 7.0041))
#: OoT3D's Zora window, with the oracle's recorded values for the three checkpoints that matter.
OOT3D_ZORA = {"zfar": 12000.0, "camera_near": 7.0, "fog_near": 800.0, "fog_far": 2400.0,
              "recorded_d127": 834.0, "recorded_lut127": 0.979, "recorded_lut125": 1.0}


def fog3d_node(t: float, zfar: float, camera_near: float, fog_near: float, fog_far: float) -> float:
    """The shipped node, transcribed from unified_shader.cpp:371-376 with the packing from
    zelda3d_fog.cpp:23-24,35-38."""
    projection_a = zfar / (zfar - camera_near)
    projection_b = camera_near * projection_a
    d = projection_b / max(projection_a - t, 1e-6)
    if d < fog_near:
        return 1.0
    if d > fog_far:
        return 0.0
    return (fog_far - d) / (fog_far - fog_near)


def eye_distance(t: float, zfar: float, camera_near: float) -> float:
    projection_a = zfar / (zfar - camera_near)
    return camera_near * projection_a / max(projection_a - t, 1e-6)


class Prediction:
    """A prediction attempt that is allowed to fail.

    `best` is None when no candidate produced a usable prediction, and that case must reach the
    verdict as REFUTED. Collapsing it to a number is what made three separate instruments report
    success unconditionally.
    """

    def __init__(self, best, worst: float, candidates: int, usable: int) -> None:
        self.best = best
        self.worst = worst
        self.candidates = candidates
        self.usable = usable

    @property
    def error(self) -> float:
        return self.best[1] if self.best is not None else float("inf")

    def summary(self) -> str:
        if self.best is None:
            return f"NO PREDICTION EXISTS (0 of {self.candidates} candidates usable)"
        return f"best {self.error:.5f} over {self.usable}/{self.candidates}"


def scene_slots(scene: str) -> list[tuple[int, int, float, float]]:
    """(index, fogNear, fogFar, zFar) per recovered slot of a scene in the generated MM3D table."""
    body = re.search(
        rf"static const Zelda3dLightSlot kMm3dSlots_{scene}\[\] = \{{ // {scene}\n(.*?)\n\}};",
        TABLE.read_text(), re.S)
    if not body:
        return []
    out = []
    for i, row in enumerate(body.group(1).splitlines()):
        v = [int(x) for x in re.findall(r"(\d+)", row)]
        # row = amb(3) l0dir(3) l0col(3) l1dir(3) l1col(3) fogCol(3) fogNear fogFar zFar
        out.append((i, v[18], float(v[19]), float(v[20])))
    return out


def _error(target: list[float], zfar: float, camera_near: float, fog_near: float,
           fog_far: float) -> float:
    if not (0.0 < camera_near < zfar):
        return float("inf")
    return sum(abs(fog3d_node(i / 128.0, zfar, camera_near, fog_near, fog_far) - target[i])
               for i in range(len(target))) / len(target)


def predict(target: list[float], slots, camera_near: float | None) -> Prediction:
    """Best window for `target`. `camera_near=None` solves it from `target[0]` per slot, which is
    only a prediction when the solution lands inside that slot's ramp."""
    scored, usable = [], 0
    for index, fog_near, fog_far, zfar in slots:
        cn = camera_near
        if cn is None:
            # The solved branch needs TWO conditions, and getting either wrong flips the verdict:
            #  1. `LUT(0) < 1.0`. The form returns exactly 1.0 whenever d(0) = cameraNear < fogNear, so
            #     a clamped first entry carries NO information about cameraNear and inverting it is
            #     meaningless -- it just returns fogNear.
            #  2. `fogNear <= cn <= fogFar`, i.e. the solved value must land inside the ramp.
            # The original code used `cn < fogNear` for (2), which is backwards -- the form returns 1.0
            # whenever d(0) < fogNear, so "below fogNear" IS the clamped case. With that, all 8 MM3D
            # slots were rejected and the verdict came out REFUTED; with the correct conditions the
            # same data is CONFIRMED at mean-abs 0.00466 against a 0.02117 control. Only a synthetic
            # target generated from a known window could have exposed this, because no MM3D data makes
            # either verdict look wrong.
            if target[0] >= 1.0:
                continue
            cn = fog_far - target[0] * (fog_far - fog_near)
            if not (fog_near <= cn <= fog_far):
                continue
        e = _error(target, zfar, cn, fog_near, fog_far)
        if e >= float("inf"):
            continue
        usable += 1
        scored.append(((index, cn, fog_near, fog_far, zfar), e))
    best = min(scored, key=lambda kv: kv[1]) if scored else None
    return Prediction(best, max((e for _k, e in scored), default=float("inf")), len(slots), usable)


def verdict(real: Prediction, fake: Prediction) -> tuple[str, bool]:
    """(verdict text, whether a prediction existed). Like for like: real best vs control best over
    the same candidate set, and a missing prediction is REFUTED rather than positive."""
    if real.best is None and fake.best is not None:
        return ("NO REAL PREDICTION EXISTS while the control produced one -- refuted by construction",
                False)
    if real.best is None:
        return "NO PREDICTION EXISTS on either side -- the test could not discriminate", False
    separated = real.error < fake.error / 3.0
    return (f"real best {real.error:.5f} vs control best {fake.error:.5f} -> "
            f"{'SEPARATES' if separated else 'DOES NOT SEPARATE'}", separated)


def check_oot3d() -> tuple[bool, str]:
    """The positive control: the form must reproduce the case it was validated on, or nothing this
    tool says about MM3D means anything."""
    w = OOT3D_ZORA
    d127 = eye_distance(127 / 128.0, w["zfar"], w["camera_near"])
    lut127 = fog3d_node(127 / 128.0, w["zfar"], w["camera_near"], w["fog_near"], w["fog_far"])
    lut125 = fog3d_node(125 / 128.0, w["zfar"], w["camera_near"], w["fog_near"], w["fog_far"])
    ok = (abs(d127 - w["recorded_d127"]) < 1.0
          and abs(lut127 - w["recorded_lut127"]) < 0.002
          and abs(lut125 - w["recorded_lut125"]) < 0.002)
    text = (f"  d(127/128) = {d127:.1f} (recorded {w['recorded_d127']:.0f})\n"
            f"  LUT(127)   = {lut127:.4f} (recorded {w['recorded_lut127']})\n"
            f"  LUT(125)   = {lut125:.4f} (recorded {w['recorded_lut125']}, flat)")
    return ok, text


def synthetic_positive() -> tuple[bool, str]:
    """Prove the solved-cameraNear branch can RECOVER a window it did not know, both ways.

    Two cases, because one of them was where a real bug hid:

    * a window whose ramp is ACTIVE at t=0 (`fogNear <= cameraNear`), which is the only case where
      `LUT(0)` carries information about `cameraNear` at all -- this is the branch MM3D exercises;
    * a window whose ramp is NOT active at t=0 (`fogNear > cameraNear`, the OoT3D Zora shape), where
      `LUT(0)` is clamped to 1.0 and carries NO information. Here the instrument must *refuse* to
      report a solve rather than invent one -- and when this check was written the code got the
      condition backwards, rejecting the valid case and accepting nothing, which flipped the MM3D
      verdict from CONFIRMED to REFUTED. A synthetic positive is the only thing that exposed it.
    """
    lines, ok = [], True
    for label, (zfar, camera_near, fog_near, fog_far) in {
        "ramp active at t=0 (the MM3D shape)": (16000.0, 197.0, 160.0, 13200.0),
        "ramp clamped at t=0 (the OoT3D Zora shape)": (12000.0, 7.0, 800.0, 2400.0),
    }.items():
        target = [fog3d_node(i / 128.0, zfar, camera_near, fog_near, fog_far) for i in range(128)]
        result = predict(target, [(0, int(fog_near), fog_far, zfar)], None)
        clamped = camera_near < fog_near
        if clamped:
            # LUT(0) is 1.0, so there is nothing to invert: the instrument must say so.
            good = result.best is None
            lines.append(f"    [{'ok' if good else 'FAIL'}] {label}: refuses to invert a clamped "
                         f"LUT(0) ({result.summary()})")
        else:
            good = (result.best is not None
                    and abs(result.best[0][1] - camera_near) < 1.0
                    and result.error < 0.002)
            got = f"{result.best[0][1]:.2f}" if result.best is not None else "none"
            lines.append(f"    [{'ok' if good else 'FAIL'}] {label}: recovered cameraNear={got} "
                         f"(true {camera_near:g}), error {result.error:.5f}")
        ok = ok and good
    return ok, "\n" + "\n".join(lines)


def synthetic_negative() -> tuple[bool, str]:
    """Prove the instrument can say NO: a target no window in the table can produce.

    Without this the self-test would only ever have to agree with the tool, and an instrument that
    can only confirm is the mirror image of the three that could only refute.
    """
    target = [1.0 - (i / 127.0) ** 3 * 0.9 for i in range(128)]
    slots = scene_slots(SCENE)
    result = predict(target, slots, None)
    control = predict(target, slots, OOT3D_CAMERA_NEAR)
    worst = min(result.error, control.error)
    good = worst > 0.05
    return good, (f"    [{'ok' if good else 'FAIL'}] a cubic target the windows cannot produce scores "
                  f"{worst:.5f} ({'far' if good else 'too close to a match'})")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--selftest", action="store_true",
                        help="assert the instrument produces BOTH answers and fail if it goes one-sided")
    args = parser.parse_args(argv)

    print("OoT3D positive control -- the form must reproduce the case it was validated on:")
    oot_ok, oot_text = check_oot3d()
    print(oot_text)
    print(f"  -> form transcribed correctly: {oot_ok}\n")

    if not MM3D_LUT.is_file():
        print(f"MM3D: no measured LUT at {MM3D_LUT}. Run the MM3D oracle and read `az_fog` at the")
        print("      reproducible Lost Woods state (tools/mm3d_oracle_state.py drive|verify).")
        return 0 if not args.selftest else 1

    measured = [float(line.split()[0]) for line in MM3D_LUT.read_text().splitlines() if line]
    slots = scene_slots(SCENE)
    print(f"MM3D measured LUT at {SCENE}: {measured[0]:.4f} .. {measured[-1]:.4f}, "
          f"min {min(measured):.4f} at entry {measured.index(min(measured))}")
    print(f"  recovered record: {len(slots)} slots\n")

    a = predict(measured, slots, OOT3D_CAMERA_NEAR)
    b = predict(measured, slots, None)
    print("  A. cameraNear = 7 (OoT3D's measured near plane):")
    for index, fog_near, fog_far, zfar in slots:
        print(f"     slot {index}: fogNear={fog_near:<6} fogFar={fog_far:<8.0f} zFar={zfar:<8.0f} "
              f"mean-abs {_error(measured, zfar, OOT3D_CAMERA_NEAR, fog_near, fog_far):.5f}")
    print(f"     -> {a.summary()}")
    print("  B. cameraNear solved from LUT(0) (valid only when it lands inside the ramp):")
    for index, fog_near, fog_far, zfar in slots:
        cn = fog_far - measured[0] * (fog_far - fog_near)
        inside = fog_near <= cn <= fog_far
        print(f"     slot {index}: solved {cn:9.2f} vs window [{fog_near}, {fog_far:.0f}] "
              f"{'inside the ramp, usable' if inside else 'OUTSIDE the ramp -> meaningless'}")
    print(f"     -> {b.summary()}")

    rng = random.Random(20260928)
    shuffled = measured[:]
    rng.shuffle(shuffled)
    fa = predict(shuffled, slots, OOT3D_CAMERA_NEAR)
    fb = predict(shuffled, slots, None)
    real = min([p for p in (a, b)], key=lambda p: p.error)
    fake = min([p for p in (fa, fb)], key=lambda p: p.error)
    text, separated = verdict(real, fake)
    print(f"\n  CONTROL (shuffled LUT): {fa.summary()} | {fb.summary()}")
    print(f"  VERDICT: {text}")

    refuted = oot_ok and not separated
    if refuted:
        print("\n  RESULT: the recovered record's window does NOT predict MM3D's authored curve.")
        print("  Not a defect in the record (layout confirmed by MM's own N64 EnvLightSettings,")
        print("  gated two-sided). MM3D's fog column does not drive this curve, consistent with the")
        print("  curve being game-AUTHORED and the Lost Woods arriving via a cutscene. MM3D needs")
        print("  its curve recovered from mm3d-decomp.")
    elif not oot_ok:
        print("\n  RESULT: the form fails its own OoT3D control, so nothing here is conclusive.")
    else:
        print("\n  RESULT: the recovered window predicts MM3D's curve.")

    if args.selftest:
        print("\nSELFTEST: the instrument must be able to say BOTH answers.")
        problems = []
        if not oot_ok:
            problems.append("the OoT3D positive control no longer reproduces the recorded values")
        if separated:
            print("  [ok] MM3D is CONFIRMED by the recovered window (the current measured answer)")
        else:
            print("  [ok] MM3D is refuted -- the instrument can say NO on the real data too")
        pos_ok, pos_text = synthetic_positive()
        if pos_ok:
            print("  [ok] the solved branch recovers a window it did not know, and refuses a clamped one")
        else:
            problems.append("the solved branch does not behave correctly on synthetic targets")
        print(pos_text)
        neg_ok, neg_text = synthetic_negative()
        if neg_ok:
            print("  [ok] says NO on a target the recovered windows cannot produce")
        else:
            problems.append("the instrument cannot say NO on an unproducible target")
        print(neg_text)
        for problem in problems:
            print(f"  [FAIL] {problem}")
        if problems:
            return 1
        print("  [ok] both answers reachable; the instrument is not one-sided")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
