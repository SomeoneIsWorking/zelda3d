#!/usr/bin/env python3
"""link_sweep.py — the Link (on-foot, 3DS-body, ZELDA3D_LINK=1) state-matrix parity SWEEP.

ORCHESTRATOR ONLY. Does not reimplement any per-dimension drive/verdict logic that already
exists — it composes:
  - parity_state_sweep.py  (discrete forced-state CSAB SELECTION vs the OoT3D decomp ground
                             truth: jump/swim/damage/shield/attack/climb/carry/idle)
  - parity_speed_sweep.py  (the walk/run locomotion CONTINUUM by speedXZ; SoH-side curve +
                             classify()/windows_overlap() reused verbatim for the oracle side)
  - REPL primitives (zelda3d_repl.py: link/linksrc/linkanimstate/linkstate/warp/...)
  - the EMBEDDED-Azahar oracle harness (focused Python owners + tools/soh3d_harness), NOT the
    embedded harness (the only oracle transport) that parity_state_sweep/parity_speed_sweep
    to — that external frontend needs Qt6, which is not installed on this machine (see
    OracleSession docstring below). `az_linkanim` (added to tools/soh3d_harness/main.cpp this
    session) reads the PLAYER+0x254+0x30 animId, so
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
import argparse, json, os, sys, time
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import parity_state_sweep as PSS   # noqa: E402  (forcestate/idle/carry drive + verdict)
import parity_speed_sweep as SPD   # noqa: E402  (locomotion continuum: soh_curve/classify)
import parity_pose_sweep as PPS    # noqa: E402  (walk/run soh_mag/csab config — reused, not re-picked)
import parity_pose_diff as PPD     # noqa: E402  (geodesic per-bone LOCAL-rotation compare, reused verbatim)
from harness_gameplay import boot_to_gameplay  # noqa: E402
from harness_paths import TITLE_STATE  # noqa: E402
from harness_process import spawn  # noqa: E402

SCRATCH = os.path.join(REPO, "scratch", "link_sweep")
CHECKLIST_MD = os.path.join(REPO, "docs", "link_parity_checklist.md")
NAMES_TABLE = os.path.join(REPO, "oot3d-decomp", "tools", "skeldata",
                           "player_animid_names.json")
NAMES_TABLE = os.path.abspath(NAMES_TABLE)
SAVE_STATE = str(TITLE_STATE)

KOKIRI = 0xEE
LINKJOINTS_NBONE = 25  # childlink_v2 rig; must match soh3d_harness/main.cpp LINKJOINTS_NBONE
POSE_OUT = os.path.join(REPO, "scratch", "link_sweep", "pose")


# ---------------------------------------------------------------------------
# Oracle transport: the embedded Azahar harness (soh3d_harness).
# ---------------------------------------------------------------------------
class OracleSession:
    """One embedded-Azahar (soh3d_harness) process, booted to free gameplay at Kokiri Forest,
    kept alive for the DURATION of a sweep so every oracle-side probe amortizes the ~4-5min
    cold-boot cost (the current title checkpoint -> START/A taps -> gPlayState populated -> warp 0xEE)
    over ONE session instead of paying it per state.

    Transport: the embedded-Azahar harness (`Azahar/build-harness`, target `soh3d_harness`,
    driven by the focused harness process/gameplay owners). It is the only oracle transport — the standalone
    Qt-frontend Azahar and its UDP-RPC tools were removed. Link's selected animation comes
    from the `az_linkanim` REPL command at PLAYER+0x254+0x30, named against
    oot3d-decomp/tools/skeldata/player_animid_names.json.
    """

    def __init__(self):
        self.names = json.load(open(NAMES_TABLE))["names"]
        self.h = None
        self.ok = False
        self.fail_reason = None

    def boot(self):
        """Boot to gameplay at Kokiri Forest.

        Delegates to harness_gameplay.boot_to_gameplay: loadstate the cached gameplay
        state (or capture it once), then warp. The old inline schedule tapped
        hold=30/release=60 and gated on `playstate`, which reports ok AT THE
        TITLE — so it happily "succeeded" on the title screen and warped into a
        context with no loaded save, where the warp is a no-op.
        """
        try:
            self.h = spawn(save_state=SAVE_STATE)
            if not boot_to_gameplay(self.h, entrance=KOKIRI):
                self.fail_reason = "oracle never reached gameplay (see harness output)"
                return False
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
    # walk from run — circle-pad-style 0..100 magnitudes
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

    def sample_joints(self):
        """One `az_linkjoints` capture: 25 bones x 3x3 LOCAL rotation (see soh3d_harness/main.cpp
        SKELANIME_JOINTTABLE_OFF, 2026-07-15 — the embedded-harness POSE analog of `az_linkanim`
        SELECTION). Returns {bone: (r0..r8)} or None on any parse failure."""
        try:
            lines = self.h.send_multiline("az_linkjoints")
        except Exception:
            return None
        if not lines or not lines[0].startswith("ok linkjoints"):
            return None
        out = {}
        for line in lines[1:]:
            line = line.strip()
            if line in ("ok end",) or line.startswith("ok "):
                continue
            toks = line.split()
            if len(toks) != 10:
                continue
            bone = int(toks[0])
            out[bone] = tuple(float(x) for x in toks[1:])
        return out if len(out) == LINKJOINTS_NBONE else None

    def capture_pose(self, deflection, n_samples=40, step_frames=3, warmup_frames=45):
        """Hold a calibrated forward analog deflection (see FORWARD_DEFLECTIONS) and take
        `n_samples` `az_linkjoints` captures spaced `step_frames` apart — the embedded-harness
        POSE analog of `curve()` (which only samples SELECTION). Returns a list of
        (cap, t_ms, bone, r0..r8) rows in the per-bone LOCAL-rotation CSV shape, so
        tools/parity_pose_diff.py consumes it unchanged. None if the drive/read never produced a
        usable sample."""
        self.resettle()
        self.h.send(f"analog 0 {int(deflection)}")
        self.h.send(f"run {warmup_frames}")
        rows = []
        for i in range(n_samples):
            self.h.send(f"analog 0 {int(deflection)}")
            self.h.send(f"run {step_frames}")
            joints = self.sample_joints()
            if joints is None:
                continue
            t_ms = round(i * step_frames * (1000.0 / 60.0), 1)
            for bone, r in joints.items():
                rows.append((i, t_ms, bone, r))
        self.h.send("analog 0 0")
        return rows if rows else None

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


def soh_reach_ztarget_idle_stance():
    """ztarget (its own STATE, not the locomotion-gate primitive): decomp ground truth is
    oot3d-decomp docs/player_anim_states.md "Standing-aim / Z-hold (#88-aim), FUN_00488b40" =
    Player_Action_80840450, entered automatically (func_80839E88/func_80839F90) once a HOSTILE-
    category focusActor is locked and the stick returns to neutral (Player_CheckHostileLockOn
    gate). Kokiri Forest's own spawn has NO ACTORCAT_ENEMY actors loaded within 3000u (confirmed
    live via `actorsnear 3000`, 2026-07-15 — real OoT has no monsters there), so this warps to the
    Deku Tree entrance (ENTR_DEKU_TREE_ENTRANCE, 0x0) instead, which has live En_Dekubaba
    (id 0x55, confirmed via the same actorsnear probe) within a few hundred units — a genuinely
    hostile, stationary lock-on target, not the friendly/neutral path (Player_Action_808407CC)
    `asel any` would otherwise reach. Returns (idleStance, focusActor, base_csab, err)."""
    EN_DEKUBABA = 0x55
    DEKU_TREE_ENTRANCE = 0x0
    PSS.S.soh_cmd(f"warp 0x{DEKU_TREE_ENTRANCE:x}")
    time.sleep(3.0)
    PSS.S.soh_ensure_free()
    PSS.S.soh_cmd("link 1")
    sel = PSS.S.soh_cmd(f"asel 0x{EN_DEKUBABA:x} 0")
    if "NONE" in sel or "no match" in sel.lower():
        return None, None, None, f"no En_Dekubaba (id 0x{EN_DEKUBABA:x}) found near Deku Tree spawn"
    r = PSS.S.soh_cmd("ztarget 1")
    PSS.S.soh_cmd("gcam 1")
    PSS.S.soh_cmd("walkhold 60 0 0")  # neutral stick, let Link settle to standing lock-on
    time.sleep(1.2)
    st = PSS.S.soh_cmd("ztargetstate")
    line = PSS.S.soh_cmd("linkanimstate")
    PSS.S.soh_cmd("walkhold 0")
    PSS.S.soh_cmd("ztarget 0")
    PSS.S.soh_cmd("gcam 0")
    idle = None
    focus = None
    for tok in st.split():
        if tok.startswith("idleStance="):
            idle = int(tok.split("=", 1)[1])
        elif tok.startswith("focusActor="):
            focus = tok.split("=", 1)[1]
    base, st1 = _parse_linkanimstate(line)
    return idle, focus, base, None


def soh_reach_death():
    """death: `linkstate death` only sets gSaveContext.health=0 (the real per-frame trigger,
    func_8083D53C in z_player.c, drives the actual death entry over the FOLLOWING frames — freezing
    immediately would starve that check). Settle unfrozen for ~30 frames, then freeze and read the
    CSAB. Ground truth: derth_rebirth (grounded/non-shocked branch) per oot3d-decomp
    docs/player_anim_states.md death entry (func_80836448)."""
    PSS.S.soh_cmd(f"warp 0x{KOKIRI:x}")
    time.sleep(2.5)
    PSS.S.soh_ensure_free()
    PSS.S.soh_cmd("link 1")
    PSS.S.soh_cmd("linkstate death")
    time.sleep(0.6)
    line = PSS.S.soh_cmd("linkanimstate")
    base, st1 = _parse_linkanimstate(line)
    PSS.S.soh_cmd("linkstate idle")  # restores gSaveContext.health (see Zelda3D_PlayerForceIdle)
    return base, st1


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
    # trigger (func_8083CBF0 -> Player_Action_808423EC -> gPlayerAnim_link_anchor_back_walk /
    # CSAB ac_back_walk, entered only when func_8083FC68's yaw-vs-facing check returns exactly -1)
    # was NOT reliably hit by any camera-relative backward stick magnitude swept live (-45..-127;
    # consistently landed in the side_walk (0) bucket despite a ~179.8° yawTarget-vs-facing delta,
    # a live-decoder precision issue, not a missing code path — see z_player.c ~8188 func_8083FC68).
    # 2026-07-15 (this session): closed via a direct Force* hook instead of fighting the live
    # decoder — Zelda3D_PlayerForceBackwalk (z_player.c) calls func_8083CBF0 directly with
    # yawTarget=shape.rot.y+0x8000 (an unambiguous dead-behind push), reproducing byte-for-byte
    # the SAME call the real func_8083FC68<0 branch makes. REPL `linkstate backwalk`.
    {"name": "backwalk", "group": "locomotion", "kind": "forcestate", "force": "backwalk",
     "expect": "back_walk",
     "note": "Zelda3D_PlayerForceBackwalk -> func_8083CBF0(yaw=facing+0x8000) -> "
             "Player_Action_808423EC + gPlayerAnim_link_anchor_back_walk -> CSAB ac_back_walk "
             "(oot3d-decomp docs/player_anim_states.md, direct anim ref not a PLAYER_ANIMGROUP "
             "table entry); the SAME func_8083CBF0 call the live yawTarget<0 branch makes"},
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
    {"name": "attack_combo", "group": "action", "kind": "forcestate", "force": "attack2",
     "expect": "kiru_fin",
     "note": "combo-swing 2 (Zelda3D_PlayerForceAttackCombo2 -> meleeWeaponAnimation="
             "PLAYER_MWA_FORWARD_COMBO_1H, the real D_80854190 row live combo-chain advancement "
             "(func_80837818/func_80837948) would itself select) -> gPlayerAnim_link_fighter_"
             "normal_kiru_finsh -> CSAB ft_nml_kiru_fin (zelda3d_player_animmap.inc); RE'd "
             "2026-07-15 from z_player.c D_80854190 table + Player_Action_808502D0"},
    {"name": "shield", "group": "action", "kind": "forcestate", "pss": "shield"},
    {"name": "item_bottle_use", "group": "action", "kind": "forcestate", "force": "itemuse",
     "expect": "bt_bug_miss",
     "note": "2026-07-16: added Zelda3D_PlayerForceItemUse (z_player.c) — installs "
             "Player_Action_SwingBottle + sBottleSwingInfo[0].missAnimation (av2.inWater forced 0 "
             "for the deterministic dry-land pick), the SAME action+anim func_8083C6B8's C-button "
             "'use held bottle' dispatch installs, bypassing only the need for a live equipped "
             "bottle -> CSAB bt_bug_miss (oot3d-decomp docs/player_anim_states.md "
             "'item_bottle_use', direct anim ref, not a PLAYER_ANIMGROUP table entry)"},
    {"name": "pickup_carry", "group": "action", "kind": "forcestate", "force": "carry",
     "expect": "carryB",
     "note": "2026-07-16: superseded the SESSION-5 cucco-grab recipe (flaky — no id=0x19 cucco "
             "reliably spawns at the scouted scene/time) with Zelda3D_PlayerForceCarry "
             "(z_player.c) — installs Player_Action_80846050 + "
             "GET_PLAYER_ANIM(PLAYER_ANIMGROUP_carryB, modelAnimType) directly, the SAME "
             "action+anim the real generic-liftable pickup path installs, bypassing only the "
             "need for a live interactRangeActor -> CSAB nml_carryB_free (oot3d-decomp "
             "docs/player_anim_states.md 'pickup_carry', anim-group table @ 0x53a778)"},
    {"name": "throw", "group": "action", "kind": "forcestate", "force": "throw", "expect": "nml_throw",
     "note": "2026-07-16: added Zelda3D_PlayerForceThrow (z_player.c) — a thin wrapper that calls "
             "the existing internal func_8083EA94(this, play) directly (Player_Action_80846578 + "
             "GET_PLAYER_ANIM(PLAYER_ANIMGROUP_throw, modelAnimType)), literally the same helper "
             "the real throw-release branch of Player_ActionHandler_9 calls once func_8083EAF0 "
             "selects THROW over PUT_DOWN -> CSAB nml_throw_free (oot3d-decomp "
             "docs/player_anim_states.md 'throw', anim-group table @ 0x53a970)"},
    {"name": "climb_hang", "group": "action", "kind": "forcestate", "pss": "climb"},
    # 2026-07-15: RE'd that func_8083EC18 (driven by the existing `forceclimb` primitive)
    # unconditionally installs func_8083A3B0 -> Player_Action_8084BF1C (the real ladder-traversal
    # action func) regardless of real-ladder-vs-forced-wall branch, so no new CODE PATH was needed
    # to reach traversal — but the only remaining live route (`forceclimb` on a genuine
    # yDistToLedge>=79 wall) was geometrically blocked: an 8-heading x graduated-distance walk
    # sweep from Deku Tree/Kokiri Forest spawns found no wall tall enough near open-ground spawn.
    # Closed this session via a direct Force* hook instead of continuing the geometry hunt:
    # Zelda3D_PlayerForceClimbMove (z_player.c) calls func_8083A3B0 directly (the identical
    # install func_8083EC18 makes) with av1.actionVar1=2 (forceclimb's own forced-wall branch),
    # then plays ageProperties->unk_AC[2] = gPlayerAnim_link_normal_Fclimb_upL, the SAME traversal
    # anim family a real tall wall would resolve to. REPL `linkstate climbup`/`climbdown`.
    {"name": "climb_updown", "group": "action", "kind": "forcestate", "force": "climbup",
     "expect": "climb_up",
     "note": "Zelda3D_PlayerForceClimbMove(dir=1) -> func_8083A3B0 -> Player_Action_8084BF1C + "
             "gPlayerAnim_link_normal_Fclimb_upL -> CSAB Fclimb_upL (contains substring "
             "'climb_up'; oot3d-decomp docs/player_anim_states.md climb_updown section, "
             "sAgeProperties.unk_AC[2] init table, NOT age-split for the forced-wall indices) — "
             "distinct from the static nml_climb_startA/B grab pose and from ForceHang's "
             "nml_hang_* wall-HANG family (a different action func, jump_climb short-climb)"},
    {"name": "swim_surface", "group": "action", "kind": "forcestate", "pss": "swim"},
    {"name": "swim_dive", "group": "action", "kind": "forcestate", "force": "dive", "expect": "sw_swim",
     "note": "underwater dive (Zelda3D_PlayerForceSwimDive -> Player_Action_8084DC48 + "
             "func_8083D330's settled loop gPlayerAnim_link_swimer_swim) -> CSAB sw_swim, distinct "
             "from ForceSwim's surface sw_swim_wait; ground truth func_8083D12C's A-press branch "
             "(z_player.c ~6797) + func_8083D330 (~6861)"},
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
    {"name": "ztarget", "group": "action", "kind": "ztarget_state",
     "note": "ztarget AS ITS OWN STATE (2026-07-15, separate scope from the locomotion-gate "
             "primitive above): decomp ground truth oot3d-decomp docs/player_anim_states.md "
             "\"Standing-aim / Z-hold (#88-aim), FUN_00488b40\" = N64 twin Player_Action_80840450, "
             "entered automatically via func_80839E88/func_80839F90 once a HOSTILE-category "
             "focusActor is locked + stick neutral (Player_CheckHostileLockOn gate). New read-only "
             "REPL `ztargetstate` + query hook Zelda3D_PlayerIsZTargetIdleStance reads the live "
             "actionFunc pointer (file-local to z_player.c, not directly comparable from "
             "zelda3d.c). Decomp-ground-truth check (no live oracle A/B — the embedded harness's "
             "az_linkanim reads the SoH-side offset convention only; a hostile hand-to-hand "
             "Z-lock's OoT3D-side camera-mode readback would need a new harness command, deferred "
             "as out of this session's time budget in favor of the decomp-verified check, which is "
             "a legitimate ground truth per the task brief's explicit fallback)."},
    {"name": "damage_knockback", "group": "action", "kind": "forcestate", "pss": "damage"},
    {"name": "getitem_pose", "group": "action", "kind": "forcestate", "force": "getitem",
     "expect": "dm_get_itemB",
     "note": "get-item raised-arm pose (Zelda3D_PlayerForceGetItem -> Player_SetupWaitForPutAway"
             "(func_8083A434) + gPlayerAnim_link_demo_get_itemB) -> CSAB dm_get_itemB "
             "(zelda3d_player_animmap.inc); ground truth z_player.c ~7354 (func_8083E4C4's caller, "
             "the non-cutscene pickup path) + func_8083A434 (~5620)"},
    {"name": "death", "group": "action", "kind": "death", "expect": "derth_rebirth",
     "note": "Zelda3D_PlayerForceDeath sets gSaveContext.health=0 ONLY — the REAL per-frame death "
             "check (func_8083D53C, z_player.c ~12248, already runs every Player_Update) then "
             "drives func_80836448 -> gPlayerAnim_link_derth_rebirth (grounded/non-shocked branch) "
             "on its own over the next few frames; no separate trigger hook was needed, confirmed "
             "by reading that check before adding any code (would have been a bandaid to duplicate "
             "func_80836448's dispatch logic when the engine already runs it)"},
]


# ---------------------------------------------------------------------------
# POSE dimension (2026-07-15): selection-only verdicts above can't catch a
# state where SoH picks the RIGHT csab but poses it WRONG (bad retarget,
# wrong frame, joint offset) — exactly the class of bug the Epona
# right-anim/wrong-drive gait stutter turned out to be. This section adds a
# geometry-level per-bone verdict for the two states an oracle can actually
# be DRIVEN to hold live (walk/run — see parity_pose_sweep.py's own
# docstring on why gated states can't be posed on the equipment-less oracle
# save). Reuses parity_pose_sweep's STATES config (soh_mag/csab/minspd) and
# parity_pose_diff's geodesic-angle math verbatim — no re-derivation.
# ---------------------------------------------------------------------------
POSE_PASS_DEG = PPS.PASS_DEG          # MATCH threshold, shared with parity_pose_sweep (12deg)
POSE_DIVERGENT_DEG = 20.0             # matches parity_pose_diff.py's own MATCH/DIVERGENT split


def write_pose_csv(rows, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("cap,t_ms,bone,r0,r1,r2,r3,r4,r5,r6,r7,r8\n")
        for cap, t_ms, bone, r in rows:
            f.write(f"{cap},{t_ms},{bone}," + ",".join(f"{v:.6f}" for v in r) + "\n")


def capture_soh_pose(csab, mag, minspd, out_path, max_iters=80, burst_frames=400, burst_draws=120):
    """SoH-side pose capture: drive to a steady CSAB at speed via `walkhold`, then `skindump` a
    dense burst. Same recipe as parity_pose_sweep.capture_soh (not reimplemented, ported inline
    because link_sweep drives SoH through PSS.S.soh_cmd, not a bare subprocess helper)."""
    cmd = PSS.S.soh_cmd
    cmd(f"warp 0x{KOKIRI:x}")
    time.sleep(0.6)
    cmd("link 1")
    cmd("gcam 1")
    base, spd = None, 0.0
    steady = False
    for _ in range(max_iters):
        cmd(f"walkhold 30 0 {mag}")
        time.sleep(0.05)
        line = cmd("linkanimstate")
        base, st1 = _parse_linkanimstate(line)
        m = None
        for tok in line.split():
            if tok.startswith("speedXZ="):
                try:
                    spd = float(tok.split("=", 1)[1])
                except ValueError:
                    spd = 0.0
        if base == csab and spd >= minspd:
            steady = True
            break
    if not steady:
        cmd("walkhold 0")
        return None, f"soh never reached steady {csab} (last base={base} spd={spd})"
    cmd(f"walkhold {burst_frames} 0 {mag}")
    cmd(f"skindump {out_path} {burst_draws}")
    for _ in range(20):
        cmd(f"walkhold {burst_frames} 0 {mag}")
        time.sleep(0.03)
    cmd("walkhold 0")
    return (out_path if os.path.exists(out_path) else None), None


def pose_verdict_for(name, oracle):
    """Run the geometry-level POSE compare for `name` (walk|run) — the pose analog of the
    `speed` kind's selection verdict. Returns a dict merged into the row: pose_verdict,
    pose_metric_deg, pose_worst_bones, pose_reason."""
    cfg = PPS.STATES.get(name)
    if not cfg:
        return {"pose_verdict": "N/A", "pose_reason": f"no parity_pose_sweep config for {name}"}
    if oracle is None or not oracle.ok:
        return {"pose_verdict": "N/A",
                "pose_reason": f"oracle unavailable: {oracle.fail_reason if oracle else 'not booted'}"}
    defl = {"walk": -24000, "run": -32000}.get(name)
    if defl is None:
        return {"pose_verdict": "N/A", "pose_reason": f"no calibrated oracle deflection for {name}"}

    ora_rows = oracle.capture_pose(defl)
    if not ora_rows:
        return {"pose_verdict": "N/A",
                "pose_reason": "oracle capture_pose produced no samples (drive/read failure)"}
    ora_csv = os.path.join(POSE_OUT, f"oracle_{name}.csv")
    write_pose_csv(ora_rows, ora_csv)

    soh_csv = os.path.join(POSE_OUT, f"soh_{name}.csv")
    soh_out, err = capture_soh_pose(cfg["csab"], cfg["soh_mag"], cfg["minspd"], soh_csv)
    if not soh_out:
        return {"pose_verdict": "N/A", "pose_reason": err or "soh capture_soh_pose failed"}

    bones = PPD.parse_bones("1-21")
    soh_local = PPD.load_soh_local(soh_csv, set(bones))
    ora_local = PPD.load_oracle_local(ora_csv, set(bones))
    if not soh_local or not ora_local:
        return {"pose_verdict": "N/A",
                "pose_reason": f"empty pose capture (soh={len(soh_local)}f oracle={len(ora_local)}f)"}

    import numpy as np
    best_means = []
    worst_bone = {}
    for ocap in sorted(ora_local):
        of = ora_local[ocap]
        best = None
        for scap in sorted(soh_local):
            sf = soh_local[scap]
            common = [b for b in bones if b in of and b in sf]
            if len(common) < 4:
                continue
            per = {b: PPD.geo_angle(of[b], sf[b]) for b in common}
            mean = sum(per.values()) / len(per)
            if best is None or mean < best[0]:
                best = (mean, per)
        if best is None:
            continue
        best_means.append(best[0])
        for b, d in best[1].items():
            worst_bone[b] = max(worst_bone.get(b, 0.0), d)
    if not best_means:
        return {"pose_verdict": "N/A", "pose_reason": "no comparable (soh,oracle) frame pairs"}

    med = float(np.median(best_means))
    verdict = "POSE-MATCH" if med < POSE_PASS_DEG else (
        "POSE-DIVERGENT" if med > POSE_DIVERGENT_DEG else "POSE-MARGINAL")
    top = sorted(worst_bone, key=lambda b: -worst_bone[b])[:3]
    top_str = ", ".join(f"{PPD.LABEL.get(b, '?')}({b})={worst_bone[b]:.1f}deg" for b in top)
    return {"pose_verdict": verdict, "pose_metric_deg": round(med, 1),
            "pose_worst_bones": top_str,
            "pose_reason": f"median best-phase per-bone geodesic angle over {len(best_means)} "
                           f"oracle frames vs {len(soh_local)} soh frames "
                           f"(MATCH<{POSE_PASS_DEG:.0f}deg, DIVERGENT>{POSE_DIVERGENT_DEG:.0f}deg)"}


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
    row = {"name": name, "group": st["group"],
           # Default POSE verdict: selection-only kinds (forcestate/carry/ztarget_move/idle_oracle/
           # model/death/ztarget_state/observe/prior/unreachable) have no live geometry oracle — the
           # equipment-less oracle save can't be driven into these states at all (same constraint
           # parity_pose_sweep.py's own docstring documents for gated states). Only `speed`
           # (walk/run) below overrides this with a real POSE-MATCH/MARGINAL/DIVERGENT verdict.
           "pose_verdict": "N/A",
           "pose_reason": "no live pose oracle for this state (selection-only; gt=decomp/oracle-id "
                          "above) — see docs/link_parity_checklist.md pose-verdict column note"}

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

    if kind == "ztarget_state":
        idle, focus, base, err = soh_reach_ztarget_idle_stance()
        if err:
            row.update(verdict="UNREACHABLE", reason=err)
            return row
        ok = bool(idle)
        row.update(verdict="MATCH" if ok else "DIVERGENT", soh=base, idleStance=idle,
                   focusActor=focus, gt="decomp",
                   metric="live actionFunc == Player_Action_80840450 (decomp twin FUN_00488b40) "
                          "after locking a hostile focusActor + neutral stick")
        return row

    if kind == "death":
        base, st1 = soh_reach_death()
        exp = st["expect"]
        ok = bool(base) and exp in base
        row.update(verdict="MATCH" if ok else "DIVERGENT", soh=base, expect=exp, gt="decomp",
                   metric="CSAB-family substring match ~30 frames after gSaveContext.health=0")
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
        row.update(pose_verdict_for(name, oracle))
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
    lines.append("**Selection vs POSE (2026-07-15):** the `Verdict` column below checks anim "
                 "SELECTION only — does SoH pick the same CSAB as the ground truth. It CANNOT catch "
                 "a state where SoH picks the right anim but poses it wrong (bad retarget, wrong "
                 "frame, joint offset) — the class of bug the Epona right-anim/wrong-drive gait "
                 "stutter turned out to be. `Pose verdict` is a SEPARATE geometry-level check: "
                 "per-bone LOCAL-rotation geodesic angle between SoH's `skindump` and a live "
                 "`az_linkjoints` oracle capture (`tools/parity_pose_diff.py` math, reused "
                 "verbatim), best-phase matched since the two playheads aren't frame-locked. "
                 "POSE-MATCH < 8°, POSE-MARGINAL 8-20°, POSE-DIVERGENT > 20° (median across "
                 "frames). Only walk/run currently have a live pose oracle (the equipment-less "
                 "oracle save can't be driven into gated states at all — same constraint "
                 "`parity_pose_sweep.py` documents); all other rows are `N/A` for pose and remain "
                 "selection-only until a rendered-crop A/B or a state-specific pose recipe is "
                 "built for them.")
    lines.append("")
    lines.append("| State | Group | Verdict | Metric / gt | Evidence | Pose verdict | Pose detail | Resolved commit |")
    lines.append("|---|---|---|---|---|---|---|---|")
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
        pose_verdict = r.get("pose_verdict", "N/A")
        pose_bits = []
        if r.get("pose_metric_deg") is not None:
            pose_bits.append(f"median={r['pose_metric_deg']}deg")
        if r.get("pose_worst_bones"):
            pose_bits.append(f"worst: {r['pose_worst_bones']}")
        if r.get("pose_reason"):
            pose_bits.append(r["pose_reason"])
        pose_detail = "; ".join(pose_bits) if pose_bits else "-"
        pose_detail = pose_detail.replace("|", "\\|")
        resolved = r.get("resolved_commit", "-")
        lines.append(f"| {name} | {group} | {verdict} | {metric} | {evidence} | {pose_verdict} | "
                     f"{pose_detail} | {resolved} |")
    lines.append("")
    lines.append("Raw per-run data (full curves, oracle transcripts): `scratch/link_sweep/*.json` "
                 "(gitignored — re-run `tools/link_sweep.py sweep` to regenerate).")
    lines.append("")
    lines.append("**RE control/debug backlog:** several MATCH rows above are reached via a "
                 "Force*-hook BYPASS of the real N64 entry gate (swim_dive, climb_hang/"
                 "climb_updown) rather than a natively-driven decode — see "
                 "`docs/re_control_debug_backlog.md` for the specific unnamed decomp functions "
                 "whose further RE would let these be driven natively, plus new-coverage / "
                 "debug-readout gaps (camera-mode readout, a distinct \"putdown\" state, "
                 "frame-exact death detection). `backwalk` was closed 2026-07-15 — "
                 "`Zelda3D_PlayerForceBackwalk` now drives the real `func_8083FC68` decode "
                 "(feeds it the exact input that lands its `-1` branch) instead of bypassing it; "
                 "see `docs/re-frontier.md` `player.backwalk-decode`.")
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
