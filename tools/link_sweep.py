#!/usr/bin/env python3
"""link_sweep.py — the Link (on-foot, 3DS-body, ZELDA3D_LINK=1) state-matrix parity SWEEP.

ORCHESTRATOR ONLY. Does not reimplement any per-dimension drive/verdict logic that already
exists — it composes:
  - parity_state_sweep.py  (discrete forced-state CSAB SELECTION vs the OoT3D decomp ground
                             truth: jump/swim/damage/shield/attack/climb/carry/idle)
  - parity_speed_sweep.py  (the walk/run locomotion CONTINUUM by speedXZ; SoH-side curve +
                             classify()/windows_overlap() reused verbatim for the oracle side)
  - REPL primitives (zelda3d_repl.py: link/linksrc/linkanimstate/linkstate/warp/...)
  - the EMBEDDED-Azahar oracle harness (harness_ctl.py + tools/soh3d_harness), NOT the
    external Qt+RPC `azahar_rpc.py` oracle that parity_state_sweep/parity_speed_sweep default
    to — that external frontend needs Qt6, which is not installed on this machine (see
    OracleSession docstring below). `az_linkanim` (added to tools/soh3d_harness/main.cpp this
    session) reads the SAME PLAYER+0x254+0x30 animId documented in oracle_link_animid.py, so
    both oracle transports name selections identically (player_animid_names.json).

State matrix: every dimension the brief calls out, extended ONLY as far as an existing input
recipe can actually drive it headlessly. A state with no such recipe is recorded UNREACHABLE
with a concrete reason — never guessed/faked into a verdict (see docs/link_parity_checklist.md
header for the full honesty contract).

CLI:
  tools/link_sweep.py sweep [--skip-oracle] [--json OUT]   # run everything, write the checklist
  tools/link_sweep.py show <state>
  tools/link_sweep.py list [--status divergent|match|unreachable]
  tools/link_sweep.py resolve <state> --commit <hash>       # mark a checklist row resolved

Results persist to scratch/link_sweep/<timestamp>.json (raw, diffable) and
scratch/link_sweep/latest.json (symlink-equivalent: plain copy, always the last sweep) — the
checklist doc (docs/link_parity_checklist.md) is REGENERATED from latest.json, never hand-edited.
"""
import argparse, json, os, subprocess, sys, time
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import parity_state_sweep as PSS   # noqa: E402  (forcestate/idle/carry drive + verdict)
import parity_speed_sweep as SPD   # noqa: E402  (locomotion continuum: soh_curve/classify)
import harness_ctl as HC           # noqa: E402  (embedded-Azahar oracle transport)

SCRATCH = os.path.join(REPO, "scratch", "link_sweep")
CHECKLIST_MD = os.path.join(REPO, "docs", "link_parity_checklist.md")
NAMES_TABLE = os.path.join(REPO, "..", "oot3d-decomp", "tools", "skeldata",
                           "player_animid_names.json")
NAMES_TABLE = os.path.abspath(NAMES_TABLE)
SAVE_STATE = os.path.join(REPO, "scratch", "title_settled.state")

KOKIRI = 0xEE


