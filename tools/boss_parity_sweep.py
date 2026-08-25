#!/usr/bin/env python3
"""boss_parity_sweep.py — render-parity audit of the 10 boss arenas (Zelda3D #115 family).

For each boss arena it: plain-warps in (the ONLY correct entry — see boss_survival.py), waits for
the boss intro to spawn the boss, enables the OoT3D Link body, brightens the (dim) arena, locates
the boss actor (ACTORCAT_BOSS cat=9, else the nearest large enemy), teleports Link next to the boss
so it falls inside `actorsnear`'s radius, and records the boss's RENDER STATUS from the `actorsnear`
coverage column:
    AUTO:/actor/<zar>[ (skin)] / TABLE:...  -> renders as the OoT3D model (PARITY OK)
    --N64--                                  -> no object->ZAR mapping = still N64 (PARITY GAP)
It also frames + screenshots the boss for visual review.

Deliverable: scratch/boss_parity/<name>.txt (actors + actorsnear dump) + .png (framed boss), and a
printed summary table of boss -> render status. Bosses are skinned actors; whether each is covered
by the auto skinned-replacement path is exactly what this answers.

Usage: python3 tools/boss_parity_sweep.py
"""
import os, re, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "scratch/boss_parity")
os.makedirs(OUT, exist_ok=True)

# entrance -> (display name, expected OoT3D boss zar substring for identification)
ARENAS = [
    ("0x40F", "Gohma_Deku",        "goma"),
    ("0x40B", "KingDodongo",       "kdodongo"),
    ("0x301", "Barinade_Jabu",     "bw"),       # Boss_Va
    ("0x00C", "PhantomGanon_Forest","fhg"),
    ("0x305", "Volvagia_Fire",     "fd"),
    ("0x417", "Morpha_Water",      "mo"),
    ("0x08D", "Twinrova_Spirit",   "tw"),
    ("0x413", "BongoBongo_Shadow", "sst"),
    ("0x41F", "Ganondorf",         "ganon"),
    ("0x517", "Ganon",             "ganon2"),
]


def repl(cmd, timeout=25):
    try:
        r = subprocess.run(["tools/zelda3d_repl.py", "cmd", cmd], cwd=REPO,
                           capture_output=True, text=True, timeout=timeout)
        return (r.stdout or "") + (r.stderr or "")
    except Exception as e:
        return f"<repl error: {e}>"


def shot(name, timeout=25):
    subprocess.run(["tools/zelda3d_repl.py", "shot", name], cwd=REPO,
                   capture_output=True, text=True, timeout=timeout)


def find_boss(actors_txt):
    """Return (id, x,y,z) of the boss: first cat=9, else nearest non-player non-prop enemy."""
    best = None
    for ln in actors_txt.splitlines():
        m = re.search(r"id=(0x[0-9a-fA-F]+).*cat=(\d+).*pos=\(([-\d]+),([-\d]+),([-\d]+)\).*dist=(\d+)", ln)
        if not m:
            continue
        aid, cat, x, y, z, dist = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))
        if cat == 9:
            return (aid, x, y, z)  # definitive boss
        if cat in (1, 4) and (best is None or dist < best[4]):  # enemy/npc fallback by nearest
            best = (aid, x, y, z, dist)
    return best[:4] if best else None


def main():
    results = []
    for ent, name, zar_hint in ARENAS:
        print(f"\n=== {name} ({ent}) ===", flush=True)
        subprocess.run([sys.executable, "tools/zelda3d_game.py", "restart", ent, "0x6000"], cwd=REPO,
                       capture_output=True, text=True, env={**os.environ, "ZELDA3D_HEADLESS": "1"})
        time.sleep(13)  # boss intro + spawn
        repl("link 1")
        repl("worldamb 0.45")
        actors = repl("actors")
        boss = find_boss(actors)
        status = "NO-BOSS-FOUND"
        near = ""
        if boss:
            bid, x, y, z = boss
            repl(f"tp {x} {y} {z}")   # bring Link to the boss so it's in actorsnear radius
            time.sleep(0.5)
            repl(f"asel {bid}")
            repl("afreeze 1")
            repl("acam 500 0")
            time.sleep(0.5)
            near = repl("actorsnear")
            # the boss render-status line: prefer the (skin) AUTO/TABLE enemy entry or the hint zar
            boss_line = ""
            for ln in near.splitlines():
                low = ln.lower()
                if zar_hint and zar_hint in low:
                    boss_line = ln.strip(); break
            if not boss_line:
                # fallback: the nearest (skin) entry, else any --N64-- enemy
                for ln in near.splitlines():
                    if "(skin)" in ln:
                        boss_line = ln.strip(); break
            if boss_line:
                if "--N64--" in boss_line:
                    status = "N64-GAP"
                elif "AUTO:" in boss_line or "TABLE:" in boss_line:
                    status = "OoT3D-OK"
                status += "  | " + boss_line
            else:
                status = "BOSS-NOT-IN-NEAR  | " + (near.splitlines()[0] if near else "")
            shot(f"../boss_parity/{name}")
        with open(os.path.join(OUT, f"{name}.txt"), "w") as f:
            f.write(f"=== {name} {ent} ===\nboss={boss}\n--- actors ---\n{actors}\n--- actorsnear ---\n{near}\n")
        results.append((name, ent, status))
        print(f"  -> {status}", flush=True)

    print("\n\n===== BOSS PARITY SUMMARY =====")
    for name, ent, status in results:
        print(f"  {name:22} {ent}  {status}")


if __name__ == "__main__":
    main()
