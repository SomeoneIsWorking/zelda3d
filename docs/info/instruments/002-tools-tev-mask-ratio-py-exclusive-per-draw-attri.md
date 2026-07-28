---
id: I002
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/tev_mask_ratio.py --exclusive (per-draw attribution at Zora)

## Validated by

VALIDATED for draws that HAVE exclusive pixels — it correctly separated d3's own 1.002 from the 0.88 it inherited from the water drawn on top. But it is INERT for d9: computing the exclusive set (pixels covered by d9 and by no other mask in scratch/drawiso/zora_masks/masks.npz) yields ZERO pixels out of d9's 12072, so --exclusive silently has nothing to report for that draw rather than saying so. Check the exclusive-pixel COUNT before believing a --exclusive number, and treat any full-mask number for a fully-overlapped draw as contaminated by whatever is layered over it.

## Known failure modes

(none recorded yet)
