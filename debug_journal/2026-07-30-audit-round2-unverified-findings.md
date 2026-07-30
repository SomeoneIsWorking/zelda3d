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
   process lifetime"**. Claimed: non-deterministic session-long loss of a replacement depending on
   how many instances were on screen when the object was first seen — the same scene can differ
   between runs. If true this also undermines any A/B measurement of auto-replaced actors.
6. **`render/zelda3d_render.cpp:892` — the `sActorForcedAuto` per-actor forced-CMB slot**. Wooden-torch
   Obj_Syokudai stays N64; the shared-ZAR mechanism is broken for static props generally.
7. **`tables/zelda3d_object_zars.inc:5` — "312/402 mapped means the other 90 have no OoT3D archive"**.
   Push blocks, En_Vase and shopkeepers have shipped assets and stay N64.
8. **`core/zelda3d.c:757` — `Zelda3D_ActorHasReplacement` "mirrors the lookups in TryDrawActor/TryAuto"**
   (internal). Mapped-object doors keep drawing AND updating past the vanilla cull distance.

## HUD / input area

9. **`hud/zelda3d_hud.cpp:205` — the virtual->pixel mapping derived from framebuffer 0's size alone**
   (user-visible). Claimed wrong X and horizontal size for anyone using Advanced Resolution with a
   forced aspect, or Low Res "N64 Mode"; right-anchored elements run off-screen. Suggested fix: take
   the aspect from `OTRGetAspectRatio()` instead of re-deriving it.
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
