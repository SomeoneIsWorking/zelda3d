# `zelda3d_shared/port/` — one SOURCE, compiled once per game

Port glue that is identical for Ocarina of Time and Majora's Mask **but names game-specific types**,
so it cannot live in the `zelda3d_shared` static library beside it.

## Why this is not the static library

`zelda3d_shared` (the STATIC lib, one directory up) has a hard contract: *nothing in it sees a
game-specific type*. Everything crosses its boundary as plain enums and PODs. That contract is what
lets it be compiled **once** and linked by both games.

The files here break that contract, and there is no way around it. `gu_pc.c` is **byte-identical**
between the two games — and it includes `"z64.h"`, which is OoT's 2,354-line decomp master header in
one tree and MM's 108-line one in the other. Identical source, different compile context. A static
library is compiled once against one include path, so it physically cannot hold this file.

So the sharing mechanism is different: **one source file, two compilations**. Each game's
CMakeLists globs this directory into its own target, against its own headers. Two objects, one
source of truth — which is the part that matters, because a duplicate is not a neutral cost. It is
two files to patch, two to audit, and two that drift apart silently.

This is recorded as claim **C064**. Dusklight, whose layering this project otherwise follows, never
had to solve it: it hosts a single game.

## What may go here

Anything that is (a) genuinely identical for both games and (b) needs a game's headers. Adding a
file is a `git mv` plus deleting the other game's copy — the glob in each game's CMakeLists picks it
up with no further wiring.

## What may NOT go here — read the diff, not the percentage

**A similarity score is text, not code.** `mixer.c` measures ~99% common (21 differing lines in 822)
and reads like pure copy-paste. It is *not* shareable: among those 21 lines are the two games' audio
DMEM base addresses — `0x3C0` in OoT, `0x0330` in MM — and MM's `ROUND_DOWN_16()` on the DMA length.
Sharing it on the strength of the percentage would have mis-addressed every audio buffer in one of
the two games. That is claim **C065**.

Its header `mixer.h` *is* here, because that one depends only on `libultraship/libultra/abi.h` and
is byte-identical. The declarations are shared; the implementation stays per-game.

Before moving a file in: `diff` the two copies and account for **every** differing line. If a
difference encodes a per-game constant, layout or behaviour, the file does not belong here until
that difference is parameterised and both games are verified end to end.
