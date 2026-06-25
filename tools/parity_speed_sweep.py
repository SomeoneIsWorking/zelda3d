#!/usr/bin/env python3
"""parity_speed_sweep.py — SPEED-CALIBRATED Link anim-SELECTION parity sweep (SoH3D vs OoT3D oracle).

Supersedes the raw-stick comparison in parity_selection_sweep.py for INTERMEDIATE states. That tool
drove both sides at the same raw analog magnitude and dismissed `walk` as a "calibration artifact"
because the N64 control stick (~±84) and the 3DS circle pad (±100) map a given magnitude to a
DIFFERENT speed. That excuse hid a REAL divergence. The fix is to compare at matched SPEED, not
matched stick: speedXZ is the physical quantity both engines animate from, and BOTH expose it
  - SoH3D : REPL `linkanimstate` field speedXZ
  - oracle: PLAYER + 0x221c (linearVelocity, = N64 speedXZ; oot3d-decomp player_port.md)

METHOD (no fragile closed-loop): for each side, sweep the forward analog magnitude over a grid; at
each magnitude warp to open ground (Kokiri 0xEE) so the drive starts clean, hold the stick ~1s to let
speed ramp to steady state, then read (steadyspeedXZ, selectedCSAB). That yields a speed→selection
CURVE per side. We then bin both curves by speedXZ and report, per bin, the CSAB each side selects.
A bin where the two sides select different CSABs at the SAME speed is a TRUE divergence.

Both sides resolve into the same OoT3D CSAB namespace (SoH3D plays the OoT3D rig's CSABs), so the
selected names compare directly: idle `nml_waitF_typeA_20f`, walk `nml_walk_free` (animId 0x47),
run `nml_run_free` (0x6c).

USAGE
  tools/parity_speed_sweep.py                       # full sweep, print the curve + divergence table
  tools/parity_speed_sweep.py --soh-mags 0,20,40,60,80,127 --ora-mags 0,30,50,70,100
  tools/parity_speed_sweep.py --json scratch/parity/speed_sweep.json
  tools/parity_speed_sweep.py --skip-oracle        # SoH3D curve only

KNOWN RESULT (2026-06-25): SoH3D selects nml_run_free at EVERY nonzero speed (incl. speedXZ~1.0),
while the oracle selects nml_walk_free at walk speed and nml_run_free only at run speed. SoH3D reads
the live N64 player anim (which uses one run anim with speed-scaled playback) and maps it straight to
nml_run_free; OoT3D/Grezzo instead SELECTS distinct walk/run CSABs by speed. That Grezzo divergence
is the residual #117 "walk" gap — this sweep surfaces it as a real per-speed FAIL.
"""
import argparse, json, os, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import azahar_rpc as A  # noqa: E402

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
SPEEDXZ_OFF = 0x221c          # PLAYER -> linearVelocity (= N64 speedXZ)
SKELANIME_OFF = 0x254
ANIMID_OFF = 0x30
PLAYER_STATE1_IN_CUTSCENE = 0x20000000
KOKIRI_ENTRANCE = 0xEE

# speed bins (speedXZ) used to align the two curves; chosen around OoT3D's idle/walk/run regimes.
SPEED_BINS = [(0.0, 0.25, "idle"), (0.25, 2.0, "walk"), (2.0, 4.0, "jog"), (4.0, 99.0, "run")]


def speed_bin(spd):
    for lo, hi, name in SPEED_BINS:
        if lo <= spd < hi:
            return name
    return "?"


# ---------------- SoH3D side (REPL) ----------------
def soh_cmd(text):
    p = subprocess.run([os.path.join(HERE, "soh3d_repl.py"), "cmd", text],
                       capture_output=True, text=True, timeout=15)
    out = (p.stdout or "").strip().splitlines()
    return out[0] if out else ""


