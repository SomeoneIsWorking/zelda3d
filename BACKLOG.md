# SoH3D backlog

Durable, in-repo task list (survives PC switches and fresh agent sessions). Keep this honest:
when an item lands, move it to **Done** with the commit; when you learn something, note it inline.
This file is the source of truth across sessions.

> **Fresh-session directive (from the user, 2026-06-18) — READ THIS FIRST:**
> **NEVER ask the user anything. NEVER. Do not even chat or narrate. ONLY WORK.** Pick items from
> this list and FIX them, committing each verified fix yourself (3-repo chain:
> libultraship→fork/soh3d, Shipwright→fork/develop, outer→origin/main). No questions, no status
> messages, no "should I" — just do the work and keep going until the list is done. The user is
> playtesting on Vulkan and will keep dropping new bug reports/screenshots; silently capture each
> new one into this file and keep fixing. Do NOT pause to confirm anything.
>
> Suggested early picks (but use your judgment, just work): #19 (dungeon warps — trivial, indices
> listed, unblocks testing), #13 (wrong entrance spawn points), #14/#16 (systemic climb/void
> collision — one root cause likely clears several), #1 (Vulkan FB upside-down — the big proper fix:
> negative-height viewport, Metal-style). MANY items share root causes (collision/terrain-warp Y
> for #4/#9/#10/#13/#14/#16/#25; En_Ko anim/variant for #7/#8; render/material for #11/#20/#24).
> Read the [[soh3d-vulkan-runsh-and-flip]] memory + `run.sh` first. Run the game via
> `tools/soh3d_game.sh` (skill: soh3d-game-control). Verify with screenshots; commit each fix.

## Open

1. **Vulkan upside-down framebuffer** — the title-screen 3D backdrop, the pause/inventory
   background, AND the equipment-screen Link model all render upside-down on Vulkan (CONFIRMED
   Vulkan-only; 2D icons/sprites on the same screens are correct). I.e. every 3D-rendered-to-
   framebuffer-then-sampled image flips. Cause: framebuffers are drawn back as 2D
   texture rectangles; Vulkan's NDC is Y-down and this backend compensates by flipping vertex Y
   (`GetClipParameters` returned `{true,true}`), so FB-sampled images come out flipped while
   uploaded textures (HUD/sprites) are fine. A per-fb `invertY` tweak was committed but is
   INEFFECTIVE for this bug (verified). Proper fix: switch the Vulkan backend to a negative-height
   viewport (Metal-style, `invertY=false`, Metal returns `{true,false}`) so FB textures sample
   upright; verify normal scene + title + inventory together. Files: `libultraship/src/fast/
   backends/gfx_vulkan.cpp` (`GetClipParameters`, `SetViewport`, present blit), `interpreter.cpp`
   (`SetVertices` line ~2184, `AdjustVIewportOrScissor` line ~2376). Reference: `gfx_metal.cpp`.

2. **Universal Start-skip for cutscenes** — everything that takes control away from Link must be
   Start-skippable: scripted CS cutscenes, onepoint cameras, actor-driven sequences, item-get
   freezes. No auto-skip, no forced-watch. Scripted CS terminators already skip on Start
   (`z_demo.c` `csSkipButton`, frames>20). REMAINING: Player cutscene-mode (`csMode`), onepoint
   cams, actor-driven. User accepts the iterate-on-softlock approach (they report what softlocks).
   (DONE: holding Start skips dialogs — `z_message_PAL.c` all 4 text-advance sites.)

7. **Kokiri kids (En_Ko) unlimited render distance** — replacement only renders within a limited
   distance; raise/remove the draw-distance cull for replaced En_Ko.

8. **Kokiri kids (En_Ko) stuck animation** — auto-replaced kids loop one pose (hands-to-face),
   including a Kokiri who should be SITTING but stands. Wrong N64-anim->CSAB / idle selection. Model
   renders fine. See [[soh3d-n64anim-csab-map]], [[soh3d-shared-variant-models]].

9. **Grass/lilypad area not walkable (collision regression)** — Link can't enter a forest pond-edge
   lilypad patch that "used to work." Check collision / terrainwarp path; needs in-game repro.

10. **Boulder half-clipped underground** — a large rock's render sits too low (bottom buried). Per-
    actor yoff/anchor. Identify rock actor/model (En_Ishi large / Obj rock), raise to ground.

11. **Kokiri sword chest too big** — renders as the large treasure chest; should be the small chest.
    Fix En_Box size/variant/scale.

12. **Skip chest-opening + reliable dialog fast-forward** (part of #2) — opening a chest takes
    control (get-item freeze) and must be Start-skippable. Also dialogs aren't fast-forwarding for
    the user — verify the hold-Start dialog skip (just added) works, and consider enabling the
    SkipText enhancement by default so B/Start fast-advance text instantly.

13. **Wrong entrance spawn points (CLUSTER, some SEVERE)** — entrances land at the wrong spawn:
    (a) Kokiri shop door → Link's house; (b) Kakariko graveyard → immediately transitions back out
    (spawns Link ON the exit/leave trigger); (c) a Kakariko door (screenshot: posted-notices door)
    → enter → fall into the void → respawn inside → fall again FOREVER (infinite void loop, can only
    escape by debug-teleport; can never die/land). Likely one root cause: wrong spawn index /
    spawn-point lookup / door→entrance routing, dropping Link outside the room floor. HIGH priority
    (the void loop is a hard softlock). Check entrance table + spawn handling.

14. **Link drops off EVERY climbable surface halfway** (collision, SYSTEMIC) — not just one ladder;
    all climbables (ladders/vines/walls) drop Link partway up. Likely a general climb-collision
    regression (terrain-warp / OoT3D-vs-N64 collision height for climb surfaces). High-value: one
    root cause probably fixes the whole climb cluster. Cluster with #4 (ladder render too high),
    #9 (grass not walkable).

15. **RmlUi Link model/anim toggle** (user-requested) — a 3-way cycle row:
    `0 = N64 model + N64 anim` (`gSoH3dLinkOn=0`), `1 = 3DS model + N64-retarget anim`
    (`gSoH3dLinkOn=1, gSoH3dLinkAnimSrc=1`), `2 = 3DS model + 3DS-own-CSAB anim`
    (`gSoH3dLinkOn=1, gSoH3dLinkAnimSrc=0`). All three modes already work in code (soh3d.c
    SoH3D_LinkEnabled/SoH3D_LinkAnimSrc). DESIGN: define `extern "C" int gSoH3dMenuLinkMode = 0;`
    in SohRmlUi.cpp (must be DEFINED in libultraship — charcompare also links it — and READ in
    soh3d.c, like gSoH3dMenuWarp). Add a `linkmode="1"` cycle row (Graphics or Debug tab); on
    activate cycle 0→1→2→0 and update `<value>` text (labels: "N64" / "3DS · N64 anim" /
    "3DS · 3DS anim"); refresh its text on menu open (extend RefreshToggleRows). soh3d.c
    SoH3D_ReplPoll: on first frame seed gSoH3dMenuLinkMode from current mode, thereafter apply
    gSoH3dMenuLinkMode → gSoH3dLinkOn/gSoH3dLinkAnimSrc.

16. **Gohma arena void-out** — walking in the Gohma (Deku Tree boss) arena drops Link through the
    floor → void. Collision hole. Repro at entrance 1039.

17. **Gohma model hand-weave** — Gohma needs hand-curated multi-CMB assembly (like kAssemblies in
    [[soh3d-auto-replace]]); generic auto-merge is unsound.

18. **Deku Baba no combat interaction (UNCERTAIN)** — a Deku Baba inside the Deku Tree couldn't be
    hurt / couldn't hurt Link. Only seen AFTER teleporting to Gohma, void-dying several times → game
    over → respawn in Deku Tree room 1, so it may be state corruption from the void deaths, not a
    general Deku Baba bug. Low confidence; revisit once #14/#16 (collision void-outs) are fixed.

20. **Market NPCs render totally wrong** — townsfolk by the Market fountain/bridge render with
    white/untextured bodies and broken zebra-striped clothing (user screenshot, Market day). Wrong
    material/texture or wrong model on the OoT3D replacement for these NPCs (En_Hy townsperson
    variants?). Likely a texture/material-index or UV bug per variant. Repro: Market (entrance 177).
    2nd screenshot: townsfolk in contorted poses with striped white clothing — anim + material both
    look wrong; a correctly-rendered kid (green/blue apron) stands behind them, so it's per-actor.

21. **Giant boulder overlapping Temple of Time** (Market) — a large rock mesh clips into/overlaps
    the Temple of Time building (user screenshot). Misplaced/wrong-scale scene geometry or actor.
    Repro: Market (177), look toward ToT.

22. **Debug-menu warp: day/night (time-of-day) selection** — when warping to a location, let the
    user pick day or night (e.g. Kakariko differs). DESIGN: add a time selector to the warp flow —
    set `gSoH3dForceTime` (soh3d.c, the SOH3D_TIME mechanism) alongside `gSoH3dMenuWarp` before the
    transition. Either a day/night toggle row that seeds the time applied on the next warp, or
    per-warp-row time attribute.

23. **Cucco wing-flap animation not implemented** — the cucco (chicken) OoT3D replacement doesn't
    play its wing-flap anim. Long-tail anim coverage; see [[soh3d-runsh-and-anim-interp]]
    (cucco-type coverage) and [[soh3d-n64anim-csab-map]].

24. **Windmill fan blades render inside the Kakariko well** (instead of water) — a wrong object/model
    is drawn in the well shaft (user screenshot: windmill sails in the well). Likely an object-id →
    ZAR mis-map or a shared-object collision in the OoT3D replacement. Repro: Kakariko well.

25. **NPC walking in mid-air** (Kakariko) — a townsperson (running man?) is animated walking high
    above a building/roof instead of on the ground (user screenshot). Actor Y-placement vs floor;
    same collision/terrain-warp Y cluster as #4/#9/#10/#13/#14/#16.

26. **Death Mountain gate shows wrong model + no collision** (Kakariko) — the closed gate to the
    mountain (guarded by the soldier) renders as a different model (a wooden beam/brick structure)
    and has no collision. Wrong object→ZAR/model mapping for the gate, plus missing collision.
    Repro: Kakariko, the DM Trail gate.

19. **Add dungeon entrances to Debug-menu level-select** (so dungeons are testable). Add warp rows to
    `libultraship/assets/rml/soh3d_test.rml` (Debug pane, `warp="<dec>"`) with these first-room
    indices (from `soh/include/tables/entrance_table.h`): Deku Tree 1, Dodongo's Cavern 5,
    Jabu-Jabu 41, Forest Temple 362, Fire Temple 358, Water Temple 17, Shadow Temple 56,
    Spirit Temple 131, Bottom of the Well 153, Ice Cavern 137, Gerudo Training 9,
    Inside Ganon's Castle 1128. (Boss rooms already present as the "Boss Fight" rows.)

4. **Kakariko ladder render too high** — the OoT3D ladder model renders a bit high, so Link appears
   to float when climbing. Lower it slightly (per-actor yoff / placement). Tune live via REPL
   `yoff`, then bake.

5. **Real polygon stairs for fake flat stairs** — original primary goal. Stairs rendered as flat
   textured planes should become real 3D stepped polygon geometry. NOTE: OoT3D ALSO uses flat
   fake-textured stairs, so this CANNOT be solved by routing to OoT3D geometry — the stepped
   meshes must be PROGRAMMED/GENERATED ourselves (synthesize real steps where the game has fake
   flat stairs).

6. **Epona → OoT3D model** (lower priority) — Epona still renders as the N64 model; actor-
   replacement gap.

27. **More foliage** (LOWEST priority — do last) — add more foliage/vegetation density to the world
    for a lusher look.

28. **Sky looks bad (low-res N64 skybox)** (NORMAL priority — not low) — the sky is the N64 skybox
    (N64 Fast3D path, not OoT3D assets), blurry/low-res. Improve: hi-res sky texture or use the
    OoT3D sky. Note: the texpack (model textures) doesn't cover the N64 sky/HUD path —
    see [[soh3d-texpack]]. (Foliage #27 is the only "extra/lowest" item — foliage is already 3DS.)

## Done (recent)

- **RmlUi "Restart → Title Screen"** — Debug-tab row (`restart="1"` → `gSoH3dMenuRestart` →
  soh3d.c `SoH3D_ReplPoll` → `SET_NEXT_GAMESTATE(Title_Init)` + `NA_BGM_STOP`).
- **run.sh black screen on Vulkan** — root cause: a stale config (`shipofharkinian.json`, cwd-
  relative; run.sh's `cd "$SOH"` loaded a different one than the game-manager) had
  `AdvancedResolution.Enabled=1`, which makes the game render to an offscreen `mGameFb` whose
  composite-to-window used to be SoH's ImGui pass — REMOVED in SoH3D (RmlUi replaced ImGui) — so
  fb 0 stayed black while RmlUi still drew. AdvancedResolution is effectively unsupported now.
- **run.sh cold boot to title screen** — plain `./run.sh` now powers on to the title screen with a
  normal save + normal day/night clock; the dev-warp shortcuts (`SOH3D_WARP`/`ENTRANCE`/`TIME`/
  `COLDBOOT`) are opt-in env overrides, off by default.
- **run.sh no longer erases uncommitted engine edits** — `sync_submodule_to_pin` now preserves a
  dirty submodule working tree instead of hard-resetting it to the pin.
