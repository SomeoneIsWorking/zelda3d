---
id: I033
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

Novelty-capped argument log in MM's SkelAnime_DrawFlexLod (ZELDA3D_SKELLOG=1) -- prints every CHANGE of skeleton / skeleton[0] / dListCount rather than the first N calls

## Validated by

Its count-capped first version printed 8 healthy lines and proved nothing, because the offending call was the 26th. Recapped by novelty it named the bug in one run: call 26 reverting to run 1's skeleton pointer with skeleton[0] = ASCII text. Validated against both classes -- mm,mm (passes) shows one change, mm,oot,mm (crashes) shows three, and the third is the fault.

## Known failure modes

(none recorded yet)
