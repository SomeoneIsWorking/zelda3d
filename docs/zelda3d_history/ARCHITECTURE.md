# Zelda3D architecture — unify the existing work, don't reinvent it

> **Status: corrected (user, 2026-06-30).** An earlier version of this file
> ("the N64-host spine decision") **misread the user's intent** and prescribed a
> from-scratch decomp spine that *drops* Ship of Harkinian and Zelda64Recomp.
> That is wrong and is fully superseded by what's below. The corrected direction:
> **reuse the existing, working projects; build on them; do not reinvent the
> wheel and do not throw any work away.**

## What the user actually wants

A single, unified PC app that hosts **both Ocarina of Time and Majora's Mask**,
built on top of work that already exists and already runs:

- **OoT → soh3d.** Ship of Harkinian rendering **OoT3D** (3DS / Grezzo assets)
  instead of N64. Its renderer was **already migrated to a single unified SDL3
  GPU backend** that draws N64 F3DEX + 3DS CMB in one pass, with an **RmlUi** UI.
  This is the user's (and this assistant's) own prior work — mature and playable.
  Lives in a sibling checkout `<repo-root>/soh3d` (GitHub `SomeoneIsWorking/zelda3d`).
- **MM → Zelda64Recomp.** The static recompilation that already plays Majora's
  Mask on PC.

"**Not limited / constrained by the existing projects**" — the user's phrase that
the earlier doc fatally misread — means we are free to **restructure the umbrella
into a proper unified PC game** and to go *beyond* what SoH's Shipwright tree or
Zelda64Recomp's structure would otherwise box us into. It does **not** mean "be
free *of* SoH / libultraship / OTR / ELF." Reuse is the whole point:

> *"Why would I ever want to reinvent the wheel… I don't want to throw anything
> away or reinvent anything."* — user, 2026-06-30

### The SoH nuance (this is what the earlier doc tripped on)

The user **dislikes SoH/libultraship as a *framework/host*** and wants to move
*off* it — they only started on SoH because it is the only OoT PC port that works.
Saying "I want to move away from SoH" is what the prior agent over-read into
"discard everything."

Moving away from the **framework** does **NOT** mean discarding what's inside it.
The following MUST be carried forward (they are the value, not the baggage):

- **The playable OoT game** itself (SoH's patched OoT decomp as it actually runs).
- **SoH's stock gameplay enhancements** — the user likes them: faster block
  pushing, faster climbing, faster King Zora (mweep), text/cutscene time-savers, etc.
- **Our own custom enhancements built on top**, e.g. the **press-to-skip onepoint
  cutscene cameras** (`Shipwright/soh/src/soh3d/soh3d.c` "#2 press-to-skip") and the
  rest of the `src/soh3d/` work (3DS rendering, behaviors, anim retargeting).
- **The 3DS render stack** (unified SDL3 GPU + RmlUi + the cmb3d core).

So the real tension to design around: the enhanced game currently **runs on
libultraship**. "Move off SoH" therefore means **re-hosting the *enhanced* decomp**
(keeping the SoH + custom enhancements) on a different platform layer — NOT swapping
back to a vanilla `zeldaret/oot` decomp (that would throw the enhancements away,
which is exactly what the user does not want). When/whether to do that re-host vs.
keep reusing soh3d as the carrier is the open topology question (below).

## Reused building blocks (do NOT rebuild these)

- **The unified SDL3 GPU renderer** from soh3d — N64 + 3DS in one pass. This *is*
  "unified rendering." It already exists; zelda3d uses it, it is not reinvented.
- **RmlUi UI** (soh3d already integrated it on the SDL3 GPU renderer).
- **The `cmb3d` 3DS asset core**, already cleanly extracted from soh3d into
  `src/cmb3d/` here (CMB/CSAB/ZAR-GAR/CTXB/PICA/ZSI/ZCOL + LzS; OoT3D + MM3D
  verified). Shared by both games.
- **Zelda64Recomp's MM** game logic + its RmlUi launcher (already adapted into
  `assets/launcher/`, see `ATTRIBUTION.md`).

## Zelda3D's job = the unification layer

zelda3d brings OoT (soh3d) and MM (Zelda64Recomp) together under **one renderer,
one UI, one launcher** — reusing the SDL3 GPU + RmlUi stack that soh3d already
provides, rather than standing up a parallel one.

### Topology — SETTLED (user, 2026-07-01)

**MM's game logic (from Zelda64Recomp) is re-hosted on soh3d's existing unified
SDL3-GPU renderer + the shared `src/cmb3d/` core — ONE renderer for both games**,
exactly as OoT already works in soh3d (SoH game logic drawn with OoT3D 3DS models).
The MM side is the analog: Zelda64Recomp MM game logic, drawn with **MM3D** 3DS
models, plus enhancements + intuitive controls.

Rejected, explicitly:
- A second 3DS render path *inside* Zelda64Recomp's own RT64 backend (that would
  stand up the parallel renderer this section says not to).
- Wiring the **prebuilt N64 `Zelda64Recompiled` release binary** — it is N64 models
  in a standalone app; Zelda3D needs MM with **3DS (MM3D)** models. The "hand off to
  each game's existing build" line in the README is the throwaway *unify-now* stopgap,
  **not** the target; the target is this single shared renderer.

The remaining open detail is mechanical, not directional: the exact seam where
Zelda64Recomp's recompiled MM graphics tasks (N64 display lists) are intercepted and
fed to soh3d's renderer instead of RT64 — see `docs/MIGRATION.md` once mapped.

## Status of work already done in this repo (and what it means now)

- ✅ `src/cmb3d/` — reused soh3d asset core, OoT3D + MM3D verified. **Keep.**
- ✅ `assets/launcher/` + `src/render/main.cpp` — the OoT/MM launcher (RmlUi,
  reused from Zelda64Recomp; both sides selectable; remembers last pick). Useful,
  but its RmlUi host should ultimately sit on **soh3d's SDL3 GPU RmlUi path**, not
  a separate SDL_Renderer one.
- ⚠️ `tools/cmb3d_view.cpp` — a from-scratch OpenGL CMB viewer. This **reinvented
  what soh3d's SDL3 GPU renderer already does** and is *not* the path forward. Kept
  only as a throwaway sanity check that the asset core round-trips to pixels; do
  **not** build the engine renderer on it.

## ROMs

ROMs live outside the repo under a local `ROM/{N64,3DS}` tree (OoT/MM `.z64`,
OoT3D/MM3D decrypted `.3ds`), pointed at by env/config; none are committed. soh3d
and Zelda64Recomp expect their own ROM inputs.
