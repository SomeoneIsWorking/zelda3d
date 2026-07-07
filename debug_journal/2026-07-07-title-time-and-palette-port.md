# 2026-07-07 — title time-of-day + spot99 palette port (slot schedule OPEN)

## Ported + verified

1. **op-0x8c = SET TIME OF DAY.** Constants read via Ghidra:
   DAT_002c5ff4=60.0f, DAT_002c5ffc=45.511f (=0x10000/1440 daytime units
   per minute). Value = (s16)(hours*60*45.511) + (s16)((min+1)*45.511).
   Title cues: 4:01 AM at f=0 and f=301 → 0x2AD7.
   Ported (zelda3d_cutscene.cpp time cues + Zelda3D_TitleCsTimeOfDay);
   ApplyTitleCam now sets gSaveContext.dayTime from the cue.
   **VERIFIED live: soh_env daytime=0x2ad7.** (The old dayTime=0x0000
   force was a pre-decode approximation — 4 AM, not midnight.)

2. **spot99 light settings plumbed.** spot99_info.zsi cmd-0x0F (17
   entries, 28B) parsed at cs load; converted to Zelda3dLightSlot;
   while gZelda3dInTitleDemo the per-frame palette selection uses these
   instead of kZelda3dSceneLighting[spot00].

3. Other cue decodes (journal-only, port pending):
   - op 0x03 sub-op 0x1e/0x1f = FUN_00366704(play, 3/4) = SET EVENT FLAG
     n in a play+0x5F98 bitfield (flag 3 at f345, flag 4 at f1930 —
     probably logo / press-start staging). Consumer un-RE'd.
   - op 0x7c = transition trigger via FUN_003655d0 (type 4, f2310..2460
     = the 38.5 s loop fade).
   - op 1000 payload {0,0,0x68,0x960}; handler FUN_00491364 (17 KB decomp
     dumped, unread).
   - op 0x0d falls into the interpreter's default (skip) case — inert?

## OPEN: the light-slot schedule

`compare lighting` at title: Az envCtx slot=8 (of spot99's 17), SoH
slot=1 (N64 4-slot z_kankyo schedule + bias). The 3DS picks slots via
its own schedule — the 2026-07-04 "palette lookup decoded" journal's
54-byte table at play+0x318F indexed by env[0x21]. BUT that journal's
env base (env+0x21 = play+0x31B1) conflicts with the pinned envCtx =
play+0x3135 (env+0x21 = play+0x3156), and a live dump of play+0x318F
shows pointer soup, not a clean 54B table. env[0x21] (both candidate
addresses) reads 0 while Az slot=8 — so the old derivation is at least
partially wrong. REDO with a proper FUN_0045dd30 (Env_Update) decomp
pass using the correct envCtx base; find what selects slot 8 at 4:01 AM
from spot99's 17 entries, then port that schedule.

Also: CompareLightingImpl printed Az prevSlot=75 — its own offsets may
need re-verification.
