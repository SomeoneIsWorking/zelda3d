---
id: I029
kind: instrument
status: trusted
created: 2026-08-07
---

## Instrument

REPL 'menuhit' (launcher mouse hit-test: every actionable row's box + which element RmlUi actually returns at its centre)

## Validated by

Validated BY CATCHING A REAL BUG, in both directions, on the same build. Before the fix it reported '5 actionable row(s), 4 reachable by mouse, 1 OCCLUDED' and NAMED the occluder -- 'start_oot ... OCCLUDED by svg class="launcher__background-mm"' -- which is the whole point: RmlUi hit-tests irrespective of background and opacity, so a 10%-opacity decoration eating a click is invisible to every other method. After adding pointer-events:none it reported 5/5 reachable, and 'menuclick' at the reported centre then actually started the game (title sequence in the log). It cannot report a silent nothing: it refuses with 'NOTHING was tested' when the launcher document is absent or hidden, and it states the denominator (rows examined) in every line so '0 occluded' is never confusable with 'I never looked'. Coordinates are quoted in the CONTEXT's own dimensions (mContext->GetDimensions(), not the cached mWidth/mHeight, which were stale and would have invited comparing boxes against the wrong size).

## Known failure modes

(none recorded yet)
