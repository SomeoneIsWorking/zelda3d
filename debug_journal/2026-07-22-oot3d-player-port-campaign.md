# 2026-07-22 — "Port 3DS Link properly": the divergence sweep and what it actually produced

User directive: port 3DS Link as a whole into zelda3d.

## The finding that shaped the work: a wholesale port is the WRONG shape

The project had already established this and it is worth restating because the instinct is to
re-decompile `z_player.cpp` wholesale. Rings 1–4 covered 730 OoT3D functions:

    ring-1      220 funcs   144 FAITHFUL / 33 DIVERGENT / 41 UNMATCHED
    rings 2–4   510 funcs   216 FAITHFUL / 18 DIVERGENT / 264 UNMATCHED

Every Link bug ever chased to ground here (#86 run-off-edge, #79 climb-teleport, #6/#85/#9
carry-placement) turned out **byte-exact to N64**. Grezzo did not rewrite Link. So "port Link
properly" reduces to porting the catalogued DIVERGENCES plus integration correctness — not to
copying code we already have.

## The sweep (Workflow `oot3d-player-port-specs`, 65 agents, 4.8M tokens, ~24 min)

One agent per catalogued player divergence: read the 3DS decomp, align to the N64 twin, isolate
Grezzo's change, resolve EVERY constant out of `code.bin`, emit an implementable spec. Then a
second agent per implementable spec, instructed to **reject by default**, re-reading constants and
checking the target code exists where claimed. Engine items (Message_Decode ×2, Audio_ProcessSeqCmd)
were excluded as not-Link.

    41 specs  ->  14 READY · 10 REJECTED · 9 NOT-DIVERGENT · 9 BLOCKED
    (corrected below: one "blocked" is really a negative result -> 10 not-divergent / 8 blocked)

**The verify stage rejected 42% of implementable specs.** Rejection reasons were substantive, not
style: a missed second divergence inside the very branch being edited; inlining noise mistaken for a
change; a load-bearing defect in a proposed refactor; wrong flag identification. Several verifiers
disassembled the ARM directly. Do not skip this stage on future sweeps.

## THE CATALOGUE IS A HYPOTHESIS, NOT A WORK LIST

