# Zelda3D agent notes

## Project and ownership

Zelda3D places 3DS presentation and recovered behavior over two peer N64-source PC engines:
SoH/OoT under `Shipwright/soh/` and 2Ship/MM under `2ship/`. The 3DS layers are `soh3d` and
`2ship3d`; both use the `Zelda3D_` interface. `docs/project-structure.md` owns the complete naming,
source layout, dependency direction, and release packaging map. Use its current paths rather than
copying a layout from another port.

Docs: `docs/project-goals.md`, `docs/project-state.md`, `docs/issues/`, `docs/codemap.md`, and
`docs/re-frontier.md` (`tools/re_frontier.py next` / `hacks`). `docs/parity-workflow.md` describes
how a host-vs-oracle comparison is run.

The launcher composes the two game cores. Keep actor behavior in focused modules under each game's
`zelda3d/` tree and renderer mechanics in their existing resource, pipeline, pass, lifecycle, and
shader owners. The existing `core/zelda3d.c` is not a destination for new behavior. Preserve the
N64-source game logic that is not coupled to the 3DS presentation boundary.

`oot3d-decomp/` and `mm3d-decomp/` are pinned reference submodules, not shipping inputs. For a
behavioral divergence, recover the relevant 3DS behavior there and port the proven difference
through the owning module. SoH's N64 struct-offset comments do not describe this 64-bit process's
layout after pointer fields; read typed C fields instead of probing guessed offsets. Binary findings
go in the relevant decomp docs.

## Driving and verification

`./run.sh` launches the intended user product. For automated OoT sessions, use
`tools/zelda3d_game.py start` and `tools/zelda3d_repl.py`; the manager is headless by default. The
embedded, windowless Azahar oracle is built through `tools/soh3d_harness.py` and controlled through
`tools/harness_cli.py`. The harness's `gameplay` response distinguishes gameplay from title-demo
`PlayState`; `warp` requires a loaded save. Extend the harness when a needed observation is missing.

`docs/lus_input_architecture.md` explains the existing physical-input to N64-pad route. For
oversized source inspection, `tools/codequery.py` provides `outline`, `slice`, `def`, `callers`, and
`find`.