# ---------------------------------------------------------------------------
# Oracle transport: EMBEDDED Azahar harness (soh3d_harness), not azahar_rpc.
# ---------------------------------------------------------------------------
class OracleSession:
    """One embedded-Azahar (soh3d_harness) process, booted to free gameplay at Kokiri Forest,
    kept alive for the DURATION of a sweep so every oracle-side probe amortizes the ~4-5min
    cold-boot cost (title_settled.state -> START/A taps -> gPlayState populated -> warp 0xEE)
    over ONE session instead of paying it per state.

    Why this transport and not tools/azahar_rpc.py (which parity_state_sweep.py /
    parity_speed_sweep.py / oracle_link_pose.py / oracle_link_animid.py all default to): that
    RPC oracle requires a STANDALONE Azahar frontend binary (`Azahar/build/bin/Release/azahar`)
    which this Azahar checkout can only build with ENABLE_QT=ON (there is no SDL2-only
    executable target left in this fork — citra_cli/citra_meta are themselves gated behind
    ENABLE_QT). Qt6 is NOT installed on this machine (no qmake6, no Qt6 pkg-config modules) —
    a genuine, concrete infra blocker, not a workaround-able one from this session. The
    embedded harness (`Azahar/build-libretro`, target `soh3d_harness`, driven by
    `tools/harness_ctl.py`) was ALREADY built (2026-07-15, for the title-cs work) and embeds
    the same Azahar/OoT3D core — so it is a legitimate, already-blessed (see CLAUDE.md
    "Direction: build a direct harness that EMBEDS Azahar as a library") oracle transport. This
    session extended it with one new REPL command, `az_linkanim` (tools/soh3d_harness/main.cpp),
    reading the identical PLAYER+0x254+0x30 offset oracle_link_animid.py documents, so animId
    naming (player_animid_names.json) is shared and comparable across BOTH oracle transports.
    """

    def __init__(self):
        self.names = json.load(open(NAMES_TABLE))["names"]
        self.h = None
        self.ok = False
        self.fail_reason = None

    def boot(self):
        try:
            self.h = HC.spawn(save_state=SAVE_STATE)
            self.h.send("run 300")
            schedule = ([HC.BTN_START] * 3) + ([HC.BTN_A] * 40)
            populated = False
            for btn in schedule:
                HC.tap(self.h, btn, hold=30, release=60)
                if HC.poll_playstate(self.h):
                    populated = True
                    break
            if not populated:
                self.fail_reason = "gPlayState never populated from title_settled.state taps"
                return False
            r = self.h.send(f"warp 0x{KOKIRI:x}")
            if not r.startswith("ok"):
                self.fail_reason = f"warp 0x{KOKIRI:x} failed: {r}"
                return False
            for _ in range(4):
                self.h.send("run 60")
            self.ok = True
            return True
        except Exception as e:
            self.fail_reason = f"oracle boot exception: {e}"
            return False

    def resettle(self):
        """Re-warp to open Kokiri ground with neutral input (between probes)."""
        self.h.send("analog 0 0")
        self.h.send(f"warp 0x{KOKIRI:x}")
        for _ in range(3):
            self.h.send("run 60")

    def sample(self):
        """One (animId, name, speedXZ) reading."""
        r = self.h.send("az_linkanim")
        if not r.startswith("ok "):
            return None, None, None
        d = {}
        for tok in r.split()[1:]:
            if "=" in tok:
                k, v = tok.split("=", 1)
                d[k] = v
        animId = int(d.get("animId", -1))
        speedXZ = float(d.get("speedXZ", 0.0))
        name = self.names[animId] if 0 <= animId < len(self.names) else f"<id {animId}>"
        return animId, name, speedXZ

    def idle_name(self):
        self.resettle()
        self.h.send("analog 0 0")
        self.h.send("run 40")
        _, name, _ = self.sample()
        return name

    # Calibrated forward analog-Y deflections (libretro s16, forward = NEGATIVE Y). The embedded
    # harness feeds `analog <lx> <ly>` straight to libretro InputState in the full s16 range
    # [-32768,32767] (tools/soh3d_harness/main.cpp), which Azahar's libretro core maps to the 3DS
    # circle pad through a DEADZONE + nonlinear curve. Empirically probed 2026-07-15
    # (scratch/oracle_walkmag_probe.py, Kokiri 0xEE): deflections shallower than ~-16000 stay in
    # the circle-pad deadzone (Link idle, speedXZ 0); -24000 -> nml_walk_free (speedXZ ~1.3);
    # -32000 -> nml_run_free (speedXZ ~4.2). So a curve MUST use near-full deflections to separate
    # walk from run — circle-pad-style 0..100 magnitudes (the external azahar_rpc oracle's unit)
    # are ~0.3% deflection here and never move Link. These four points straddle idle/walk/run.
    FORWARD_DEFLECTIONS = [0, -24000, -28000, -32000]

    def curve(self, deflections=None, hold_frames=45):
        """Speed->CSAB curve at each calibrated forward analog-Y deflection (s16, forward=neg).
        Returns parity_speed_sweep's Oracle.curve() shape ({"mag","speedXZ","csab"}) so
        SPD.classify()/SPD.windows_overlap() consume it unchanged."""
        out = []
        for d in (deflections if deflections is not None else self.FORWARD_DEFLECTIONS):
            self.resettle()
            self.h.send(f"analog 0 {int(d)}")
            self.h.send(f"run {hold_frames}")
            _, name, spd = self.sample()
            out.append({"mag": d, "speedXZ": round(abs(spd or 0.0), 3), "csab": name})
        self.h.send("analog 0 0")
        return out

    def close(self):
        if self.h:
            try:
                self.h.quit()
            except Exception:
                pass
            try:
                self.h.proc.wait(timeout=5)
            except Exception:
                try:
                    self.h.proc.kill()
                except Exception:
                    pass


