#!/usr/bin/env python3
"""Generate the PLAYER (Link) N64-anim -> OoT3D-CSAB table: zelda3d_player_animmap.inc.

Link is special-cased (NOT the ZELDA3D_AUTO path): Zelda3D_TryDrawPlayer reads the live
Player.skelAnime.animation (a const char* OTR string, basename gPlayerAnim_link_*) and plays
the OoT3D *_new link rig's OWN matching CSAB, phase-locked. See memory zelda3d-link-player-path.

The N64 decomp anim names map to the 3DS CSAB names by a small set of category-prefix + token
rewrites (e.g. normal_->nml_, fighter_->ft_, demo_->dm_, finsh->fin, power->pow). This generator
applies those rules to every gPlayerAnim_link_* symbol in the SoH source and EMITS ONLY the pairs
whose resolved CSAB actually exists in BOTH the boy and child *_new zars (so one basename table is
correct for either age). An N64 anim with no resolvable CSAB is omitted -> the runtime resolver
falls back to ZELDA3D_LINK_IDLE_CSAB (so Link reads as standing, never frozen in bind pose).

The REWRITE RULES are the real knowledge here; the table is just their verified projection. To
extend coverage, add a rule below and rerun. Validation against the live zar means a wrong rule
can only ever drop an entry, never emit a bad one.

Run:  . ./.env && python3 tools/gen_player_animmap.py
Needs ZELDA3D_OOT3D_ROM (the decrypted OoT3D ROM) for the link zars.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from ctr_romfs import CtrRom  # noqa: E402
import zar  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "Shipwright/soh/src/zelda3d/tables/zelda3d_player_animmap.inc")
SRC = os.path.join(REPO, "Shipwright/soh/src")

# Two player-anim namespaces exist in the N64 decomp and BOTH reach the live skelAnime:
#   gPlayerAnim_link_*   — shared/adult clips (sAgeProperties[0], and most of sAgeProperties[1])
#   gPlayerAnim_clink_*  — CHILD-ONLY clips (sAgeProperties[1]: the whole ladder-climb family, the
#                          child cutscene demos, op3, defense_ALL). Grezzo's twins are the same
#                          names under a `cl_` prefix (cl_nml_climb_upL, cl_dm_get1, cl_op3_wait1).
# Missing the `clink_` namespace here is what made CHILD Link play the IDLE clip while climbing a
# ladder (#201 c2): sAgeProperties[1].unk_A4/A8/AC/C4/CC all point at gPlayerAnim_clink_normal_climb_*,
# none of which the old `gPlayerAnim_link_` -only scan could ever see, so every one of them resolved
# to NULL -> ZELDA3D_LINK_IDLE_CSAB.
PREFIX = "gPlayerAnim_link_"
CHILD_PREFIX = "gPlayerAnim_clink_"

# Leading category token: N64 decomp name -> OoT3D CSAB prefix.
CATEGORY = [
    ("normal_", "nml_"),
    ("fighter_", "ft_"),
    ("demo_", "dm_"),
    ("anchor_", "ac_"),
    ("bottle_", "bt_"),
    ("hammer_", "hm_"),
    ("magic_", "mg_"),
    ("swimer_", "sw_"),
    # bow_, boom_, fishing_, hook_, uma_, silver_, derth_, last_ keep their prefix.
]

# Substring rewrites applied AFTER the category substitution (Grezzo's naming tweaks).
SUBS = [
    ("finsh", "fin"),               # *_kiru_finsh_* -> *_kiru_fin_*
    ("normal_kiru", "nml_kiru"),    # fighter_normal_kiru -> ft_nml_kiru
    ("Lnormal", "Lnml"),            # fighter_Lnormal_kiru -> ft_Lnml_kiru
    ("power", "pow"),               # *_power_*/_Lpower_* -> *_pow_*/_Lpow_*
    ("pierce_kiru", "pierce"),      # fighter_pierce_kiru -> ft_pierce
    ("down_slope_slip", "down_slip"),
    ("up_slope_slip", "up_slip"),
    ("normal2", "nml2"),            # normal_normal2fighter -> nml_nml2fighter
    ("free2fighter_free", "free2ft_free"),
    ("drink_demo", "drink_dm"),     # bottle_drink_demo_* -> bt_drink_dm_*
    ("doorA_link", "doorA_lk"),     # demo_doorA_link -> dm_doorA_lk
    ("doorB_link", "doorB_lk"),
    ("kakeyori", "runup"),          # demo_kakeyori_* -> dm_runup_*
    ("nml2fighter_free", "nml2ft_free"),  # normal_normal2fighter_free -> nml_nml2ft_free
    ("child_tunnel", "cl_nml_tunnel"),    # child_tunnel_start/end -> cl_nml_tunnel_* (child crawl)
    ("_demo", "_dm"),               # SUFFIX demos: hatto/okiru/shagamu_demo -> *_dm (demo_ prefix
                                    # is handled by CATEGORY above; this only hits the trailing form)
]

# Exact n64base -> CSAB overrides, applied BEFORE the rule pipeline. For ground-truth-verified cases
# the rewrite rules can't express (e.g. Grezzo kept the full original name for one CSAB). Same
# philosophy as the En_Ko/En_Hy CSAB overrides — verified, not guessed.
OVERRIDES = {
    # Grezzo kept the verbatim N64 name for this transition CSAB (no nml_ rename); exists both ages.
    "gPlayerAnim_link_normal_free2freeB": "link_normal_free2freeB",

    # --- jump_climb family: OoT3D has NO jump_climb CSAB and REUSES the hang family. ---
    # Ground truth = OoT3D player anim-group table @ fp=0x53a5f8 (oot3d-decomp): the jump_climb_*
    # groups (hold @+0x468, wait @+0x480, up @+0x498) resolve to animIds 143-148 = nml_hang_*,
    # with the modelAnimType free/non-free split matching N64 sPlayerAnimGroups exactly. The
    # jump_climb_up_free=145 element cross-validates against the immediate in func_8083AA10's
    # wall-vault branch (0x4be634, animId 0x91). See docs/player_anim_states.md.
    "gPlayerAnim_link_normal_jump_climb_hold":      "nml_hang_hold",       # animId 148
    "gPlayerAnim_link_normal_jump_climb_hold_free": "nml_hang_hold_free",  # animId 147
    "gPlayerAnim_link_normal_jump_climb_wait":      "nml_hang_wait",       # animId 144
    "gPlayerAnim_link_normal_jump_climb_wait_free": "nml_hang_wait_free",  # animId 143
    "gPlayerAnim_link_normal_jump_climb_up":        "nml_hang_up",         # animId 146
    "gPlayerAnim_link_normal_jump_climb_up_free":   "nml_hang_up_free",    # animId 145

    # --- run-jump-into-water family: OoT3D reuses the run_dive family. ---
    # Ground truth = decomp immediates: func_8083AA10 water-dive branch (0x1cfd74) passes animId
    # 0x69=105 (nml_run_dive) to func_808389E8 with 6.0f morph after the WaterBox+50.0f check;
    # Player_Action_80844A44 (0x21e4e8) loops animId 0x68=104 (nml_run_dive_wait). Matches N64
    # z_player.c:5809 / :9975 exactly. See docs/player_anim_states.md.
    "gPlayerAnim_link_normal_run_jump_water_fall":      "nml_run_dive",       # animId 105
    "gPlayerAnim_link_normal_run_jump_water_fall_wait": "nml_run_dive_wait",  # animId 104
}


def csab_basenames(rom, zarpath):
    z = zar.Zar(rom.read(rom.get(zarpath)))
    return set(os.path.basename(f.name)[:-5] for f in z.files if f.name.endswith(".csab"))


def resolve(n64name):
    if n64name in OVERRIDES:
        return OVERRIDES[n64name]
    child_only = n64name.startswith(CHILD_PREFIX)
    s = n64name[len(CHILD_PREFIX):] if child_only else n64name[len(PREFIX):]
    for k, v in CATEGORY:
        if s.startswith(k):
            s = v + s[len(k):]
            break
    for a, b in SUBS:
        s = s.replace(a, b)
    # The child namespace keeps the same category/token rewrites, just under Grezzo's `cl_` prefix.
    return ("cl_" + s) if child_only else s


def resolves_in(ageset, base):
    """A CSAB base resolves in an age zar if the file exists verbatim OR under the child 'cl_' prefix
    (Grezzo prefixes some child-age anims with cl_; the runtime getCsab has the matching fallback)."""
    return base in ageset or ("cl_" + base) in ageset


def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    boy = csab_basenames(rom, "/actor/zelda_link_boy_new.zar")
    child = csab_basenames(rom, "/actor/zelda_link_child_new.zar")
    shared = boy & child  # one basename table must resolve for either age

    out = subprocess.check_output(
        ["grep", "-rhoE", "gPlayerAnim_c?link_[a-zA-Z0-9_]*", SRC]
    ).decode().split()
    names = sorted(set(out))

    rows = []
    dropped = []
    for n in names:
        if not re.fullmatch(r"gPlayerAnim_c?link_[A-Za-z0-9_]+", n):
            continue
        c = resolve(n)
        if n.startswith(CHILD_PREFIX):
            # A `clink_` clip is only ever selected on CHILD Link (sAgeProperties[1]), so requiring
            # it in the boy zar too would be wrong — and would drop cl_dm_Tbox_open, the one CSAB
            # Grezzo ships child-only. Validate against the child zar alone.
            ok = resolves_in(child, c)
        else:
            # Emit if the CSAB resolves in BOTH ages (verbatim or via the child cl_ prefix that the
            # runtime getCsab fallback handles) — so age-specific names like dm_Tbox_open are
            # covered by a single basename entry.
            ok = resolves_in(boy, c) and resolves_in(child, c)
        if ok:
            rows.append((n, c))
        else:
            dropped.append((n, c))

    lines = []
    lines.append("// GENERATED by tools/gen_player_animmap.py — do not edit by hand.")
    lines.append("// PLAYER (Link) N64-anim -> OoT3D-link-CSAB map. Key = the live")
    lines.append("// Player.skelAnime.animation basename (gPlayerAnim_link_* AND the CHILD-only")
    lines.append("// gPlayerAnim_clink_* namespace -> Grezzo's cl_* twins); value = the CSAB base")
    lines.append("// played on the *_new link rig (resolved to boy/anim or child/anim by age). A")
    lines.append("// shared anim is emitted only if its CSAB exists in BOTH age zars (a clink_ one")
    lines.append("// only needs the child zar); an unlisted anim falls")
    lines.append("// back to ZELDA3D_LINK_IDLE_CSAB at runtime. Edit the rewrite rules in the generator,")
    lines.append("// not this file. See memory zelda3d-link-player-path.")
    lines.append("static const Zelda3dPlayerAnimMap kPlayerAnimMap[] = {")
    for n, c in rows:
        lines.append('    { "%s", "%s" },' % (n, c))
    lines.append("};")
    lines.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(lines))

    print("wrote %s: %d entries (%d N64 anims, %d unmapped -> idle fallback)"
          % (os.path.relpath(OUT, REPO), len(rows), len(names), len(dropped)))

    # Coverage audit: dump the UNMAPPED anims (fall back to idle) + the CSAB the rewrite rules tried
    # but that isn't present in both age zars, so the 3DS-Link port gaps are visible at a glance.
    cov = os.path.join(REPO, "scratch", "link_anim_coverage.txt")
    os.makedirs(os.path.dirname(cov), exist_ok=True)
    with open(cov, "w") as f:
        f.write("# UNMAPPED player anims (fall back to idle in the 3DS-Link path)\n")
        f.write("# n64base\t-> attempted CSAB (not present in both age zars)\n")
        for n, c in sorted(dropped):
            f.write("%s\t-> %s\n" % (n, c))
    print("coverage gaps -> %s" % os.path.relpath(cov, REPO))


if __name__ == "__main__":
    main()