`divergence_map.md` was wrong often enough that every row must be confirmed against the decomp before
acting. Nine rows are not divergences at all (decompiler inlining of N64's own code). Worse, several
name the WRONG FUNCTION — an implementer working straight from the table would have edited unrelated
code:

| row said | actually is |
|---|---|
| `0x3438a4` Player_InitItemActionWithAnim | **Message_StartOcarina** (its "renumbered item ids" are OcarinaAction ids) |
| `0x3523dc` first-person/gyro cam toggle | **Audio_OcaSetInstrument** |
| `0x35da3c` footstep variant index | **Scene_SetTransitionForNextEntrance** |
| `0x34b17c` func_8083CF5C floor/gravity | **func_8084B000** water buoyancy |
| `0x34b288` run/walk playspeed setter | **func_8084B158** SWIM/DIVE playspeed setter |

All five corrected in `oot3d-decomp/docs/divergence_map.md` this session.

## The 14 ported (all landed, all build clean)

| what | where |
|---|---|
| jump sword-clank SFX (NA_SE_PL_JUMP_METAL, B-sword only, before the jump SFX, raw) | z_player.c |
| ITEM_LENS -> ITEM_NONE obtainability branch | z_parameter.c |
| plane degeneracy epsilon 0.008f -> 0.00008f (+ debug print dropped) | sys_math3d.c |
| underwater talk/Navi veto unless Iron Boots | z_player.c |
| Iron-Boots-in-water blocks ALL A interactions | z_player.c |
| Master Sword regive guarded by not-already-owned | z_play.c |
| water buoyancy rescaled 2/3 + anim-gated Iron-Boots floor (-6.0 during swim-wait) | z_player.c |
| dead-player control stick produces zero target speed | z_player.c |
| first-person entry resets idle anim out of Z-target side walk (+ actionVar2 13->12) | z_player.c |
| camera refusal error beep DELETED; modeChangeFlags case 1 SFX DELETED | z_camera.c |
| Zora-tunic + Iron-Boots underwater accel/yaw case; land-boots REG(45) exemption; sBootData[0][7] 350->434 | z_player_lib.c |
| sword-trail tip trim (Master 0.85 / Kokiri 0.65, blure only — collider keeps full tip) | NEW `zelda3d/player/zelda3d_sword_trail.{cpp,h}` |
| in-water item-button allowance for put-away/hookshot upper actions | z_player.c |
| (Message_StartOcarina) — doc correction only, no code | divergence_map.md |

### Deliberately NOT ported (recorded so they are not mistaken for oversights)
- **Sword-trail MATERIALS** (11 entries @ VA 0x004dc3c4): those are 3DS resource slots in the blure's
  GAR, not SoH `TrailType` values. Landing them without the 3DS materials wired up produces
  default/white trails — a straight regression of SoH's existing enhancement.
- **Touch-UI gates** (variant bit 0x1000000, six-slot button scan): meaningless on PC.
- **Soft-floor speed-cap gate** (variant bit 0x100): its own spec says it must not land standalone.
- **SoH's FreeLook CVar block** in Camera_ChangeModeFlags: a SoH feature, not an OoT3D divergence.

### One addition that is NOT ported behavior, flagged in-code
`Interface_LoadItemIcon1` in the Master Sword regive. `Item_Give` used to refresh the B-button icon;
the 3DS inline does not, because its HUD path differs. Kept so SoH's HUD stays consistent. Labelled,
because in six months it would otherwise read as faithful porting.

## Verification status (HONEST)

- **Smoke test PASSES**: boots, plays, Kokiri Forest renders, no crash with all 14 in.
- **Targeted oracle sweep — all MATCH**: jump, roll, attack, swim_surface, swim_dive.
- **CAVEAT that matters:** the swim states were MATCH *before* the buoyancy change too. So this shows
  NO REGRESSION; it does NOT prove the buoyancy port improved fidelity — the sweep's tolerance may
  accommodate both constant sets. Proving improvement needs a frame-level velocity trace vs the
  oracle, not the sweep's pass/fail.
- **FULL STATE MATRIX RE-MEASURED against the OoT3D oracle — no regressions from the 14 ports.**
  All 25 states checked fresh this session and every one matches its pre-port baseline:

      MATCH (21): model_render, backwalk, sidestep_l, sidestep_r, turn_in_place, jump, roll,
                  attack, attack_combo, shield, item_bottle_use, pickup_carry, throw, climb_hang,
                  climb_updown, swim_surface, swim_dive, mount_dismount, ztarget,
                  damage_knockback, getitem_pose, death
      DIVERGENT (1): idle          <- pre-existing, unchanged by the ports
      UNREACHABLE (2): walk, run   <- pre-existing, unchanged by the ports

  **SECOND, HARDER CORRECTION (2026-07-22, later): "MATCH the oracle" is WRONG — it is match vs
  DECOMP.** Every forcestate row carries `gt="decomp"` and its metric is "CSAB-family substring match
  vs oot3d-decomp action-func anim group". The expectation comes from the DECOMP CORPUS, not from
  querying a live OoT3D. I described these as oracle comparisons many times this session; they are
  not.

  What exposed it: trying to capture an oracle screenshot. The embedded oracle boots,
  `OracleSession.boot()` returns ok=True, `snapshot` succeeds — and the frame is OoT3D's TITLE SCREEN
  (a sky gradient), with `az_linkanim` and `az_playerpos` both answering "no Player actor" even after
  boot's warp to Kokiri and 240 settle frames. So the live oracle never reaches gameplay at all in
  these runs, which is only possible *because* the sweep never asks it anything.

  Consequences: the sweep is a regression check against a static expectation corpus — still useful,
  and the no-regression result stands — but it is NOT evidence of parity with OoT3D. Any claim of
  the form "verified against the oracle" in this session's earlier notes should be read as "verified
  against the decomp expectation".

  Open: why the oracle stalls on the title. boot()'s tap schedule (3x START + up to 40x A, checking
  `poll_playstate`) reports populated, yet no Player actor materialises. `poll_playstate` returning ok
  is evidently NOT sufficient evidence of gameplay — that is the same class of mistake as trusting
  `link_sweep.py list` after a killed sweep.

  **SCOPE CORRECTION — what this sweep actually measures (I overstated it earlier).** Every row's
  metric is a *CSAB-family substring match* — i.e. WHICH ANIMATION the state selects — and every
  row's `pose_verdict` is `N/A` ("no live pose oracle for this state (selection-only)"). It does not
  measure motion, velocity, or physics at all. Concretely, `swim_surface` passes by comparing
  `soh=sw_swim_wait` against `expect=sw_swim`.

  Consequences, stated plainly:
  - "All 25 states MATCH" means **no ANIM-SELECTION regressions** from the 14 ports. That is a real
    and useful result, but it is narrower than it sounds.
  - The buoyancy rescale is **invisible to this harness by construction**. It changes velocity
    constants, and nothing here samples velocity. So the buoyancy port remains UNVERIFIED by any
    measurement taken so far — the earlier "swim states still MATCH" observation was never capable
    of confirming or refuting it.
  - Same applies to the sword-trail trim (geometry), the plane epsilon (numeric), and the two
    camera SFX deletions (audio): none is observable through anim selection.

  **RECIPE for actually verifying buoyancy (worked out here; nothing blocking left but the doing).**
  Three facts that cost time to establish — start from them:

  1. **The sweep drives force-states UNDER FREEZE** (`link_sweep.py:723` -> `parity_state_sweep`
     `linkstate <s>` under freeze). Frozen physics cannot express a velocity change, which is the
     structural reason this harness is selection-only. A buoyancy trace must force the state and then
     let frames run UNFROZEN.
  2. **`linkstate` / `warp` are absent from the REPL unless the game is started with
     `ZELDA3D_LINK=1`.** They are not in the model-REPL's own `cmds:` list, so a bare
     `zelda3d_repl.py cmd linkstate` answers `? 'linkstate'` and looks like the command does not
     exist. It does; the instance was just started without the env var.
  3. **The oracle already exposes what is needed**: `az_playerpos` and `az_playerinfo` (plus
     `az_run_until`, `az_ticks`) in `tools/soh3d_harness/main.cpp`. `az_linkanim` also returns
     `speedXZ`, so the oracle side needs no new command for a vertical trace.

  Procedure: start SoH with `ZELDA3D_LINK=1`; `linkstate swim` WITHOUT freeze; sample `posinfo`
  per frame for ~60 frames to get posY(t); drive the oracle to the same state and sample
  `az_playerpos` per tick; feed both to `tools/motion_parity.py` (it already compares posY).
  Expected discriminator: the ported constants (rise accel 0.06666667, damping -0.2, sink bump
  0.6666667) vs the N64 set (0.1, -0.3, 1.0) produce visibly different rise curves, so the trace
  distinguishes them even before oracle comparison — that alone confirms the edit is live and
  correctly signed.

  **ATTEMPTED THE RECIPE — BLOCKED ON HARNESS CONTROL (2026-07-22). Do not retry as-is.**
  Corrections to my own earlier notes first, both of which were wrong:
  - "The OoT REPL has no tp/warp" is FALSE. It has ~150 commands (authoritative list:
    `grep -ohE 'strcmp\(cmd, "[a-z0-9_]+"\)' soh/src/zelda3d/repl/zelda3d_repl.cpp`). The short
    `cmds:` list printed on an unknown command is only the model-REPL subset — do not infer from it.
  - "`linkstate` needs ZELDA3D_LINK=1" is FALSE. It takes an ARGUMENT (`sscanf "%*s %63s" == 1`), so
    a bare `linkstate` falls through to the unknown-command handler and looks missing. `linkstate
    swim` works fine.

  I claimed three blockers here. **TWO WERE MY OWN TESTING ERRORS** — corrected, because filing
  false tooling bugs is worse than filing none:
  - ~~`asample` cannot sample the player~~ — **FALSE.** `asel link` (or `asel player`) selects Link
    explicitly; its own docstring says the player is "normally excluded" from the id scan. My
    `asel 0` failed because id 0 is not in the scan set, not because the capability is missing.
    `asel link` -> `pos=(-1802,-1023,860)`, and `asample` can then stream him.
  - ~~`tp` does not move Link~~ — **FALSE.** Tested with small offsets from a known-good position:
    +Z 60 moved him (z 860 -> 920), while +X 60 hit a wall and +Y 40 just fell back under gravity.
    My six Lake Hylia targets were simply all invalid/blocked positions. `tp` then walked him
    hundreds of units across the map fine.

  **The one REAL blocker stands:** `linkstate swim` sets the swim animation + state flags while Link
  stands on land, so `func_8084B000` never runs — posY held at -79.0 across 24 samples. That is the
  documented force-hook gate-bypass debt: it fakes the state instead of entering it.

  Still-open practical problem (not a tooling defect): I could not find water in Lake Hylia. Link
  descends smoothly to y=-1234 walking the basin with `st1` never showing PLAYER_STATE1_IN_WATER
  (0x8000000), which is consistent with this debug save having the lake DRAINED. Next attempt should
  either use a save/scene where water is guaranteed (Zora's Domain, or Lake Hylia with the water
  restored) or query the water box directly rather than hunting coordinates.

  **RESOLVED — BUOYANCY PORT VERIFIED LIVE (2026-07-22).** The blocker was never the tooling; it was
  that Lake Hylia is DRAINED in this save. Water exists in **Zora's Domain** (`warp 0x109`), found by
  grid-probing `st1 & PLAYER_STATE1_IN_WATER` (0x8000000): first hit at (-1094, -2, -340).

  Recipe that works, end to end:

      warp 0x109                      # Zora's Domain — water independent of the lake-drain flag
      tp -1094 -160 -340              # push Link well under the surface
      asel link                       # the player IS selectable; `asel 0` is not the way
      asample 90 scratch/motion/swim_soh.csv

  Result (`scratch/motion/swim_buoyancy_verified.csv`, 90 frames): the per-frame velY increment while
  rising is **0.0666–0.0667**, i.e. exactly the ported `0.06666667f`. N64's constant is `0.1f`.

  The damping term confirms it INDEPENDENTLY. Predicted `dVelY = (-velY * 0.2) + 0.0667` vs observed:

      velY -0.3709 -> predicted 0.1409, observed 0.1409   (N64 would be 0.2113)
      velY -0.2300 -> predicted 0.1127, observed 0.1126   (N64 would be 0.1690)
      velY -0.1174 -> predicted 0.0902, observed 0.0902   (N64 would be 0.1352)
      velY -0.0272 -> predicted 0.0721, observed 0.0721   (N64 would be 0.1082)
      velY +0.0449 -> predicted 0.0667, observed 0.0667   (N64 would be 0.1000)

  Every point matches the ported constants to four decimals and none is close to the N64 set. The
  `phi_f14 = 2.0f` rise clamp also appears exactly (velY saturates at 2.0). So `func_8084B000` is
  live, correctly signed, and running the OoT3D values — the first port in this campaign confirmed by
  MEASUREMENT rather than by build-success plus anim-selection parity.

  **ORACLE A/B ATTEMPTED — blocked, and worth knowing why.** Upgrading this from "the constants are
  live" to a true parity claim needs the same trace on the OoT3D side. The oracle boots fine
  (`LS.OracleSession().boot()` -> ok) and accepts `warp 0x0109`, but every subsequent
  `az_playerpos` / `az_playerinfo` answers **"no Player actor"**, across 10 walk steps and ~500
  frames. `warp 0xEE` (Kokiri) works in link_sweep's own use, so the failure is specific to warping
  the ORACLE to Zora's Domain — plausibly an adult-only/flag-gated entrance that lands without a
  populated player, or a transition state the actor-chain walk cannot resolve.

  Also note the oracle has NO `tp`: placing its Link requires analog driving, which is why this is
  harder than the SoH side. Next attempt should either pick a water scene the oracle can reach from
  its existing save state, or capture a save state already in water and `loadstate` it directly —
  rather than warping and hoping the player actor survives.

  METHOD NOTE: the one-shot full sweep is unreliable — it was killed by a 30-minute timeout after
  only 4 states, and `link_sweep.py list` then still showed the STALE baseline, which is an easy way
  to report a verification that never ran. Run it in `--only` batches of ~8 (~24s/state) instead.

  FLAKINESS NOTE, worth keeping: `ztarget` came back UNREACHABLE in a batch and MATCH when re-run
  alone. That looked like a real regression — `Player_ActionHandler_13` is exactly the Z-target /
  first-person entry path this session changed — so it was re-tested rather than written off. A
  single UNREACHABLE from this harness is not evidence; confirm by re-running the state alone.
