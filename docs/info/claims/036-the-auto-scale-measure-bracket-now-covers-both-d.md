---
id: C036
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The auto-scale measure bracket now covers both display lists, and an empty bracket no longer overwrites a real height with zero

## Evidence

Empty sessions previously reported height 0, which Zelda3D_MeasureResult writes into the slot. Proof it mattered: zelda_gi_heart (obj 0xb7) was a permanent state-4 give-up in Kokiri Forest, and with the suppression guard it measures scale=0.32468 n64h=17.0. Translucent actors now measure too: the Deku Tree wall web (ydan_spkabe) went from state=4 with no measurement to state=2 scale=0.10000 n64h=288.8. Opaque props in the same scene unregressed (ydan_objects 0.00804/10.0, syokudai 0.99363/60.0); skinned entries correctly stay at state=2 scale=0 since they use bone lengths.

## What would falsify it

An opaque prop's measurement changes value or stops arriving, or a measure session is found that accumulates geometry in BOTH lists (which would make the two sessions fight rather than one being empty)
