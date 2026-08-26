---
id: C088
kind: claim
status: holds
created: 2026-08-26
tags:
depends: Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/authored_flight.cpp#advanceFlight, Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/steering_math.cpp#Atan2, tools/soh3d_harness/boss_fd_comparison_policy.cpp#Evaluate
---

## Claim

BossFd forced action-0 authored flight producer matches OoT3D through 270 authored ticks

## Evidence

Embedded oracle and Clang-linked libsoh_core.so reported exact zero deltas for position, rotation, velocity, speed, turn, move timer, and sampled 150-entry ring; validated comparator fault path reported DIVERGED then restored MATCH on 2026-08-26.

## What would falsify it

Any nonzero paired producer/ring delta for the same forced profile, or a change to advanceFlight/Atan2/trig-table generation/comparison evaluation without re-running the paired baseline-fault-restore gate