def soh_state():
    """(base_csab, speedXZ, st1) from linkanimstate, or (None, None, None)."""
    line = soh_cmd("linkanimstate")
    base, spd, st1 = None, None, None
    for tok in line.split():
        if tok.startswith("base="):
            base = tok[5:]
        elif tok.startswith("speedXZ="):
            try: spd = float(tok[8:])
            except ValueError: pass
        elif tok.startswith("st1=0x"):
            try: st1 = int(tok[4:], 16)
            except ValueError: pass
    if base in ("(unmapped)", "(null)"):
        base = None
    return base, spd, st1


def soh_ensure_free():
    for _ in range(2):
        for _ in range(12):
            _, _, st1 = soh_state()
            if st1 == 0:
                return True
            time.sleep(0.5)
        soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")
        time.sleep(2.5)
    return False


def soh_sample_at(mag, hold_s=1.2, hz=20.0):
    """Warp clean, drive forward at magnitude `mag` for hold_s, return (steadyspeedXZ, csab)."""
    soh_cmd(f"warp 0x{KOKIRI_ENTRANCE:x}")
    time.sleep(2.5)
    soh_ensure_free()
    soh_cmd("link 1"); soh_cmd("gcam 1")
    n = max(1, int(hold_s * hz))
    period = 1.0 / hz
    spd_last, base_last = 0.0, None
    for i in range(n):
        if i % 4 == 0:
            soh_cmd(f"walkhold 30 0 {mag}")
        base, spd, st1 = soh_state()
        if st1 == PLAYER_STATE1_IN_CUTSCENE:
            soh_ensure_free(); continue
        if spd is not None: spd_last = spd
        if base is not None: base_last = base
        time.sleep(period)
    soh_cmd("walkhold 0")
    return spd_last, base_last


def soh_curve(mags):
    out = []
    for m in mags:
        spd, csab = soh_sample_at(m)
        out.append({"mag": m, "speedXZ": round(spd, 3), "csab": csab, "bin": speed_bin(spd)})
        print(f"  [soh] mag={m:4d} speedXZ={spd:5.2f} ({speed_bin(spd):4s}) -> {csab}")
    return out


# ---------------- Oracle side (RPC) ----------------
def _load_table():
    t = os.path.join(HERE, "..", "..", "oot3d-decomp", "tools", "skeldata", "player_animid_names.json")
    return json.load(open(os.path.abspath(t)))["names"]


def oracle_warp_open():
    lc = os.path.join(HERE, "..", "..", "oot3d-decomp", "tools", "link_ctl.py")
    if os.path.exists(lc):
        subprocess.run([lc, "warp", "0xEE"], capture_output=True, text=True, timeout=20)
        time.sleep(2.0)


