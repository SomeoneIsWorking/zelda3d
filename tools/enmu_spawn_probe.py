#!/usr/bin/env python3
"""enmu_spawn_probe.py — probe every Market-family scene for ACTOR_EN_MU (0x1AD).

Warps SoH3D to each candidate entrance, runs actorsnear at wide radius, and reports
whether 0x1AD (ovl_En_Mu, "Haggling townspeople") is present. Derives the definitive
spawn list without needing to parse OTR scene actor tables.

Usage: tools/enmu_spawn_probe.py [ent_hex...]
    (default: full Market family)
"""
import os, re, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Market-family entrances: enter each scene once at day-time (0x8001) then night (0xE000).
DEFAULT_ENTRANCES = [
    ("0x33",  "SCENE_MARKET_ENTRANCE_DAY"),
    ("0x7E",  "SCENE_MARKET_GUARD_HOUSE"),
    ("0xAD",  "SCENE_BACK_ALLEY_DAY"),
    ("0xAE",  "SCENE_BACK_ALLEY_NIGHT"),
    ("0xB1",  "SCENE_MARKET_DAY"),
    ("0xB7",  "SCENE_BAZAAR"),
    ("0x43B", "SCENE_BACK_ALLEY_HOUSE"),
]

TARGET_ID = 0x1AD


def probe(ent, day="0x8001"):
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", str(ent), str(day)],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(4)
    r = subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", "actorsnear 10000"],
        cwd=REPO, capture_output=True, text=True, timeout=15,
    )
    scene = None
    p = subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", "posinfo"],
        cwd=REPO, capture_output=True, text=True, timeout=10,
    )
    m = re.search(r"scene=0x([0-9a-fA-F]+)", p.stdout)
    if m:
        scene = int(m.group(1), 16)
    count = 0
    matches = []
    for line in r.stdout.splitlines():
        mm = re.search(r"id=0x([0-9A-Fa-f]+)", line)
        if mm and int(mm.group(1), 16) == TARGET_ID:
            count += 1
            matches.append(line.strip())
    return scene, count, matches


def main():
    ents = sys.argv[1:] or [e for e, _ in DEFAULT_ENTRANCES]
    labels = {e: label for e, label in DEFAULT_ENTRANCES}
    print(f"# probing ACTOR_EN_MU (0x{TARGET_ID:03X}) across {len(ents)} entrances")
    total_hits = 0
    for ent in ents:
        for day in ("0x8001", "0xE000"):
            scene, count, matches = probe(ent, day)
            tag = "DAY" if day == "0x8001" else "NIGHT"
            label = labels.get(ent, "?")
            scene_s = f"0x{scene:02X}" if scene is not None else "?"
            print(f"  ent={ent:>6} {tag:>5} scene={scene_s} {label:<32} EN_MU count={count}")
            for m in matches:
                print(f"    {m}")
            total_hits += count
    print(f"# total EN_MU instances observed: {total_hits}")
    sys.exit(0 if total_hits == 0 else 1)


if __name__ == "__main__":
    main()
