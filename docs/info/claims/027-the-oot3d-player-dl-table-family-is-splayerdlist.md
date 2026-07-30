---
id: C027
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The OoT3D player DL table family is sPlayerDLists[21] @0x0053c698, indexed by PLAYER_MODELTYPE_*, giving (adult,child) mesh ids for all 21 model types

## Evidence

Master pointer table of 21 words at 0x0053c698, each pointing at one 0x10 DL row {adultNear,childNear,adultFar,childFar}; 21 == PLAYER_MODELTYPE_MAX (0x15). Five independent checks: exact slot count; the two enum entries documented 'unused, same as X' hold identical values (0x02/0x03 both (16,2); 0x0B/0x0C both (29,19)) while pointing at DIFFERENT addresses; both sheath slots land on tables already verified in-game (0x11 and 0x13 -> (42,21)); LH_BGS = adult 37 matches what the port already does; and the shield sub-run group +4..+7 adult column is 31,31,0,2 which is literally the existing hand-derived line 'hylian ? 0 : mirror ? 2 : 31'. Full table in oot3d-decomp/docs/player_dl_tables.md.

## What would falsify it

A model type is ported from this table and renders visibly wrong geometry in-game, or the 21-slot table is shown to be indexed by something other than PLAYER_MODELTYPE_*
