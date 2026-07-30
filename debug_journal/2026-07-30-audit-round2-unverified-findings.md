# Audit round 2 — 12 UNVERIFIED findings (do not read the run's `confirmed: 0`)

The round-2 sweep reported `confirmed: 0`. **That number is an artefact.** 27 of 29 agents died on
API 500/529, including EVERY adversarial refuter, and the workflow script's survivor test was
`cast > 0 && refuted < cast` — so with zero votes each finding evaluated to "does not survive" and
landed under `dismissed`. Twelve genuine findings were filed as refuted without a single refuter
having run. (Script fixed: zero votes now reports UNVERIFIED. Same failure mode this project keeps
finding in its own code — an instrument returning nothing is indistinguishable from a negative
result unless it says so.)

COVERAGE: only 2 of 5 areas ran (behaviours, HUD/input). **player, animation and scene never
executed at all** and remain unswept.

STATUS of everything below: found by one agent, NOT adversarially verified, NOT reproduced by the
operator. Treat as leads. Numbers are the agent's own.

## Auto-replace / actor model selection (behaviours area)

1. **`model/zelda3d_model.cpp:918` — "largest non-debris, non-flat CMB identifies the object a mapped
   ZAR stands for"**. **RE-SCOPED BY THE OPERATOR 2026-07-30 — premise confirmed, headline examples
   REFUTED, and the real case is different from the one reported.**

   The premise is right: ZARs do pack multiple models. Confirmed on the ROM — `zelda_rd` holds
   `redead.cmb` + `gibud.cmb`, `zelda_st` holds `staltula` + `staltula_gold`, `zelda_mb` holds
   `molblin` + `bossblin`.

   But the reported consequence does NOT follow. Every one of those models is SKINNED (bones 17, 11,
   16), and `loadAutoModel` sets `out->skinned = bones().size() > 1`, after which the auto path SKIPS
   the actor and leaves the N64 model. So "ReDeads as Gibdos / Skulltulas as Gold Skulltulas /
   Moblins as the big Moblin" cannot happen — those actors are not auto-replaced at all. An
   adversarial refuter would have caught this; none ran (all died on 529).

   Measured partition over all 312 mapped ZARs:
       147  have >= 2 non-debris CMB candidates
        23  ALL candidates skinned  -> auto path skips, renders N64, no wrong model
       104  have >= 2 STATIC candidates -> the pick CAN render a wrong mesh

   And the 104 are dominated by OBJECT COLLECTIONS rather than variant pairs:
   `zelda_bdan_objects` alone packs 16 distinct Water Temple props (six door variants, two switches,
   spikes, a pedestal, water); `zelda_demo_kekkai` packs 16; `zelda_ddan_objects` 5. One object id
   maps to the whole collection, so a single pick is served to every actor that shares that object.
   THAT is the real defect, and it is the "dungeon mechanisms all as one mesh" half of the original
   claim — not the character half.

   STILL OPEN: how many of the 104 actually reach the screen. The auto path also needs the MEASURE
   opcode to fire and a scale to resolve, and finding 3 claims 13 object ids can never render at all,
   so some of the 104 fall back to N64 for unrelated reasons. Counting the ones that genuinely render
   a wrong mesh in game is the next step.
2. **`render/zelda3d_render.cpp:454` — "one auto-derived world scale per object id is right for every
   instance"** (131 affected). Params-sized props render at one frozen size.
3. **`core/zelda3d.c:1105` — "bracketing an actor's N64 draw with MEASURE measures that actor"**
   (13 object ids). Those ids can never render their OoT3D model, and the failure is SILENT — a
   coverage audit reading the table counts them as replaced.
4. **`model/zelda3d_model.cpp:916` — "skipping flat/debris CMBs is enough for 'most vertices' to
   select the in-world model"** (8). Temple of Time pedestal/sword and some get-item models.
5. **`render/zelda3d_render.cpp:634` — "the auto cache is a per-object memo safe to retain for the
   process lifetime"**. **OPERATOR CONFIRMED 2026-07-30 (mechanism, by code path).**

   Two facts combine:
   * `sPendingMeasureKey` is a SINGLE slot (`zelda3d_render.cpp:635`) and is assigned with **no
     guard** — `Zelda3D_TryAuto` does `e->tries++; e->state = 1; Zelda3D_EmitMeasure(...);
     sPendingMeasureKey = objId;` unconditionally (~:886-893). If a second object needs measuring in
     the same draw pass it overwrites the first's key, orphaning the first's bracket: its measurement
     is lost, but its `tries` was already spent.
   * `if (e->tries >= 8) { e->state = 3; }` (~:886) and `state == 3` returns N64 forever
     (~:807) — there is no reset, no per-scene invalidation, nothing that clears it.

   So an object first encountered in a frame that also introduces other new objects can burn all
   eight attempts losing the slot and then render as N64 **for the rest of the process**. Whether
   that happens depends on what else appeared alongside it, which is why it is non-deterministic.

   NOT measured: how often this actually fires at runtime. The mechanism is certain from the code;
   the frequency is not, and a crowded-scene test would be needed to quantify it.

   METHODOLOGY CONSEQUENCE, and I checked my own work against it: an A/B capture of an
   AUTO-REPLACED actor is unreliable across launches for this reason alone. This session's
   measurements are mostly unaffected because they targeted Link (dedicated player path, not auto),
   scene room CMBs (water, courtyard window — not auto) and HUD sprites. The one exception is the
   Heart Container A/B, where I spawned `Item_B_Heart` and saw no change; I attributed that to
   testing the wrong asset (the 12 affected materials are the GET-ITEM models), and that reading
   still holds — but this cache is a second possible explanation for a null result there.