- Individually unexercised: the audio deletions (confirm by ear), the Lens branch (needs a Lens
  pickup), the Iron-Boots vetoes, the dead-input gate.

### Blast radius worth re-measuring
`sBootData[0][7]` 350 -> 434 feeds `REG(38)`, the landing/roll pitch term, for the DEFAULT adult
boots — ordinary adult movement everywhere, not an edge case. If a parity-map row covering adult
landing/roll is CLOSED, this reopens it.

### One load-bearing inference
The Zora-tunic test assumes OoT3D `Player+0x1a4` is `currentTunic` (3DS Actor size 0x1a4 mirroring N64
0x14C; corroborated by `+0x1a7` == currentBoots). Not directly observed. One live dump of that byte
while wearing a known tunic settles it; if it were `currentShield`, the constant 2 would mean
PLAYER_SHIELD_HYLIAN and the code would look right while doing the wrong thing.

## Blocked-list bookkeeping correction

`0x0037547c` is tallied under "blocked" only because `implementable=false`, but its own spec is
explicit that it is a **completed negative result**: the function is OoT3D's SFX request builder, it
is not player code, and it contains no region gate — there is simply no player-side change to make.
So the honest split is **10 not-divergent / 8 genuinely blocked**, not 9/9.

## The 8 genuinely blocked items (need more RE before any port)
1. `0x002bf814` auto-aim acquisition assist — the APPLIER is resolved; the search producing the
   candidate at `play+0x2130` is not.
