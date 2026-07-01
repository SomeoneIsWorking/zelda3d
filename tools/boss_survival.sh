#!/usr/bin/env bash
# boss_survival.sh — post-load survival + boss-spawn regression net for the 10 boss arenas.
#
# WHY: tools/scene_crashscan.py only covers a ~4s LOAD window. This drives each boss arena
# for ~14s so the boss intro + boss AI actually run, catching post-load/anim/AI crashes the
# load scan misses (the class that #114 Deku-Tree En_Box belonged to). Also confirms the boss
# actor itself spawned, not just an empty arena.
#
# KEY FACTS (verified 2026-06-24, all 10 ALIVE + boss spawns, no crash):
#  * Boss arenas MUST be entered via PLAIN dev-warp (cutsceneIndex=0) — `zelda3d_game.sh restart
#    <ent>`. That spawns BOTH Link and the boss (boss = ACTORCAT_BOSS = `cat=9` in REPL `actors`;
#    do NOT misread cat=9 as a Bg prop). e.g. Boss_Dodongo id=0x27 cat=9 at King Dodongo 0x40B.
#  * Do NOT use `cswarp <boss> 0xfff0` for boss rooms: boss rooms are NOT cutscene-only scenes
#    (the intro is triggered in-scene AFTER a normal entry). Forcing cutsceneIndex 0xfff0+ selects
#    a setup layer whose start-position list has no ACTOR_PLAYER -> func_800304DC null-derefs the
#    empty PLAYER list -> SIGSEGV. This is the SAME degenerate-setup FALSE POSITIVE as Chamber of
#    Sages (handoff session k); NOT a Zelda3D bug. Don't file, don't bandaid.
#  * Bosses spawn IDLE (speedXZ=0) and arenas are dim; observing boss model/AI parity needs an
#    AI trigger + brighter framing (future tooling), not just a warp.
#
# Boss-arena ENTRANCE indices: Deku/Gohma 0x40F, Dodongo/KingDodongo 0x40B, Jabu/Barinade 0x301,
#  Forest/PhantomGanon 0x00C, Fire/Volvagia 0x305, Water/Morpha 0x417, Spirit/Twinrova 0x08D,
#  Shadow/BongoBongo 0x413, Ganondorf 0x41F, Ganon 0x517.
set -u
cd "$(dirname "$0")/.."
LOG=scratch/logs/run.log
DWELL=${DWELL:-14}
declare -A BOSS=(
  [0x40F]="Gohma_DekuBoss" [0x40B]="KingDodongo_DodongoBoss" [0x301]="Barinade_JabuBoss"
  [0x00C]="PhantomGanon_ForestBoss" [0x305]="Volvagia_FireBoss" [0x417]="Morpha_WaterBoss"
  [0x08D]="Twinrova_SpiritBoss" [0x413]="BongoBongo_ShadowBoss" [0x41F]="Ganondorf_Boss"
  [0x517]="Ganon_Boss"
)
for ent in 0x40F 0x40B 0x301 0x00C 0x305 0x417 0x08D 0x413 0x41F 0x517; do
  name=${BOSS[$ent]}
  ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart "$ent" 0x6000 >/dev/null 2>&1
  sleep "$DWELL"
  out=$(python3 tools/zelda3d_repl.py cmd "actors" 2>&1)
  boss=$(echo "$out" | grep -c "cat=9")
  crash=$(grep -anE "FATAL signal|Signal: 11|terminate called|Assertion" "$LOG" 2>/dev/null | tail -2)
  if [ -n "$crash" ]; then
    echo "$ent $name => CRASH"; echo "    $crash"
  else
    echo "$ent $name => ALIVE (boss cat=9 actors: $boss)"
  fi
done