# ---------------------------------------------------------------------------
# State matrix
# ---------------------------------------------------------------------------
# group: render | locomotion | action
# kind:
#   model        — is the 3DS Link body actually drawing on-foot (data check via REPL, not PNG)
#   idle_oracle  — idle standing anim vs a LIVE oracle reading (gt=oracle)
#   speed        — locomotion continuum (walk/run) vs a LIVE oracle speed->CSAB curve
#   forcestate   — PSS "forcestate" kind, decomp ground truth (gt=decomp; equipment-less oracle
#                  save can't reach these live — see parity_state_sweep.py docstring)
#   carry        — PSS "carry" kind, decomp ground truth
#   observe      — driven via the SAME forcestate mechanism but with NO verified decomp
#                  ground-truth CSAB family yet; records what SoH selects for follow-up RE
#                  instead of fabricating a PASS/FAIL against a guessed `expect`
#   ztarget_move — sidestep_l/sidestep_r/turn_in_place (2026-07-15 RE): native N64 Z-targeting
#                  gates these on `Player_focusActor != NULL` (z_player.c func_8083FC68/FD78,
#                  confirmed identically gated in OoT3D decomp FUN_004b9920/FUN_004bf3bc — see
#                  oot3d-decomp docs/player_anim_states.md "Back-walk / Z-target" and "Side-walk
#                  / strafe"). REPL `ztarget <0|1>` (zelda3d.c) locks focusActor onto the
#                  `asel`-selected actor via the real native lock-on entry point
#                  Player_SetAutoLockOnActor, re-asserted every frame (that function is a
#                  one-frame latch by native design — see the REPL handler comment). Drives
#                  `walkhold` under the lock + `gcam` (camera-relative stick), reads the
#                  resolved CSAB via `linkanimstate` (+ its `sideWalkBlend` field, since
#                  Player_Action_8084193C/func_80841860 ALWAYS reports the side_walkL CSAB
#                  pointer in skelAnime.animation and encodes the actual L/R choice as a blend
#                  weight against side_walkR — see the REPL handler comment in zelda3d.c).
#                  gt=decomp (no live-oracle drive wired for the Z-target recipe this session —
#                  the equipment-less oracle save's reachable-state constraint from
#                  parity_state_sweep.py's docstring applies the same way here).
#   unreachable  — no existing REPL/oracle input recipe reaches this state headlessly at all
UNREACHABLE_NO_RECIPE = "no existing REPL/oracle input recipe drives this state headlessly"


def _parse_sidewalk_blend(line):
    for tok in line.split():
        if tok.startswith("sideWalkBlend="):
            try:
                return float(tok.split("=", 1)[1])
            except ValueError:
                return None
    return None


def _parse_linkanimstate(line):
    base = None
    seg = line.split("upper=")
    if seg and "base=" in seg[0]:
        base = seg[0].split("base=")[1].split()[0]
    if base in ("(unmapped)", "(null)", "(none)"):
        base = None
    st1 = None
    for tok in line.split():
        if tok.startswith("st1=0x"):
            try:
                st1 = int(tok[4:], 16)
            except ValueError:
                pass
    return base, st1


def soh_reach_ztarget(stick, settle_frames=45):
    """Drive Zelda3D into a Z-target-locked locomotion state: warp clean, lock focus onto the
    nearest actor (`asel any 0` + `ztarget 1`), force the camera behind Link's facing (`gcam 1`,
    re-evaluated every frame so it tracks the Z-target turn), then either:
      - stick == (0, 0): turn_in_place. The turn-to-face-target fires IMMEDIATELY on lock
        acquisition and is often done within a few frames (PLAYER_ANIMGROUP_45_turn only plays
        while shape.rot.y hasn't caught up to the target yaw yet) — so POLL linkanimstate right
        after `ztarget 1` instead of settling first, and return the first "45_turn" reading seen
        (falling back to the last reading if the turn never appears, e.g. already facing target).
      - otherwise: settle onto the lock, then hold `stick`=(sx,sy) for `settle_frames` and read
        the settled selection (this is the sidestep_l/sidestep_r recipe).
    Returns (base_csab, sideWalkBlend, st1)."""
    PSS.S.soh_cmd(f"warp 0x{KOKIRI:x}")
    time.sleep(2.5)
    PSS.S.soh_ensure_free()
    PSS.S.soh_cmd("link 1")
    PSS.S.soh_cmd("asel any 0")
    PSS.S.soh_cmd("gcam 1")
    if stick == (0, 0):
        PSS.S.soh_cmd("ztarget 1")
        line = None
        last_line = None
        for _ in range(15):
            time.sleep(0.05)
            last_line = PSS.S.soh_cmd("linkanimstate")
            base, _ = _parse_linkanimstate(last_line)
            if base and "45_turn" in base:
                line = last_line
                break
        line = line or last_line or ""
    else:
        PSS.S.soh_cmd("ztarget 1")
        time.sleep(0.6)
        sx, sy = stick
        PSS.S.soh_cmd(f"walkhold {settle_frames} {sx} {sy}")
        time.sleep(settle_frames / 60.0 + 0.2)
        line = PSS.S.soh_cmd("linkanimstate")
    PSS.S.soh_cmd("walkhold 0")
    PSS.S.soh_cmd("ztarget 0")
    PSS.S.soh_cmd("gcam 0")
    base, st1 = _parse_linkanimstate(line)
    return base, _parse_sidewalk_blend(line), st1

