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

2. **Universal Start-skip for cutscenes + dialogs** — everything that takes control away from Link
   must be skippable by pressing Start: scripted CS cutscenes, onepoint cameras, actor-driven
   sequences, item-get freezes, AND text/dialog boxes. No auto-skip, no forced-watch. Today only
   some scripted CS commands are partly Start-skippable (`z_demo.c` `csSkipButton`, gated by
   scene/gameMode/frames>20); dialogs key off `CVAR_ENHANCEMENT("SkipText")` in `z_message_PAL.c`.

3. **RmlUi "Restart → title screen"** — add a menu item that restarts the game back to the title
   screen (re-init to Title gamestate) without quitting.

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
