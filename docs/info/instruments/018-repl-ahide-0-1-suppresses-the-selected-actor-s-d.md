---
id: I018
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

REPL ahide <0|1> -- suppresses the selected actor's draw (baked-room-mesh vs actor-drawn test)

## Validated by

Applied to Bg_Treemouth in Kokiri Forest: 9257 px changed against a 574 px same-scene noise floor, and the visual shows exactly the actor's contribution (a dark threshold lip) vanishing while the surrounding tree/tunnel geometry stays intact. MEASURED LIMITATION: it does NOT hide Link, because Player draws through Player_Draw and not Zelda3D_TryDrawActor -- 'asel link; ahide 1' changes only 574 scattered px, so Link is NOT a valid positive control for this tool.

## Known failure modes

(none recorded yet)
