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

## The workaround that makes it usable: `LSAN_OPTIONS=use_globals=0`

LSAN stops treating globals as GC roots, so an object reachable only from `X::Instance` becomes
"unreachable" and IS reported. Validated on the same two-allocation control: with `use_globals=0` it
reports the reachable 11,111-byte block as well as the unreachable 22,222.

The absolute number this produces is meaningless on its own — ~80 MB across ~36,800 allocations for
one OoT run, nearly all of it engine state that is legitimately alive at exit. **The number that
means something is the DIFFERENCE between run counts**, because a per-run leak is exactly what grows
with the run count:

    oot        80,711,147 bytes / 36,803 allocations
    oot,oot    81,329,052 bytes / 36,887 allocations     -> +617,905 bytes per extra run

Two gotchas, both of which cost a run before being noticed:

- **`log_path` must be ABSOLUTE.** It resolves against the launcher's CWD, which the sequence harness
  changes; a relative path produced `ERROR: Can't open file …` inside the log and zero report files,
  which reads identically to "no leaks found".
- **The harness must let the process exit.** ASAN's leak scan runs after `main` returns, and
  `zelda3d_sequence.sh` killed the launcher after a hardcoded 30s. Now `ZELDA3D_SEQ_EXIT_WAIT`.
