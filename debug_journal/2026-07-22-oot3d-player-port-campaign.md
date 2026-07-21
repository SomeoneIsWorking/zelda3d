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
- Full state-matrix sweep was running at the time of writing.
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
