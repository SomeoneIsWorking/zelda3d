---
id: I027
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/unique_symbol_collisions.py (reports STB_GNU_UNIQUE symbols shared between the two game cores -- the one class RTLD_LOCAL cannot privatise)

## Validated by

Run against BOTH classes, not reasoned about: on the pre-fix binaries it reported 303 core-owned collisions and exited 1; on the post-fix binaries 0 and exit 0. It refuses rather than reporting a clean result when a core binary is missing (exit 2, 'this check inspected NOTHING'). Library-internal statics (std/nlohmann/Rml) are listed under --all but never fail the check, so the failing category stays readable.

## Known failure modes

(none recorded yet)
