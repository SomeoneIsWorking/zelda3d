# #88 "weird yawn" / wrong idle fidget — PREMISE FALSIFIED, idle picker measured AT PARITY

**Date:** 2026-07-23 · **HEAD at start:** `5f749af5` · **Outcome:** no code change (comment + docs only).

## TL;DR

There is no yawn. The whole #88 RE rested on a decomp note that read a raw animation-table index as
"yawn": **animId `0x50` is `nml_wait_free`**, Link's neutral standing idle. A scan of all 582 entries
of `oot3d-decomp/tools/skeldata/player_animid_names.json` finds **no yawn / akubi / nobi / stretch clip
anywhere in Link's animation set.**

With that corrected, OoT3D's claimed "default idle table @0x53a5f8 `{0x50,0x58,0x58,0x119}`" decodes to
`{nml_wait_free, nml_wait, nml_wait, ft_wait_long}` — **byte-identical to N64's
`D_80853914[PLAYER_ANIMGROUP_wait]`**. It was never a Grezzo change.

I then measured the live oracle against the live game at Kokiri and found the idle picker **already at
parity**. Nothing was ported.

## Method

Per the standing rule to verify the premise against the live oracle before porting (the last two Link
tasks both found a "RE-ready, just port it" note was stale), I measured both sides before touching code.

- **Oracle:** `harness_ctl.spawn()` → `boot_to_gameplay(entrance=0xEE)` → `set_time_of_day(0x6000)` →
  `analog 0 0` (no stick ⇒ plain idle). Per-sample `az_linkanim` (animId from
  `Player+0x254+0x30`), plus `r32`/`r8` for `actionFunc(+0x1708)`, `idleType(+0x1748)`,
  `stateFlags2(+0x1714)`, `+0x29b8`, `+0x174e`, and the version byte `*0x54ac55`.
  Scripts: `scratch/idle_observe/oracle_idle.py`, `scratch/idle_observe/oracle_idle_dist.py`.
- **Ours:** `tools/zelda3d_game.sh start` (headless, entrance 238 = 0xEE, time 0x6000), `asel link`,
  then `linkanimstate` polled for 360 s.
- animId → CSAB name resolved through `player_animid_names.json` (the same table `link_sweep.py` uses).

Oracle idle state confirmed first: `actionFunc = 0x004ba538` (= `Player_Action_Idle`), so the picker
under test really is the one the RE targeted.

## The measurement

Matched state — **neither side had a weapon IN HAND at idle.** This is what the gate actually tests:
it requires `rightHandType == PLAYER_MODELTYPE_RH_SHIELD`, which is only set with the sword drawn, so at
a plain idle it is RH_OPEN on both sides *regardless of inventory* (our Link visibly carries a sheathed
sword). Confirmed observationally: the `wait_itemC_20f` (ADJUST_SHIELD) and `wait_itemD1_20f`
(SWORD_SWING) fidgets never fired on either side. The comparison is therefore valid without needing the
two saves to have matching inventories.

| | default idle | fidgets seen | distribution | cadence (fidget : default) |
|---|---|---|---|---|
| **oracle** (n=6 picks, ~2100 f) | `nml_wait_free` (0x50) | `nml_waitF_typeA_20f` (0x56), `waitF_itemA_20f` (0x1c) | look-around 67% / tunic 33% | ~276 f : ~132 f ≈ **2:1** |
| **ours** (n=26 picks, 360 s) | `nml_wait_free` | `nml_waitF_typeA_20f`, `waitF_itemA_20f`, `waitF_itemB_20f` | look-around 69% / tunic 15% / tap-feet 15% | ~10.5 s : ~5.1 s ≈ **2:1** |

Both match the faithful N64 formula for a Link with no weapon in hand. `Player_ChooseNextIdleAnim` rolls
`commonType = Rand_ZeroOne() * 5`; commonType 0 (SWORD_SWING) and 3 (ADJUST_SHIELD) are rejected
without a drawn weapon and fall back to `roomCtx.curRoom.behaviorType2` (= 0 = FIDGET_LOOK_AROUND at
Kokiri), giving **expected 60% look-around / 20% tunic / 20% tap-feet**. Both sides land there.

The strict default↔fidget alternation (the `idleType = (idleType+1)&1` toggle) matches on both sides,
and the `-6.0f` morph is not a divergence — N64's `Player_ChooseNextIdleAnim` already ends in
`LinkAnimation_Change(..., ANIMMODE_ONCE, -6.0f)`, and our port runs that exact vendored code.

Our path is: vendored N64 `z_player.c` picks a `gPlayerAnim_*` resource → `zelda3d_link.cpp` reads
`player->skelAnime.animation` → `Zelda3D_ResolvePlayerCsab` maps the basename through
`kPlayerAnimMap` → 3DS CSAB. The fidget clips map 1:1 by name (`nml_wait_typeA_20f`,
`waitF_itemA_20f`, …), so faithful N64 selection yields the faithful 3DS clip.

## ⚠️ The sampling trap (the main thing to carry forward)

**My first oracle run showed ONLY the look-around fidget and looked like a hard, clean divergence.**
It produced a very tidy (and wrong) story: "OoT3D suppresses the common tunic/tap-feet fidgets; ours
plays them; port the suppression." I had even found a plausible mechanism for it (see `play+0x2130`
below) before checking the statistics.

It was pure small-n noise. Idle re-picks only happen on `animDone` in `Player_Action_Idle`, i.e.
**~130–280 frames apart**, so a run that looks long in wall-clock can contain only 2–3 picks. At n=6
the oracle's `waitF_itemA_20f` appears and the "divergence" evaporates.

**Rule: any idle-distribution claim needs ≥20 fidget picks per side.** This is now recorded in-code in
`zelda3d_link.cpp` and in `player_port.md`.