2. `0x002c3e34` standing-aim look-around fidget — target fully resolved, but its only activation path
   runs through a variant gate in caller `FUN_00488b40` with two unresolved runtime values.
3. `0x002c3fac` animated boots-swap action — needs the consumer of `Player+0x1b8` identified (no
   decompiled function reads it); a Ghidra data-xref or a live watchpoint would settle it.
4. `0x004c55c0` hookshot 3D reticle — every literal resolved, but the 3DS MODELS behind
   `Player+0x290c/+0x2910/+0x2914` are not.
5. `0x003c45f4` camera-mode decision tree — the 3DS camera-mode enum is renumbered AND extended and
   the mapping is unknown (FIRST_PERSON emits 3 where N64 CAM_MODE_FIRSTPERSON = 6).
6. `0x0033ebfc` ledge-grab wall-embed test — needs the `.bss` value at `0x0051b2f4 + 0x110` (selects
   checkHeight 23.0f vs 26.0f).
7. `0x002c3970` six-button item scan — needs the writers of uiCtx `+0x44/+0x48/+0x60` identified.
8. `0x002c2700` do-action label promotion — the MEANING of its 11 return values is unmapped.

Agents were told to say "blocked" rather than invent a plausible change, and did. Note how many of
these bottom out in the SAME place: a runtime value or a 3DS-engine subsystem, not player logic —
consistent with the ring-4 finding that the frontier has left gameplay code.
