---
id: C020
kind: claim
status: holds
created: 2026-07-29
tags: 
reconfirmed: 2026-07-29
---

## Claim

dual-tex modes 2/3 were discarding stages on 170 materials incl. Link's body; now guarded on an exact 2-stage match

## Evidence

Corpus re-derivation: 159 materials at 3 stages + 3 at 4 stages + 8 via mode 2 matched the 2-stage prefix and lost the rest. link_v2 mat15/mat28, childlink_v2 mat18 among them. No regression at Kokiri after the guard.

## What would falsify it

a title-screen measurement showing the 8 affected title_logo materials changed appearance — NOT yet checked, plain warp to ENTR_TITLE_0 renders sky only

## Re-confirmed 2026-07-29

Title VERIFIED (the gap flagged in the original claim is closed). Booted the real Opening->title demo with ZELDA3D_WARP= and measured the wordmark's OWN pixels (20536 px present in all three frames), which is background-independent since the logo is screen-anchored while the camera sweeps: pre (249.56,18.27,16.43) vs post (249.31,18.04,16.19), a ~0.1% per-channel shift, with per-pixel mean delta 0.79 against a 0.40 same-build control. No regression to the CLOSED title row.
