---
id: I046
kind: instrument
status: trusted
created: 2026-09-28
---

## Instrument

tools/pica_lighting_registers.py

## Validated by

Shows the other answer: reduces the oracle's captured config0/config1 pairs to the non-trivial terms of ComputeFragmentsColors. Reports 0 active terms for the captured MM3D words (0x80000400/0xff7fffff) and 14 for a maximally-enabled legal word, so a reducer that could only ever say 'nothing needed' would fail. Field map is PARSED from Azahar regs_lighting.h, not transcribed. Refuses a config1 with the hardwired bit 18 clear and an invalid lighting-config field. 23 tests, 6/6 mutations caught by tools/check_pica_lighting_registers_mutations.py, which purges __pycache__ because a size-preserving mutation otherwise leaves a stale .pyc in effect.

## Known failure modes

(none recorded yet)
