# Majora form controls, Goron roll, and phase-tour gate

## Symptom

The existing MM force layer could drive 22 established states but had no reliable headless path to
request a real transformation and enter the Goron roll controller. Separately, the MM3D CSAB phase
evidence covered only three scenes and had no repeatable wider tour gate.

## Reproducing tooling

Player controls are exposed through the existing transport:

- `tools/mm_control.py info` reads runtime/save form, owned/equipped masks, action/request fields,
  state flags, speed, and position.
- `tools/mm_control.py form goron` requests the real asynchronous mask path; poll `info` rather than
  treating dispatch as completion.
- `linkstate goronroll` enters the shipping Goron-roll setup only after the runtime form is Goron.
  Hold stick input and sample `linkinfo` twice to prove action persistence and movement progression.

`tools/mm_phase_tour.py` is the CLI for a serial twelve-scene driver built from focused session,
artifact, orchestration, catalog, report, and direct runtime owners. With
`ZELDA3D_MM_PHASE_REPORT=1`, it rejects the wrong scene, a zero final denominator, any unmapped
animation, and a static model/clip pair with at least two samples per actor. Output belongs under
gitignored `scratch/mm_phase_tour/`.

## Root cause

The reliable control surface must enter shipping MM action paths. Form changes are asynchronous and
owned by `Player_UseItem` plus the normal transformation handler; directly mutating runtime form,
save, object, or mask state would bypass the behavior being tested. Goron roll likewise starts at
the normal Goron A-button installer `func_80836B3C`, which delegates to the real controller and
`Player_Action_96`.

The phase driver must validate the shipping reporter instead of duplicating animation selection or
phase math in Python. This keeps one implementation of the behavior and makes the tour a falsifier,
not a parallel oracle.

## Dead ends

- Direct writes to form/save/object state would create a synthetic state and are forbidden.
- Returning success from the form request cannot prove that the action gate accepted or completed
  the transition.
- A zero-sample or all-static phase report is not clean coverage; it is a failed instrument.
- A clean twelve-scene tour would cover those scenes only, not the whole game or the PARTIAL-15
  mapping policy.

## Fix / status

New control behavior lives in focused `2ship/2s2h/zelda3d/mm3d_player_force.c`; the 22,000-line
Player overlay does not grow. Under `zelda3d/repl/`, `mm3d_repl.*` composes focused transport,
framing, lifecycle, command parsing/routing, world, scene, model, and Link owners;
`mm3d_link_repl.{cpp,h}` owns Link command parsing/readout, and `tools/mm_control.py` owns CLI
shorthands. The CLI-only phase-tour entry point delegates session/process ownership, artifact
layout, orchestration, scene catalog, and report parsing to focused Python modules; the old
`mm_runtime.py` facade is deleted and its offline falsifier suite passes.

Status: **IN PROGRESS**. Static Clang/symbol/format/Python checks passed. The new transformation,
Goron-roll, and twelve-scene phase paths have not run live on the changed tree, so they add no new
runtime parity evidence yet.
