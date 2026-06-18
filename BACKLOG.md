# SoH3D backlog

Durable, in-repo task list (survives PC switches and fresh agent sessions). Keep this honest:
when an item lands, move it to **Done** with the commit; when you learn something, note it inline.
Mirrors the live task tracker but this file is the source of truth across sessions.

## Open

1. **Vulkan upside-down framebuffer** — the title-screen 3D backdrop and the pause/inventory
   background render upside-down on the Vulkan backend. Cause: framebuffers are drawn back as 2D
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
