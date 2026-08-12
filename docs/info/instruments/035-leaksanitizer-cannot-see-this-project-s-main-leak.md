---
id: I035
kind: instrument
status: trusted-with-a-caveat
created: 2026-08-12
tags: asan,lsan,leaks,lifetime
---

## Instrument

LeakSanitizer (via `ASAN_OPTIONS=detect_leaks=1` on the `scratch/build-asan` tree) as evidence about
leaks in zelda3d.

## What it can and cannot show

It works — and it is **blind to this project's dominant leak class.**

LSAN reports **unreachable** allocations. Every per-run leak fixed in issue 0016 was *reachable*: the
object stayed pointed at by a global (`OTRGlobals::Instance`, `SaveManager::Instance`,
`Rando::Settings::mInstance`, …). LSAN classifies those as "still reachable" and, by default, says
nothing. So `detect_leaks=1` on this tree can report **zero leaks while a full copy of the message
tables, the item tables and the actor DB is leaked every run.**

## Validated in BOTH directions, on real data

- Positive control, toolchain: a 12,345-byte unreachable `malloc` → `LeakSanitizer: detected memory
  leaks`. The toolchain works.
- The discriminating control: one allocation stored in a global (11,111 bytes) and one dropped
  (22,222 bytes) in the same program. LSAN reported **only the 22,222** — silent on the reachable one.
  That is the exact shape of every singleton leak this arc fixed.
- On the real binary: `zelda3d --probe-cores` with `verbosity=1` prints `LeakSanitizer: checking for
  leaks` and then reports nothing. The check RAN; it had nothing it was willing to call a leak.

## How to get a real answer about these leaks

Not from LSAN's default. Either null the global before exit so the block becomes unreachable, or
count allocations directly — which is what the per-run `"freeing the previous run's X"` lines do, and
why they print a count rather than a bare "cleared".

## Consequence for past and future claims

Any "ASAN found no leaks" statement about zelda3d means "no UNREACHABLE leaks". It is not evidence
that a run cleans up after itself. `tools/zelda3d_deep_check.sh` states this in its verdict rather
than leaving the reader to assume the stronger reading.
