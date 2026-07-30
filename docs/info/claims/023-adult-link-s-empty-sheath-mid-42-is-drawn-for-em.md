---
id: C023
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

Adult Link's empty sheath (mid 42) is drawn for EmptySheathNoShield and for ShieldOnBackSwordDrawn with no shield, matching the OoT3D sheath DL tables

## Evidence

sSheathDLs @VA 0x0053c5e8 = (42,21) and sSheathWithoutSwordDLs @0x0053c4d8 NONE=(42,21) DEKU=(42,12) HYLIAN=(1,10) MIRROR=(3,21), read byte-exact from code.bin (offset = VA-0x100000), 8-byte stride with (adult,child) as s16 at +0/+4. Mid 42 confirmed as real sheath-strap geometry by isolating it live (linkmid only 42) and differencing against an empty-mask frame: 2826 px, bbox y[9:263] x[128:553].

## What would falsify it

A different table is shown to drive the 3DS sheath draw, or the (adult,child) s16-at-+0/+4 stride is disproven, or adult mid 42 turns out to be something other than the sheath strap
