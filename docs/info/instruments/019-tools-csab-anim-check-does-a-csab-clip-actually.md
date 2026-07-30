---
id: I019
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

tools/csab_anim_check -- does a CSAB clip actually animate (built against the real C++ parser)

## Validated by

Run against BOTH classes: MM3D 585/617 animating after the track-reader fix vs 0/109 before, and the OoT3D control 183/189 both before and after, so it discriminates rather than always-passing. KNOWN FAILURE MODE: with no resolvable archive paths it prints 'archives=0 clips=0 ANIMATES=0 FROZEN=0' cleanly, which is indistinguishable from 'nothing animates' -- I hit this with an empty argument list and briefly read it as a regression. Always check archives= against the number of paths passed before trusting the other columns; the caveat is documented at the top of the file.

## Known failure modes

(none recorded yet)
