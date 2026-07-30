---
id: I014
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

REPL autostate now dumps forced-CMB slots (forced[i] actor/key/state/scale/n64h/model/tries)

## Validated by

It reported the wooden-torch slot as state=0 tries=0 before the spawn and state=2 scale=0.95016 n64h=58.0 tries=1 after, i.e. it distinguishes the two answers rather than printing a constant. Previously it dumped only sAuto[] and could not see forced slots AT ALL, which is how the broken routing survived.

## Known failure modes

(none recorded yet)
