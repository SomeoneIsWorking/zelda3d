---
id: C023
kind: claim
status: holds
created: 2026-07-30
tags: 
reconfirmed: 2026-07-30
---

## Claim

Adult Link's empty sheath (mid 42) is drawn for EmptySheathNoShield and for ShieldOnBackSwordDrawn with no shield, matching the OoT3D sheath DL tables

## Evidence

sSheathDLs @VA 0x0053c5e8 = (42,21) and sSheathWithoutSwordDLs @0x0053c4d8 NONE=(42,21) DEKU=(42,12) HYLIAN=(1,10) MIRROR=(3,21), read byte-exact from code.bin (offset = VA-0x100000), 8-byte stride with (adult,child) as s16 at +0/+4. Mid 42 confirmed as real sheath-strap geometry by isolating it live (linkmid only 42) and differencing against an empty-mask frame: 2826 px, bbox y[9:263] x[128:553].

## What would falsify it

A different table is shown to drive the 3DS sheath draw, or the (adult,child) s16-at-+0/+4 stride is disproven, or adult mid 42 turns out to be something other than the sheath strap

## Re-confirmed 2026-07-30

Values re-verified and the LAYOUT CORRECTED by a full dump of 0x0053c380-0x0053c680: entries are s32 (adult +0, child +4) and the logical stride is 0x10, not 0x8 -- every (adult,child) pair is stored twice. The committed sheath values are unaffected because they were sampled 0x10 apart, but the earlier 's16 at +0/+4, stride 8' description was wrong. Also found sSheathWithoutSwordDLs has 8 rows not 4. Full decode in oot3d-decomp/docs/player_dl_tables.md.
