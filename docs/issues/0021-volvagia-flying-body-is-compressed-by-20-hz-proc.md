---
id: 21
title: Volvagia flying body is compressed by 20 Hz procedural history
status: investigating
symptom: Flying Volvagia renders with a shortened/compressed articulated body because the 150-entry OoT3D history is populated at the host 20 Hz cadence instead of the producer 30 Hz cadence
tags: volvagia,boss-fd,history,cadence,render
created: 2026-08-24
updated: 2026-08-26
---

## Root cause

OoT3D's `FUN_003C724C` produces the 150-entry body ring and 45-entry mane ring once per
30 Hz actor update, after its movement helpers update the actor transform. The host actor update is
20 Hz. Recording only the host transform once per host update therefore stores 50% more elapsed time
between adjacent history entries and shortens the visible 18-segment body. This is a producer-rate
error, not a draw transform or segment-spacing error.

The controller cadence was coupled to the same incorrect history tick as well. The live head and two
arm controllers advance by 2/3 authored frame on every 30 Hz producer update, before the stop gate;
the body and death-head controllers remain at frame zero.

## What was tried / dead ends

- Scaling the body/mane offsets or changing their draw spacing would only hide the compressed ring
  and would diverge again when speed changes.
- Advancing all four CSAB controllers once per recorded host sample was wrong in both ownership and
  rate: it moved the static body/death-head controllers and ran the live controllers 1.5 times fast.
- Sampling from the draw path made history depend on visibility and render submission. The producer
  is actor-update-owned in the 3DS binary.
- A process-global singleton plus a gameplay-frame-gap reset was not valid actor lifecycle
  ownership: multiple Boss_Fd actors are legal, pause gaps can reset a live actor, and immediate
  arena-address reuse can retain stale state. The existing typed `ObjectExtension` store is freed at
  the authoritative `Actor_Delete` seam and resets between core runs.

## Resolution

The shipping owner now emits one then two authored ticks on alternating 20 Hz
host updates, reproducing the 30 Hz producer without floating cadence drift.
Each authored tick uses the decompiled unit integration scalar, exact
interpolated OoT3D sine/cosine table, and `FUN_003696EC` atan2 polynomial.

The embedded-oracle forced-profile gate reached exact zero-delta through 270
authored ticks for position, rotation, velocity, speed, turn, move timer, and
the sampled 150-entry body ring. Its positive control changed one history X
sample by 1000 and reported `DIVERGED` (`meanPos=50`, `maxPos=1000`), then
returned exact `MATCH` after restoration. The issue remains investigating
until the live user-facing flying-body view is captured; the comparator scope
does not cover general action/death sequencing.
