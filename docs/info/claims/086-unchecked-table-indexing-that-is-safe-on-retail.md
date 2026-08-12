---
id: C086
kind: claim
status: holds
created: 2026-08-12
tags: audit,port-only-callers,issue-0023
depends: docs/issues/0023-unchecked-table-indexing-reachable-from-port-only.md
---

## Claim

Unchecked table indexing that is safe on retail hardware but reachable from port-only callers is a systemic category in the vendored decomp, not isolated incidents: 22 confirmed sites across 2197 files.

## Evidence

33-agent audit 2026-08-12, run after the same bug shape was found TWICE by accident in one day (Camera_Init/sInitRegs and Entrance_GetTableEntry). Six scanners over slices of the vendored decomp, each finding checked by a separate ADVERSARY agent instructed to refute it and default to refuted when uncertain: 27 verified, 22 confirmed, 5 refuted. Denominators recorded per slice (mm-core 31 files, oot-core 154, mm-overlays-port 1911, oot-port-layer 31, zelda3d-layer 42, libultraship 28). Two spot-checked by hand rather than accepted: Flags_SetEventChkInf indexes u16[14] by flag>>4 with no bound while the REPL eventflag command parses %i, and the adversary's own correction (that a second eventflag handler in the same else-if chain is dead code) was confirmed correct. Catalogued in docs/issues/0023.

## What would falsify it

Seven further findings were reported but NOT adversarially verified (the run capped verification at 6 per slice) and are deliberately excluded from the catalogue -- so 22 is a floor on the confirmed count, not a complete census, and the true total is higher. The claim fails if the confirmed sites turn out to be dominated by unreachable callers: reachability was argued per-site by the adversary but only two sites (eventflag, warp) have been demonstrated live on the running game.
