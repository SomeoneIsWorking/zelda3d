# Zelda3D agent notes

Global working policy arrives through the parent `AGENTS.md` discovery link. Its editable authority
is `shared/re-harness/instructions/AGENTS.md`; this file contains only Zelda3D-specific details.

## Project and ownership

Zelda3D places 3DS presentation and recovered behavior over two peer N64-source PC engines:
SoH/OoT under `Shipwright/soh/` and 2Ship/MM under `2ship/`. The 3DS layers are `soh3d` and
`2ship3d`; both use the `Zelda3D_` interface. `docs/project-structure.md` owns the complete naming,
source layout, dependency direction, and release packaging map. Use its current paths rather than
copying a layout from another port.

Start nontrivial work with `tools/info.py brief <terms>`. Consult `docs/project-goals.md` for durable
outcomes, `docs/project-state.md` for current capability status and focus, `docs/issues/` for atomic
work, `docs/codemap.md` for ownership and placement, and `docs/re-frontier.md` for the next grounded
RE dependency. Update only the authority whose answer changes. `docs/parity-map.md` records specific
oracle comparisons; a closed case needs new regression evidence or a user request before reopening.
`docs/parity-workflow.md` describes the comparison method. `KANBAN.md` and `debug_journal/` contain
earlier reports and evidence; consult them for provenance, then record current issues and capability
status in their canonical authorities.

The launcher composes the two game cores. Keep actor behavior in focused modules under each game's
`zelda3d/` tree and renderer mechanics in their existing resource, pipeline, pass, lifecycle, and
shader owners. Use `docs/codemap.md` to find the precise owner before editing. The existing
`core/zelda3d.c` is not a destination for new behavior. Preserve the N64-source game logic that is
not coupled to the 3DS presentation boundary.

`oot3d-decomp/` and `mm3d-decomp/` are pinned reference submodules, not shipping inputs. For a
behavioral divergence, recover the relevant 3DS behavior there and port the proven difference
through the owning module. SoH's N64 struct-offset comments do not describe this 64-bit process's
layout after pointer fields; read typed C fields instead of probing guessed offsets. Record new
binary findings in the relevant decomp docs and project RE authority.

## Driving and verification

`./run.sh` launches the intended user product. For automated OoT sessions, use
`tools/zelda3d_game.py start` and `tools/zelda3d_repl.py`; the manager is headless by default. The
embedded, windowless Azahar oracle is built through `tools/soh3d_harness.py` and controlled through
`tools/harness_cli.py`. The harness's `gameplay` response distinguishes gameplay from title-demo
`PlayState`; `warp` requires a loaded save. Extend the harness when a needed observation is missing.

Reproduce the failing state through the shipping interaction path before changing behavior. Use
the oracle and production probes for a focused discriminator, then exercise the live user path for a
user-visible fix. A forced pose or one-frame comparison alone cannot prove the full interaction.
`docs/lus_input_architecture.md` explains the existing physical-input to N64-pad route when input
work is involved. For oversized source inspection, `tools/codequery.py` provides `outline`,
`slice`, `def`, `callers`, and `find`.
