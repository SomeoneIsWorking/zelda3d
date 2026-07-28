---
id: I006
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

Ghidra FindRangeRefs / the Reference DB for locating who reads a data address in code.bin

## Validated by

PARTIALLY TRUSTED — it fails SILENTLY on un-disassembled regions, and its silence reads as 'nothing references this'. Concrete case: FindRangeRefs over 0x4bff40..60 returned ZERO refs and that zero was written into the RE frontier as a property of the binary ('the table is reached via base+offset'), blocking kanban #201 e as multi-session RE for days. In fact 0x4bff48 is read by an ordinary ldr r2,[pc,#0x278] at 0x004bfcc8; Ghidra simply had not disassembled that region as code, because an embedded literal pool mid-function derails auto-analysis. VALIDATION RULE: before believing a zero-reference result, confirm the surrounding bytes are disassembled as code (Disasm.py / DecompDump on a nearby address). CHEAP CROSS-CHECK that is immune to this failure: scan code.bin directly for ldr rX,[pc,#imm] whose computed target (va + 8 +/- imm) equals the address of interest, and for pool words / movw+movt pairs materialising it. Seconds to run, exact, no analysis state involved.

## Known failure modes

(none recorded yet)
