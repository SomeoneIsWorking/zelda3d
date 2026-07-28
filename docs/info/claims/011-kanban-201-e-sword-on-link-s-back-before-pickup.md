---
id: C011
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

Kanban #201 e (sword on Link's back before pickup) is a MISSING STATE, not a wrong mesh id — LinkGear cannot express 'draw nothing on the back' because equips.buttonItems[0] is not one of its inputs

## Evidence

OoT3D rule, RE'd end to end (oot3d-decomp commit 7175128): the sheath mesh is sheathDLists[currentShield*4 + lod*2], and when child && sheathType in {SHEATH_16, SHEATH_17} && buttonItems[0] != ITEM_SWORD_KOKIRI the base swaps to 0x0053c4b8, whose rows are -1,-1,-1,-1 and -1,13,-1,13. Those are byte-for-byte N64's two rows commented '(child, no sword)' in sSheathWithSwordDLs (z_player_lib.c:212-223): NULL,NULL,NULL,NULL and NULL,gLinkChildDekuShieldWithMatrixDL,NULL,gLinkChildDekuShieldWithMatrixDL — the NULL-vs-value pattern matches at every position, and -1 is SetMeshVisible's no-op sentinel. Our side (Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp, LinkMidMask::compute plus the shared linkAdultMidMask) maps EVERY sheathType to some sheath mesh, with SHEATH_17 falling through to EmptySheathNoShield and no draw-nothing case in the enum.

## What would falsify it

A live run showing child Link with no Kokiri sword already drawing nothing on his back (which would mean the defect is elsewhere), or a LinkGear/adapter change that adds the B-item input and does not fix it.