6. **`render/zelda3d_render.cpp:892` — the `sActorForcedAuto` per-actor forced-CMB slot**. Wooden-torch
   Obj_Syokudai stays N64; the shared-ZAR mechanism is broken for static props generally.
7. **`tables/zelda3d_object_zars.inc:5` — "312/402 mapped means the other 90 have no OoT3D archive"**.
   **OPERATOR VERIFIED 2026-07-30 — partly right, cause identified, and smaller than claimed.**

   Swept all 68 unmapped `OBJECT_*` entries against every `/actor/*.zar` in the ROM. Only **5** have a
   plausible unused archive by name, and 2 of those are `OBJECT_LINK_BOY` / `OBJECT_LINK_CHILD`, which
   are deliberately excluded (Link goes through the dedicated player path, not auto-replace). So the
   real gap is **three** objects, not the sweeping "90 have no archive":

       OBJECT_PU_BOX   -> /actor/dk_pu_box.zar   [pu_box1/2/4_model.cmb]  -- 3 variants
       OBJECT_TRAP     -> /actor/dk_trap.zar     [trap_model, trap2_center_model] -- 2 variants
       OBJECT_VASE     -> /actor/dk_vase.zar     [vase1_obj_o2.cmb]       -- SINGLE model

   CAUSE: `tools/gen_object_zars.py` resolves `zelda_<base>.zar` then bare `<base>.zar`
   (gen_object_zars.py:62). OoT3D groups dungeon props under a `dk_` prefix, which the generator
   handles ONLY through explicit `ALIAS` entries — it already has `dk_lightbox`, `shop_tana`,
   `kogoma` — and these three simply have no entry. So the header's "no OoT3D ZAR" is an inference
   from a failed name lookup, not a fact about the ROM.

   NOT enabled here, deliberately. `dk_vase` is a clean single-CMB candidate, but `dk_pu_box` and
   `dk_trap` are multi-variant COLLECTIONS and would walk straight into finding 1's pick ambiguity
   (one picked mesh served to every instance). And enabling a 3DS model without seeing it in game is
   the "looks finished but isn't" failure this project explicitly guards against. The safe increment
   is `OBJECT_VASE` alone, once someone can frame an En_Vase and confirm it.
8. **`core/zelda3d.c:757` — `Zelda3D_ActorHasReplacement` "mirrors the lookups in TryDrawActor/TryAuto"**
   (internal). Mapped-object doors keep drawing AND updating past the vanilla cull distance.

## HUD / input area

9. **`hud/zelda3d_hud.cpp:205` — the virtual->pixel mapping derived from framebuffer 0's size alone**
   (user-visible). Claimed wrong X and horizontal size under Advanced Resolution with a forced aspect,
   or Low Res "N64 Mode"; right-anchored elements run off-screen.

   **OPERATOR PROGRESS 2026-07-30 — mechanism narrowed, still UNVERIFIED.**
   * The chain is real: `zelda3d_hud.cpp:205` computes `sOriginX = 160 - 120*(W/H)` from
     `Zelda3D_Hud_Begin`, which returns `GfxRenderingAPISdl3Gpu::MainFbSize` =
     `mFramebuffers[0]` (backend fb 0 — the render target that gets blitted to the swapchain,
     `gfx_sdl3gpu.cpp:1010`). The interpreter's own aspect is `mCurDimensions.width/height`
     (`interpreter.cpp:5295`), i.e. the GAME CANVAS. Those are two different sources, which is
     precisely the finding's premise. Advanced Resolution DOES exist in this fork
     (`soh/soh/SohGui/ResolutionEditor.cpp`) with `gAdvancedResolution.AspectRatioX/Y`.
   * What is NOT established: whether backend fb 0's aspect actually diverges from the canvas
     aspect when a ratio is forced. If fb 0 is resized to the forced aspect, the two agree and the
     finding is moot.
   * **Reproduction attempt FAILED — do not repeat it as-is.** `cvari gAdvancedResolution.Enabled 1`
     + `AspectRatioX 4` + `AspectRatioY 3` on a running 800x480 instance produced NO letterboxing
     (non-black column range stayed x[0:799] before and after). The CVars are read at framebuffer
     setup and nothing re-applied them, so the condition never engaged. Next attempt must either
     trigger the framebuffer resize path (a window resize / `UpdateFramebufferParameters`) or set the
     CVars in the config BEFORE launch.
   * NOTE `cvari` PERSISTS to config. I set and then reverted these three to 0 — check them if the
     launcher ever behaves oddly.
10. **`hud/zelda3d_hud.cpp:229` — the `gSPZelda3DHudFlush` marker composites "at that point"**.
    Claimed the minimap image renders nothing once internal resolution is raised, MSAA is on, or on
    macOS — the interpreter-drawn compass arrows then float over an empty patch.
11. **`z_parameter.c:5084` — hardcoded HUD source-texture dimensions** (6). Wrong crop with an
    alt-assets/HD pack; invisible without one.

## A correction to THIS repo's own notes

12. **`model/zelda3d_model.cpp:1367`** — the agent contradicts a claim I wrote in
    `debug_journal/2026-07-29-csab-linear-int16-rotation-stride.md`: that tectite's CMB is never
    loaded *because* it is absent from the auto-replace table. It says the table-absence explanation
    is wrong for tectite. My own evidence was weaker than I presented it: I spawned En_Tite, found no
    `aTest=1 aRef=0.000` group among the drawn models, and concluded the CMB never loads — but absence
    of that one material is not proof the CMB never loaded. The empirical observation stands; the
    stated CAUSE does not. Corrected in that file.
