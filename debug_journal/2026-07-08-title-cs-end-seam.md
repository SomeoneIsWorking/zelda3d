# 2026-07-08 — title cs END SEAM: destination 104 at frame 2400 (attract cycle)

Closes the "cs lifecycle seam" item from the title-cs port arc
(memory `soh3d-title-scene-spot99`, handoff next-list). Before this,
`Zelda3D_TitleCsAdvance` wrapped at end_frame 2400 and the flyover
looped in place forever; the real 3DS fires the script's destination
command there (opcode 0x3E8, dest id 104 — the SAME destination id the
N64 title cs uses) and cycles the attract demos.

## Change

- `z_demo.c`: extracted the N64 terminator's case-104 body into
  `Cutscene_TitleDestination(play)` (spirit temple → death mountain
  crater → cutscene map cycle via `sTitleCsState`). The N64 script's own
  terminator is gated on `!gZelda3dInTitleDemo` — while the 3DS title cs
  drives, the N64 command must not double-fire at ITS (earlier) frame.
- `zelda3d.c` (`Zelda3D_ApplyTitleCam`): when the cs cursor wraps
  (`Zelda3D_TitleCsAdvance()` returns 0) and no transition is pending,
  call `Cutscene_TitleDestination(play)`.

## Verified

Live headless run (vsync off): title flyover plays 0..2400, then the
scene transitions to the spirit-temple attract demo (scene 0x51 →
0x17 with cutsceneIndex 0xFFF2) — observed via REPL `posinfo` poll,
~99 s wall clock. No double-fire (N64 terminator gated).

## Session note — duplication post-/clear (workflow)

This context re-derived the title-cs format from stale worktree
journals and briefly built a DUPLICATE port on the outdated branch
`worktree-title-cs-parity` (spot00-attributed, lerp rider, static
dayTime) before reconciling with origin/main, which already carried the
superior arc (spot99, cue+path rider at 14.2u, flowing time, lighting).
That branch is superseded and deleted; only this end seam (+ the
oot3d-decomp `docs/cutscene_format.md` format doc, which main lacked,
and the up-vector/roll construction cross-check vs live Az) was new.
Lesson recorded in memory: after /clear, reconcile with origin/main and
the memory index BEFORE trusting worktree-local journals.
