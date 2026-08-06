---
id: 12
title: OoT rejects NTSC MQ US/JP roms as unrecognised, and scanning a PAL Debug 2 rom hits an UNREACHABLE
status: resolved
symptom: A valid NTSC Master Quest US or JP rom is filtered out of the extractor's candidate list as an unknown version, despite having a ZAPD config and a known-good CRC
tags: extractor,n3,oot,rom,undefined-behaviour
created: 2026-08-06
updated: 2026-08-06
---

## Cause

`verMap` in `soh/Extractor/Extract.cpp` listed `{ OOT_NTSC_US_GC, "NTSC MQ US" }` and
`{ OOT_NTSC_JP_GC, "NTSC MQ JP" }` — both keys already mapped two rows above to the NON-MQ names.
An `std::unordered_map` initialiser list drops duplicate keys (first wins), so those two rows were
dead, and `OOT_NTSC_US_MQ` / `OOT_NTSC_JP_MQ` appeared in the map not at all. `FilterRoms`
rejects anything the map does not contain, so both roms were 'unrecognised' — even though each has a
ZAPD config (`Config_GC_MQ_NTSC_U.xml` / `_J.xml`) and a known-good whole-rom CRC in
`goodCrcs`, i.e. every other part of the extractor was ready for them.

`PAL Debug 2` had the mirror-image problem: present in the name map, present in NEITHER
`IsMasterQuest` nor `GetZapdVerStr`, both of which ended in `UNREACHABLE` (`__builtin_unreachable`).
Merely *scanning* a Debug 2 rom therefore reached undefined behaviour, before any validation.

The shape is what allowed it: the same set of facts lived in four places that had to agree by hand —
a constants block, a name map, a CRC array, and two parallel switch statements.

## Fix

One `RomVersionTable` row per version (`soh/Extractor/RomVersions.cpp`), carrying name, ZAPD
config and MQ flag together, which makes a duplicate-key typo unrepresentable. The two MQ versions
are now reachable. `PAL Debug 2` gets `zapdVerStr = nullptr` — 'recognised but not extractable',
since no `Config_*.xml` exists for it — and stops with a message instead of UB. `IsMasterQuest`
answers `false` for an unknown rom rather than running off the end of a switch.

See claim C070 and `docs/project-structure.md`.
