# 2026-07-07 — 3DS time-based light schedule RE'd; blend seam + layout OPEN

## SOLVED (verified)

1. **CompareLightingImpl's Az "slot"/"prevSlot" labels are WRONG.** JIT
   watch on 0x08721A1A + disasm 0x0045e438..0x0045e480: those bytes are
   the SUN DIRECTION (sin/cos of dayTime-0x8000, scaled by pool floats at
   0x0045e800..0x0045e80c), written at env+0xB5..0xB7 with negation at
   +0xBB..0xBD (env base r4 = play+0x3125 in Env_Update). Fix the harness
   labels when touching it next.

2. **The 3DS light schedule = N64 sTimeBasedLightConfigs, kept.** Static
   table at [pool 0x0045e168] = code.bin 0x00531EFC; rows of 54B = 9 x
   6-byte spans {u16 startTime, u16 endTime, u8 slotFrom, u8 slotTo};
   row = env[0x21] (config), 0 at title. Blend w = (t-start)/(end-start)
   (FUN_0045dd30 @0x0045e4a8, decomp lines 150-290). Configs 0..4 map to
   slot groups 4N..4N+3. Title at 4:01 AM: span 0x2AAC..0x4000 = slots
   3->0, w=0.0079 (night, hint of dawn).
   PORTED as data+API: zelda3d_cutscene.cpp kTitleLightSchedule +
   Zelda3D_TitleCsLightBlend. dayTime cue port verified (soh_env 0x2ad7).

## OPEN — do NOT wire blindly (falsified attempts this session)

1. **Runtime palette entry layout is CONTESTED.** Env_Update's blend
   reads entry bytes at +0xA..0xC (-> env+0xB2..B4, ambient-ish),
   +0x10..0x12 (-> env+0xB8..BA), +0x16..0x18 (-> env+0xBE..C0) of the
   28B entries (base iVar18, source unpinned — may be a scene-load
   CONVERTED copy, not the raw ZSI cmd-0x0F bytes).
   gen_oot3d_scene_lighting.py's validated map (+0 amb, +4 l0col,
   +0xA l1col, +0x10 fogEnd f32) CONFLICTS if iVar18 = raw ZSI bytes
   (colors would overlap the fog floats). Resolve by pinning iVar18
   (decomp the fn head for its source) and reading env+0xB2.. live at a
   KNOWN time (my one live read gave amb=(0,143,37) with env+0xa8
   time=0 — the time cell was ALSO 0 when dayTime should be 0x2AD7, so
   either the env+0xa8 offset or the base is wrong for that read;
   re-derive both from the decomp before trusting any of it).

2. **SoH apply seam:** writing play->envCtx.lightSettings from
   ApplyTitleCam (post-frame) is a NO-OP — Environment_Update recomputes
   it next frame before consumers read (verified soh_env unchanged).
   Correct seam: inside/after z_kankyo's lightSettings computation, like
   the existing Zelda3D_WorldShadeBlend hook. Wire there once (1) is
   pinned.

3. Curiosity: the 16 bytes at spot99_info.zsi+0x3518 (before " BDQ")
   equal palette entry[4] bytes — the "container prefix" interpretation
   is suspect; check whether cmd-0x17's ptr actually points at a
   16-byte light-snapshot + cs.

## Next session order

1. Decomp FUN_0045dd30 head: pin iVar18 (palette base) + iVar8 (the
   +0xa8 time holder) + param_2 (env base) relations.
2. Live-read env+0xB2..C0 at title with a confirmed time cell; match
   against ZSI entries -> layout pinned.
3. Wire the blend at the z_kankyo seam; verify soh_env == Az blended
   values; then close title lighting.
