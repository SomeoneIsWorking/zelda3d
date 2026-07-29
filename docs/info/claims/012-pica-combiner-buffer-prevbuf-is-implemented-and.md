---
id: C012
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

PICA combiner buffer (PREVBUF) is implemented and correct: CMB stage i's bufferInput maps to Azahar update-mask bit i-1

## Evidence

A/B with one line differing: bracelet RGB(25,17,7)->(136,101,11) at unchanged footprint, matching its texture mean (144,94,4); no-PREVBUF control identical to the integer. Mapping confirmed on all 14 PREVBUF-reading materials latching at exactly read-1.

## What would falsify it

a material that reads PREVBUF with no preceding latch (would need the unparsed initial tev_combiner_buffer_color), or a PREVBUF material rendering wrong vs the oracle