STATE_MATRIX = [
    {"name": "model_render", "group": "render", "kind": "model",
     "note": "does the OoT3D Link body draw on-foot at all (mounted already fixed, prior work)"},

    {"name": "idle", "group": "locomotion", "kind": "idle_oracle"},
    {"name": "walk", "group": "locomotion", "kind": "speed"},
    {"name": "run", "group": "locomotion", "kind": "speed"},
    # 2026-07-15: RE'd that backwalk/sidestep_l/sidestep_r/turn_in_place ALL require a Z-target
    # lock-on (Player.focusActor != NULL) to reach in the native N64 code — confirmed the SAME
    # gate exists in the OoT3D decomp (FUN_004b9920/FUN_004bf3bc; oot3d-decomp
    # docs/player_anim_states.md). Added REPL `ztarget <0|1>` as the prerequisite primitive (see
    # UNREACHABLE_NO_RECIPE-adjacent kind "ztarget_move" doc above). sidestep_l/sidestep_r/
    # turn_in_place are now reliably driven+verified this session. backwalk's own dedicated
    # trigger (func_8083CBF0 -> Player_Action_808423EC -> gPlayerAnim_link_normal_back_walk,
    # entered only when func_8083FC68's yaw-vs-facing check returns exactly -1) stays
    # UNREACHABLE: empirically swept the full camera-relative backward stick range
    # (sy=-45..-127 at sx=0, under `ztarget`+`gcam`) and it consistently lands in the
    # side_walk (0) bucket instead of the back_walk (-1) bucket, even though the observed
    # yawTarget-vs-shape.rot.y delta is ~179.8° (near the theoretical -1 threshold per the
    # func_8083FC68 formula read from z_player.c ~8073). The discrepancy was NOT root-caused
    # this session (func_8083FC68's few local temp/speedTarget values aren't exposed via any
    # REPL readout to verify live) — needs either a live yawTarget/speedTarget debug field or
    # confirmation that Camera_GetInputDirYaw reads a camera-cached yaw that `gcam`'s direct
    # eye/at poke doesn't update (gcam never touches Camera_Update's internal state, only
    # eye/at/eyeNext) when the Z-target camera mode (2) is active.
    {"name": "backwalk", "group": "locomotion", "kind": "unreachable", "reason":
     "Z-target lock-on reached via REPL `ztarget` (prerequisite primitive, 2026-07-15), but the "
     "dedicated back_walk trigger (func_8083FC68 returning exactly -1 in z_player.c) was not "
     "reliably hit by any camera-relative backward stick magnitude swept (-45..-127); it "
     "consistently resolves to the side_walk (0) bucket instead despite a ~179.8° "
     "yawTarget-vs-facing delta — root cause not isolated this session (no live readout of the "
     "function's local speedTarget/temp values); see the STATE_MATRIX comment above for the "
     "concrete next step (expose a yawTarget/speedTarget debug field, or audit whether `gcam`'s "
     "direct eye/at poke is compatible with Camera_GetInputDirYaw under Z-target camera mode 2)"},
    {"name": "sidestep_l", "group": "locomotion", "kind": "ztarget_move", "stick": (-80, 0),
     "expect": "side_walk", "side_dir": "L",
     "note": "Z-target + pure sideways stick (camera-relative) -> PLAYER_ANIMGROUP_side_walk "
             "(func_8083CC9C -> Player_Action_8084193C); direction read from linkanimstate's "
             "sideWalkBlend field (blend>0.5 = L-sourced)"},
    {"name": "sidestep_r", "group": "locomotion", "kind": "ztarget_move", "stick": (80, 0),
     "expect": "side_walk", "side_dir": "R",
     "note": "same recipe as sidestep_l, opposite stick sign; sideWalkBlend<0.5 = R-sourced"},
    {"name": "turn_in_place", "group": "locomotion", "kind": "ztarget_move", "stick": (0, 0),
     "expect": "45_turn",
     "note": "Z-target lock with a NEUTRAL stick while not yet facing the target -> "
             "Player_SetupTurnInPlace -> PLAYER_ANIMGROUP_45_turn (nml_45_turn_free); this is "
             "literally the natural first-frame behavior of acquiring a lock-on, no extra input "
             "recipe needed beyond `ztarget 1`"},

    {"name": "jump", "group": "action", "kind": "forcestate", "pss": "jump"},
    {"name": "roll", "group": "action", "kind": "forcestate", "force": "roll", "expect": "landing_roll",
     "note": "forward dodge-roll: Zelda3D_PlayerForceRoll -> Player_SetupRoll (byte-faithful N64) "
             "plays PLAYER_ANIMGROUP_landing_roll; OoT3D anim-group table @0x53a7c0 = "
             "{138,139,139,138,138,138} = nml_landing_roll_free/nml_landing_roll (oot3d-decomp "
             "docs/player_anim_states.md, RE'd 2026-07-15 via ReadWord.py)"},
    {"name": "attack", "group": "action", "kind": "forcestate", "pss": "attack"},
    {"name": "attack_combo", "group": "action", "kind": "unreachable",
     "reason": UNREACHABLE_NO_RECIPE + " (linkstate attack forces ONE slash; no recipe chains "
               "repeated timed A presses into a combo under freeze)"},
    {"name": "shield", "group": "action", "kind": "forcestate", "pss": "shield"},
    {"name": "item_bottle_use", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (no equipped item/bottle in the headless save + no linkstate recipe for item-use anim)"},
    {"name": "pickup_carry", "group": "action", "kind": "carry", "pss": "carry"},
    {"name": "throw", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (carry recipe reaches carry-hold; no recipe drives the throw release)"},
    {"name": "climb_hang", "group": "action", "kind": "forcestate", "pss": "climb"},
    {"name": "climb_updown", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (linkstate climb forces the wall-grab/hang pose only; no recipe drives climb traversal)"},
    {"name": "swim_surface", "group": "action", "kind": "forcestate", "pss": "swim"},
    {"name": "swim_dive", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (linkstate swim forces surface swim-wait only; no recipe drives underwater dive)"},
    {"name": "mount_dismount", "group": "action", "kind": "prior",
     "note": "Epona/En_Horse 3DS mount render already ported+verified prior session — see "
             "debug_journal/2026-07-15-epona-en-horse-3ds-render.md; not re-driven by this sweep"},
    # NOTE 2026-07-15 (backwalk/sidestep/turn_in_place task): a Z-target hold primitive (REPL
    # `ztarget <0|1>`, zelda3d.c) now exists — it was added as a PREREQUISITE for the
    # locomotion-cluster states above and is reused by their "ztarget_move" driver. This row is
    # intentionally left UNREACHABLE/unclaimed: it belongs to a separate card (verifying the
    # Z-target STATE itself — reticle/HUD/camera-mode behavior, not just the locomotion gate the
    # other states needed) that a different session owns. Don't resolve this row without that
    # session's own verdict criteria.
    {"name": "ztarget", "group": "action", "kind": "unreachable", "reason":
     "no existing REPL/oracle input recipe verifies the Z-target STATE itself (lock-on reticle/"
     "HUD/camera-mode) headlessly — a locomotion-gating primitive (REPL `ztarget`) now exists "
     "(added 2026-07-15 as a prerequisite for backwalk/sidestep_l/sidestep_r/turn_in_place) but "
     "this card is about verifying Z-targeting as its own state, which is separate scope"},
    {"name": "damage_knockback", "group": "action", "kind": "forcestate", "pss": "damage"},
    {"name": "getitem_pose", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (get-item pose is triggered by a chest/pickup flag sequence with no headless recipe)"},
    {"name": "death", "group": "action", "kind": "unreachable", "reason": UNREACHABLE_NO_RECIPE +
     " (no recipe drives Link's HP to 0 headlessly)"},
]


def find(name):
    for st in STATE_MATRIX:
        if st["name"] == name:
            return st
    return None


# ---------------------------------------------------------------------------
# SoH-side probes
# ---------------------------------------------------------------------------
def soh_model_check():
    """MATCH iff `link 1` hooks the OoT3D body AND linkanimstate resolves a real (non-fallback,
    non-null) CSAB — i.e. the 3DS Link body is actually driving/drawing on-foot."""
    PSS.S.soh_cmd(f"warp 0x{KOKIRI:x}")
    time.sleep(2.5)
    PSS.S.soh_ensure_free()
    link_r = PSS.S.soh_cmd("link 1")
    src_r = PSS.S.soh_cmd("linksrc 3ds")
    base, _, _, _ = PSS.soh_animstate()
    ok = ("link=1" in link_r) and ("linksrc=3ds" in src_r) and bool(base) and "fallback" not in (base or "")
    return ok, {"link": link_r, "linksrc": src_r, "base_csab": base}


# ---------------------------------------------------------------------------
# Verdict computation per state
# ---------------------------------------------------------------------------
def run_state(st, oracle):
    name = st["name"]
    kind = st["kind"]
    row = {"name": name, "group": st["group"]}

    if kind == "unreachable":
        row.update(verdict="UNREACHABLE", reason=st["reason"])
        return row

    if kind == "prior":
        row.update(verdict="MATCH", reason=st["note"], evidence="prior-session, not re-driven")
        return row

    if kind == "model":
        ok, data = soh_model_check()
        row.update(verdict="MATCH" if ok else "DIVERGENT", data=data)
        return row

    if kind == "forcestate":
        # Two ways a forcestate names its decomp ground truth: reuse a parity_state_sweep STATE
        # (via `pss`), or carry its own `force`+`expect` inline (states link_sweep RE'd itself,
        # e.g. roll). Both drive Zelda3D_PlayerForce* through REPL `linkstate` under freeze and
        # substring-match the resolved CSAB against the decomp anim-group family.
        if "pss" in st:
            pss_st = next(s for s in PSS.STATES if s["name"] == st["pss"])
        else:
            pss_st = {"name": name, "kind": "forcestate", "force": st["force"],
                      "gt": "decomp", "expect": st["expect"]}
        soh = PSS.soh_reach(pss_st)
        v = PSS.verdict(pss_st, soh, None)
        row.update(verdict={"PASS": "MATCH", "FAIL": "DIVERGENT"}.get(v, "UNREACHABLE"),
                   soh=soh, expect=pss_st.get("expect"), gt="decomp",
                   metric="CSAB-family substring match vs oot3d-decomp action-func anim group")
        return row

    if kind == "carry":
        pss_st = next(s for s in PSS.STATES if s["name"] == st["pss"])
        soh = PSS.soh_force_carry()
        v = PSS.verdict(pss_st, soh, None)
        verdict = {"PASS": "MATCH", "FAIL": "DIVERGENT"}.get(v, "UNREACHABLE")
        reason = None
        if verdict == "UNREACHABLE":
            reason = ("carry recipe (Kakariko 0xDB cucco grab) found no id=0x19 actor within "
                       "3000u of spawn this run — the native cucco spawn this recipe assumed is "
                       "not present at this scene/time; recipe needs re-scouting, not a game bug")
        row.update(verdict=verdict, soh=soh, expect=pss_st.get("expect"), gt="decomp", reason=reason)
        return row

    if kind == "observe":
        st2 = {"name": name, "kind": "forcestate", "force": st["force"]}
        soh = PSS.soh_reach(st2)
        row.update(verdict="UNREACHABLE", soh=soh, reason=st["note"])
        return row

    if kind == "ztarget_move":
        base, blend, st1 = soh_reach_ztarget(st["stick"])
        exp = st["expect"]
        if not base:
            row.update(verdict="UNREACHABLE", soh=base, expect=exp, gt="decomp",
                       reason=f"ztarget_move drive produced no resolved base CSAB (st1=0x{st1 or 0:x})")
            return row
        ok = exp in base
        if ok and "side_dir" in st:
            if blend is None:
                ok = False
            elif st["side_dir"] == "L":
                ok = blend > 0.5
            else:
                ok = blend < 0.5
        row.update(verdict="MATCH" if ok else "DIVERGENT", soh=base, expect=exp, gt="decomp",
                   sideWalkBlend=blend, side_dir=st.get("side_dir"),
                   metric="CSAB-family substring match (+ sideWalkBlend direction check where "
                           "applicable) under native Z-target lock (REPL `ztarget`)")
        return row

    if kind == "idle_oracle":
        soh = PSS.soh_reach({"name": "idle", "kind": "idle"})
        if oracle is None or not oracle.ok:
            row.update(verdict="UNREACHABLE", soh=soh,
                       reason=f"oracle unavailable: {oracle.fail_reason if oracle else 'not booted'}")
            return row
        ora = oracle.idle_name()
        both_idle = PSS.is_idle(soh) and PSS.is_idle(ora)
        row.update(verdict="MATCH" if both_idle else "DIVERGENT", soh=soh, oracle=ora, gt="oracle",
                   metric="both sides select a *_wait* family CSAB while standing")
        return row

    if kind == "speed":
        # Delegate the SoH-side curve to parity_speed_sweep (no reimplementation); drive the
        # oracle side through the embedded-harness OracleSession, then reuse SPD.classify() /
        # SPD.windows_overlap() verbatim for the verdict — same metric #117 was fixed against.
        soh_mags = [0, 30, 60, 100, 127]
        soh_curve = SPD.soh_curve(soh_mags)
        sc = SPD.classify(soh_curve)
        if oracle is None or not oracle.ok:
            row.update(verdict="UNREACHABLE", soh_curve=soh_curve, soh_class=sc,
                       reason=f"oracle unavailable: {oracle.fail_reason if oracle else 'not booted'}")
            return row
        ora_curve = oracle.curve()  # calibrated forward deflections (see OracleSession.curve)
        oc = SPD.classify(ora_curve)
        key = "walk_csab" if name == "walk" else "run_csab"
        s_sel, o_sel = sc.get(key), oc.get(key)
        ov = SPD.windows_overlap(sc, oc)
        sel_ok = (s_sel is not None and o_sel is not None and
                  (("walk" in s_sel) == ("walk" in o_sel)) and (("run" in s_sel) == ("run" in o_sel)))
        verdict = "MATCH" if (sel_ok and ov is not False) else "DIVERGENT"
        if s_sel is None or o_sel is None:
            verdict = "UNREACHABLE"
        row.update(verdict=verdict, soh_curve=soh_curve, oracle_curve=ora_curve,
                   soh_class=sc, oracle_class=oc, soh_select=s_sel, oracle_select=o_sel,
                   window_overlap=ov, gt="oracle",
                   metric="selected CSAB family at matched speedXZ + walk/run threshold-window overlap")
        return row

    row.update(verdict="UNREACHABLE", reason=f"unhandled kind {kind}")
    return row


# ---------------------------------------------------------------------------
# sweep / persistence / checklist
# ---------------------------------------------------------------------------
def do_sweep(skip_oracle=False, only=None):
    oracle = None
    if not skip_oracle:
        oracle = OracleSession()
        print("[link_sweep] booting embedded-Azahar oracle (soh3d_harness)...", file=sys.stderr)
        if oracle.boot():
            print("[link_sweep] oracle ready (Kokiri Forest, free gameplay)", file=sys.stderr)
        else:
            print(f"[link_sweep] oracle boot FAILED: {oracle.fail_reason}", file=sys.stderr)

    rows = []
    try:
        for st in STATE_MATRIX:
            if only and st["name"] not in only:
                continue
            print(f"  [{st['name']}] driving...", file=sys.stderr)
            row = run_state(st, oracle)
            print(f"  [{st['name']:16s}] {row['verdict']}", file=sys.stderr)
            rows.append(row)
    finally:
        if oracle is not None:
            oracle.close()

    result = {"timestamp": datetime.now(timezone.utc).isoformat(), "rows": rows}
    os.makedirs(SCRATCH, exist_ok=True)
    ts_path = os.path.join(SCRATCH, f"{int(time.time())}.json")
    json.dump(result, open(ts_path, "w"), indent=2)
    print(f"[link_sweep] wrote raw {ts_path}", file=sys.stderr)
    # NOTE: latest.json is written by the CALLER (cmd_sweep), AFTER merging when --only is used —
    # writing it here would clobber the rows a scoped run didn't re-drive before _merged reads them.
    return result


def load_latest():
    latest_path = os.path.join(SCRATCH, "latest.json")
    if not os.path.exists(latest_path):
        sys.exit("link_sweep: no scratch/link_sweep/latest.json — run `sweep` first")
    return json.load(open(latest_path))


def write_checklist(result):
    rows = result["rows"]
    counts = {}
    for r in rows:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    lines = []
    lines.append("# Link (on-foot, ZELDA3D_LINK) parity checklist")
    lines.append("")
    lines.append("**GENERATED by `tools/link_sweep.py sweep` — do not hand-edit.** Re-run the "
                 "tool to refresh; edit `STATE_MATRIX` in `tools/link_sweep.py` to add/change "
                 "states, not this file.")
    lines.append("")
    lines.append(f"Last swept: {result['timestamp']}")
    lines.append("")
    lines.append(f"**Summary:** " + ", ".join(f"{v}={counts.get(v,0)}"
                 for v in ("MATCH", "DIVERGENT", "UNREACHABLE")) + f" (of {len(rows)})")
    lines.append("")
    lines.append("Ground truth (`gt`): `oracle` = live OoT3D reading via the embedded-Azahar "
                 "harness (`tools/soh3d_harness`, `az_linkanim`); `decomp` = the oot3d-decomp "
                 "action-func CSAB family (the equipment-less oracle save can't reach these "
                 "states live — see `tools/parity_state_sweep.py` docstring).")
    lines.append("")
    lines.append("| State | Group | Verdict | Metric / gt | Evidence | Resolved commit |")
    lines.append("|---|---|---|---|---|---|")
    for r in rows:
        name = r["name"]
        group = r["group"]
        verdict = r["verdict"]
        metric = r.get("metric") or r.get("gt") or "-"
        evid_bits = []
        for k in ("soh", "oracle", "soh_select", "oracle_select", "window_overlap"):
            if k in r and r[k] is not None:
                evid_bits.append(f"{k}={r[k]}")
        if "reason" in r and r["reason"]:
            evid_bits.append(f"reason: {r['reason']}")
        evidence = "; ".join(evid_bits) if evid_bits else "-"
        evidence = evidence.replace("|", "\\|")
        resolved = r.get("resolved_commit", "-")
        lines.append(f"| {name} | {group} | {verdict} | {metric} | {evidence} | {resolved} |")
    lines.append("")
    lines.append("Raw per-run data (full curves, oracle transcripts): `scratch/link_sweep/*.json` "
                 "(gitignored — re-run `tools/link_sweep.py sweep` to regenerate).")
    lines.append("")
    with open(CHECKLIST_MD, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[link_sweep] wrote {CHECKLIST_MD}", file=sys.stderr)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def cmd_sweep(args):
    only = set(args.only.split(",")) if args.only else None
    result = do_sweep(skip_oracle=args.skip_oracle, only=only)
    final = _merged(result) if only else result
    # Write latest.json exactly once, from the FINAL (merged, when scoped) row set.
    json.dump(final, open(os.path.join(SCRATCH, "latest.json"), "w"), indent=2)
    write_checklist(final)


def _merged(partial):
    """When --only is used, merge the partial result into the existing latest.json instead of
    clobbering rows that weren't re-run. Returns the merged result (caller writes latest.json)."""
    path = os.path.join(SCRATCH, "latest.json")
    if not os.path.exists(path):
        return partial
    base = json.load(open(path))
    by_name = {r["name"]: r for r in base["rows"]}
    for r in partial["rows"]:
        by_name[r["name"]] = r
    order = [st["name"] for st in STATE_MATRIX]
    merged_rows = [by_name[n] for n in order if n in by_name]
    return {"timestamp": partial["timestamp"], "rows": merged_rows}


def cmd_show(args):
    result = load_latest()
    for r in result["rows"]:
        if r["name"] == args.state:
            print(json.dumps(r, indent=2))
            return
    sys.exit(f"no such state {args.state!r} in latest sweep")


def cmd_list(args):
    result = load_latest()
    want = args.status.upper() if args.status else None
    for r in result["rows"]:
        if want and r["verdict"] != want:
            continue
        print(f"{r['verdict']:12s} {r['group']:12s} {r['name']}")


def cmd_resolve(args):
    path = os.path.join(SCRATCH, "latest.json")
    result = load_latest()
    found = False
    for r in result["rows"]:
        if r["name"] == args.state:
            r["resolved_commit"] = args.commit
            if r["verdict"] == "DIVERGENT":
                r["verdict"] = "MATCH"
            found = True
    if not found:
        sys.exit(f"no such state {args.state!r} in latest sweep")
    json.dump(result, open(path, "w"), indent=2)
    write_checklist(result)
    print(f"[link_sweep] {args.state} resolved @ {args.commit}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("sweep", help="drive the full state matrix, write checklist")
    sp.add_argument("--skip-oracle", action="store_true")
    sp.add_argument("--only", default=None, help="comma list of state names")
    sp.set_defaults(func=cmd_sweep)

    sh = sub.add_parser("show", help="dump the raw result row for one state")
    sh.add_argument("state")
    sh.set_defaults(func=cmd_show)

    ls = sub.add_parser("list", help="list states from the last sweep")
    ls.add_argument("--status", choices=["match", "divergent", "unreachable"], default=None)
    ls.set_defaults(func=cmd_list)

    rs = sub.add_parser("resolve", help="mark a state resolved by a fixing commit")
    rs.add_argument("state")
    rs.add_argument("--commit", required=True)
    rs.set_defaults(func=cmd_resolve)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
