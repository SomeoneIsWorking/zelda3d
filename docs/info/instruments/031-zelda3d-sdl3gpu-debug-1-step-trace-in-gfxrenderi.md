---
id: I031
kind: instrument
status: DISTRUSTED
created: 2026-08-12
distrusted_on: 2026-08-12
---

## Instrument

ZELDA3D_SDL3GPU_DEBUG=1 step trace in ~GfxRenderingAPISdl3Gpu -- prints each teardown phase before the call it is about to make

## Validated by

NOT VALIDATED AS A DOUBLE-RELEASE CHECK -- see the distrust note. It does reliably do the one thing it was built for: name the phase that dies when a phase dies.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-12

It certified the code it was standing in. Issue 0009 used it to RULE OUT a double release in this destructor -- 'every release loop completes' -- and the bug was a double release in this destructor. The trace answers 'did each phase run', which cannot contradict 'two phases released the same handle'; and because SDL3 QUEUES a released GPU resource, the abort always landed after the last step printed, which read as exculpatory. Trust it only for locating a phase that dies. For ownership, use I030.

> Every result this instrument produced is suspect until it is re-validated.