## Corrections landed in the RE docs

`oot3d-decomp/docs/player_port.md` §"#88 picker ALIGNED" previously asserted `0x50 = yawn` and framed
the bug as "the HOT-room bit + which idle table the version gate selects". Both parts are now marked
falsified/inert with the measurements above, and the bug→function table row is rewritten. Per the
"keep notes honest and self-correcting" rule the old claim was corrected in place, not appended to.

The **alt-table version gate does not block #88** and needs no user call: the oracle's observed default
idle is `0x50` (`nml_wait_free`) = the DEFAULT table, so the `{0x1f9,0x1f8,0x1f8,0x1fa}` alt path is
simply not taken at a plain idle. (For the record: oracle reads `*0x54ac55 = 0x7f` and
`player+0x174e = 0x02`, so the gate's `player+0x174e != 1` disjunct is already true.) The
`divergence_map.md` OPEN DECISION on the alt-anim path therefore stays open on its own merits but is
**not** a #88 prerequisite.

## Genuine 3DS-only deltas found, and why neither was ported

Both are real, both are **inert at the reachable idle**, and neither is the reported symptom. Nothing
was stubbed, faked or approximated for either — they stay honestly un-ported.

1. **HOT-room bit** — `if (*(s8*)(play + 0x4c37) != 0) fidgetType = FIDGET_HOT(3);`. `+0x4c37` is
   room-header behavior bit 9, authored per-room via `SCENE_CMD_ROOM_BEHAVIOR` (0x2344c4), Grezzo's
   replacement for N64 `behaviorType2 == TYPE2_3`. Kokiri is non-HOT so it never fires there. A
   faithful port needs the bit **extracted from the ROM room header**, not guessed per scene — and
   note `FIDGET_HOT` and `FIDGET_WARM` share the same anims (`wait_typeB`/`waitF_typeB`) in *both* N64
   and 3DS, so even when it fires the visible delta is limited to genuinely HOT rooms (Fire Temple, DMC).

2. **`play + 0x2130` gate (newly identified this session).** The fidget branch of `004ba538` is
   `if ((focusActor==0) && (play[0x2130] != 0)) { <alt / skip path> } else { <faithful N64 common roll> }`.
   Since `focusActor==0` is already guaranteed inside that branch, this reduces to a test on
   `play[0x2130]` alone: when it is non-zero the common-fidget roll is **bypassed entirely**.
   **`play + 0x2130` is the 3DS-only auto-aim head-track TARGET actor** — pinned by
   `oot3d-decomp/build/decomp/002b7fd0.c:556`, `func_0x002bf814(player, play, *(int*)(play+0x2130), 0)`,
   where `0x2bf814` is the "auto-aim head-track nearest actor" acquisition assist already listed in
   `divergence_map.md` ring-1 as an un-ported 3DS-only feature.
   Measured **inert at Kokiri** — the common fidgets *do* fire on the oracle, so `play[0x2130] == 0`
   there. ⇒ A faithful port of this gate is **blocked on porting auto-aim `0x2bf814` itself.** It would
   be easy and wrong to approximate `play[0x2130]` with a "is there a nearby actor" test; that fakes an
   un-RE'd subsystem's output, which is exactly the RE-frontier sin, so it was not done.

   Note also the ordering hazard this creates for a future session: if auto-aim is ported *later*, this
   idle gate must be ported *with* it, or the idle distribution will silently change.

   Also worth recording: the common-fidget gate *itself* is byte-faithful to N64 in the 3DS binary —
   `if (commonType < 4) { if (commonType==0||commonType==3) { require rightHandType==0x0a(RH_SHIELD);
   if (commonType!=0) require Player_GetMeleeWeaponHeld2; } fidgetType = commonType + 9; }`, i.e.
   ADJUST_TUNIC(1)/TAP_FEET(2) accepted unconditionally, exactly as N64. So the *only* Grezzo change to
   fidget selection is the outer `play[0x2130]` bypass, not the gate.

## Checked and ruled out along the way

- **SoH's `VB_SET_IDLE_ANIM` enhancement hook** (`soh/soh/Enhancements/Fixes/FixTwoHandedIdleAnim.cpp`)
  changes the fidget gate, but it is CVar-gated (`gEnhancements.TwoHandedIdle`, default 0) and is not
  force-enabled anywhere in `src/zelda3d/`. Consistent with the observation: no SWORD_SWING fidget ever
  appeared, which is what the enhancement would have enabled with a melee weapon held.
- **A distribution skew.** An intermediate 7-pick sample of ours showed only 14% look-around vs the
  expected 60%, which looked like a real skew. At n=26 it is 69% — again small-n.

## What the user is most likely seeing

Since selection, distribution, cadence and morph all match, the residual candidate for a "weird yawn"
is the *appearance* of the dominant fidget clip itself — `nml_waitF_typeA_20f`, the look-around/stretch,
which is ~2/3 of all fidgets and is the only idle motion big enough to read as a "yawn". That is a POSE
question, not a selection question, and the pose path is already covered by the live per-bone oracle
(`tools/parity_pose_sweep.py`: idle 1.2° / walk 1.2° / run 1.7° median). A clip of the forced fidget is
at `scratch/screenshots/fidget_waitF_typeA.mp4` (`linkanim nml_waitF_typeA_20f` + `acam 120` + `record`)
so the user can confirm whether that clip is the thing they called the yawn.

## Artifacts

- `scratch/idle_observe/oracle_idle.py`, `scratch/idle_observe/oracle_idle_dist.py` (oracle probes)
- `scratch/idle_observe/oracle_dist.json`, `oracle_idle_samples.json` (raw samples)
- `scratch/screenshots/fidget_waitF_typeA.mp4` (the look-around fidget, forced + recorded)
