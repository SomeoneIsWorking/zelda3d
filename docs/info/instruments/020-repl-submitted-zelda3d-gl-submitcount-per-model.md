---
id: I020
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

REPL submitted / Zelda3D_GL_SubmitCount -- per-model draw-submission counter

## Validated by

Validated in both directions on one Jabu-Jabu run: m_WFloat00W 9222, m_Wsea00 4857, m_Wshutter1 4584, bdan_switch_b 708 submissions, against auto[0x21] model=2017 with submits=0 (resolved but never submitted). The zero row is what makes the positives meaningful. It exists because the pixel-contribution check cannot see three measured prop classes -- flat single-sided planes edge-on, props flush with the floor, and occluded instances of many-instance props -- where a CORRECT routing reads 0 px; that false negative caused three reverts of working code. LIMITATION: submits>0 proves the model is DRAWN, not that it is the RIGHT mesh; a wrong mesh submits identically.

## Known failure modes

(none recorded yet)