class Oracle:
    def __init__(self):
        self.rpc = A.Rpc()
        for pid, tid, _ in self.rpc.processes():
            if tid == OOT3D_TID:
                self.rpc.select(pid)
        self.names = _load_table()
        self._reactor()

    def _reactor(self):
        ps = self.rpc.read32(GPLAYSTATE)
        self.actor = self.rpc.read32(ps + 0x208C + 0x0C + 2 * 8 + 4) if ps else 0

    def speedXZ(self):
        return struct.unpack("<f", self.rpc.read(self.actor + SPEEDXZ_OFF, 4))[0]

    def animid(self):
        aid = self.rpc.read32(self.actor + SKELANIME_OFF + ANIMID_OFF)
        return aid, (self.names[aid] if 0 <= aid < len(self.names) else f"<id {aid}>")

    CUTSCENE_ANIMS = ("cl_dm_", "demo")  # name prefixes that mean "not free gameplay"

    def _drive(self, cx, cy, frames, hz=30.0):
        """Hold a circle-pad vector for `frames`; return (lastSpeed, lastCsabName)."""
        period = 1.0 / hz
        spd_last, name_last = 0.0, None
        for _ in range(frames):
            self.rpc.set_input(0, (cx, cy))
            try:
                spd_last = self.speedXZ(); _, name_last = self.animid()
            except Exception:
                pass
            time.sleep(period)
        return spd_last, name_last

    def find_open_dir(self, mag=100, settle=3.5):
        """Warp to open ground, then find a circle-pad ANGLE Link actually moves along (spawn facing
        varies / may point at a wall). Returns (cx, cy) unit-ish vector at `mag`, or None if Link is
        wedged in a cutscene / never moves (oracle unusable this run). This is the robustness fix for
        the post-Spirit-Temple 0xEE standup-cutscene wedge."""
        import math
        oracle_warp_open(); self._reactor()
        for deg in range(0, 360, 45):
            cx = int(mag * math.sin(math.radians(deg)))
            cy = int(mag * math.cos(math.radians(deg)))
            spd, name = self._drive(cx, cy, 30)
            if name and any(name.startswith(p) for p in self.CUTSCENE_ANIMS):
                return None  # wedged in a spawn cutscene — re-warp won't help mid-run
            if spd > 0.8:
                return (cx, cy, math.radians(deg))
        return None

    def curve(self, mags):
        d = self.find_open_dir()
        if d is None:
            print("  [ora] UNAVAILABLE: Link wedged in a cutscene or no open direction at the spawn "
                  "(0xEE standup-cutscene wedge). Re-boot the oracle or warp it to free gameplay.",
                  file=sys.stderr)
            return []
        import math
        _, _, ang = d
        out = []
        for m in mags:
            oracle_warp_open(); self._reactor()
            cx = int(m * math.sin(ang)); cy = int(m * math.cos(ang))
            spd, name = self._drive(cx, cy, int(1.2 * 30))
            out.append({"mag": m, "speedXZ": round(spd, 3), "csab": name, "bin": speed_bin(spd)})
            print(f"  [ora] mag={m:4d} (dir {math.degrees(ang):.0f}deg) speedXZ={spd:5.2f} "
                  f"({speed_bin(spd):4s}) -> {name}")
        return out


# ---------------- compare ----------------
def dominant_by_bin(curve):
    """bin -> most-common csab in that bin (across the magnitude grid)."""
    byb = {}
    for pt in curve:
        byb.setdefault(pt["bin"], {})
        byb[pt["bin"]][pt["csab"]] = byb[pt["bin"]].get(pt["csab"], 0) + 1
    return {b: max(c, key=c.get) for b, c in byb.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soh-mags", default="0,20,40,60,80,100,127")
    ap.add_argument("--ora-mags", default="0,30,45,60,80,100")
    ap.add_argument("--json", default=None)
    ap.add_argument("--skip-oracle", action="store_true")
    args = ap.parse_args()

    soh_mags = [int(x) for x in args.soh_mags.split(",")]
    ora_mags = [int(x) for x in args.ora_mags.split(",")]

    print("== SoH3D speed->selection curve ==")
    if not soh_ensure_free():
        print("WARN: SoH3D stuck in cutscene", file=sys.stderr)
    soh = soh_curve(soh_mags)

    ora = []
    if not args.skip_oracle:
        print("== Oracle speed->selection curve ==")
        ora = Oracle().curve(ora_mags)

    soh_b = dominant_by_bin(soh)
    ora_b = dominant_by_bin(ora) if ora else {}

    print(f"\n{'speed bin':<10} {'SoH3D selects':<24} {'OoT3D selects':<24} verdict")
    print("-" * 72)
    diverged = []
    for _, _, name in SPEED_BINS:
        s = soh_b.get(name); o = ora_b.get(name)
        if s is None and o is None:
            continue
        if args.skip_oracle or o is None:
            verdict = "—"
        elif s == o:
            verdict = "OK"
        else:
            verdict = "DIVERGE"; diverged.append(name)
        print(f"{name:<10} {(s or '-'):<24} {(o or '(skipped)'):<24} {verdict}")
    print(f"\n{len(diverged)} divergent speed bin(s): {', '.join(diverged) or 'none'}")

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        json.dump({"soh": soh, "oracle": ora, "soh_by_bin": soh_b, "oracle_by_bin": ora_b,
                   "diverged": diverged}, open(args.json, "w"), indent=2)
        print(f"# wrote {args.json}")
    return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
