---
id: C066
kind: claim
status: holds
created: 2026-08-06
tags: shared,resource,estimate
depends: Shipwright/soh/soh/resource, 2ship/2s2h/resource
---

## Claim

The resource/ layer is a POOR de-duplication target despite topping the similarity ranking: only 3 of its 28 byte-identical .cpp files also have a byte-identical header.

## Evidence

Pairwise check over Shipwright/soh/soh/resource vs 2ship/2s2h/resource: 156 matching basenames, 28 .cpp byte-identical, but only Animation.cpp, Background.cpp and Cutscene.cpp have headers that are ALSO identical (~40 LOC). The rest are small stubs (mostly 11-line Set*Factory bodies) whose .cpp is identical only because the per-game divergence lives in the header -- e.g. SetActorList.cpp is identical in both yet returns ActorEntry*, a different struct per game. An estimate of ~6k LOC recoverable here was derived from similarity buckets and does not survive checking the headers.

## What would falsify it

if the two games' resource headers are unified (or the per-game types are parameterised), the .cpp bodies become shareable and this stops being a poor target
