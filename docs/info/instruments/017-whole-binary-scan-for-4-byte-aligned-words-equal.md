---
id: I017
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

Whole-binary scan for 4-byte-aligned words equal to a known data VA (finds master pointer tables and literal pools without Ghidra)

## Validated by

Run against 5 probed table addresses it returned 6 hits with specific VAs and 0 for one address, i.e. it discriminates rather than always-hitting. It located both the sPlayerDLists master table at 0x0053c698 and the literal pool at 0x004c71cc holding consecutive pointers to the two sheath tables. Cross-checked the negative case too: the same script's ARM/Thumb MOVW/MOVT counters returned 3 and 49 over the whole 4.6MB binary, which correctly FALSIFIED the movw/movt hypothesis rather than silently finding nothing.

## Known failure modes

(none recorded yet)
