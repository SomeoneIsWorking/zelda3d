# Handoff — title-logo phase gating + headless harness (2026-07-09)

Session was interrupted after landing (uncommitted) the next slice of the OoT3D title-screen
faithful-port arc. **No verification run yet** — the build is clean and the game loads the new
cs parsing, but per the project rule ("every fix MUST post evidence before it leaves
in-progress") nothing has been screenshotted against the new behavior. The next session's
first job is to verify, then commit, then continue.

## What changed this session (uncommitted, working tree is dirty)

1. **Headless runs for the harness** — `tools/soh3d_harness.sh` + `tools/harness_ctl.py`
   now run on a private Xvfb `:99` with `SOH3D_HARNESS_HEADLESS=1`. The previous setup
   opened a real Vulkan window on `DISPLAY=:0` (the GPU backend ignores the stale
   `SOH_HEADLESS=1` env var the harness used to set). `_ensure_headless_env()` in
   `harness_ctl.py` brings up Xvfb lazily on the first `spawn()`. Wayland-safe.

2. **Asset symlinks near the harness binary** — `Azahar/build-libretro/bin/Release/` now
   has symlinks to `oot.o2r`, `soh.o2r`, and `assets/` (all pointing into
   `Shipwright/build-cmake/soh/`). Without these the harness's `soh_boot` aborts on a
   "missing assets/" popup. **These symlinks are NOT committed** (absolute-path-dependent);
   re-create them if the harness binary is rebuilt elsewhere:
   ```
   ln -sf $REPO/Shipwright/build-cmake/soh/oot.o2r  Azahar/build-libretro/bin/Release/oot.o2r
   ln -sf $REPO/Shipwright/build-cmake/soh/soh.o2r  Azahar/build-libretro/bin/Release/soh.o2r
   ln -sf $REPO/Shipwright/build-cmake/soh/assets  Azahar/build-libretro/bin/Release/assets
   ```

3. **Extended cs parsing** — `Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp` now decodes
   op-0x03 (misc triggers), op-0x7c (transition/fade), and op-0x3e8 (destination sentinel)
   from the spot99 " BDQ" stream. Previously only OP97 (camera), OP0A (rider cues), and
   OP8C (time) were parsed; OP03/OP7C/OP3E8 fell through to the generic 48-byte stride
   and were silently dropped. New C API in `zelda3d_cutscene.h`:
     - `int  Zelda3D_TitleCsMiscTriggerFrame(uint16_t sub)` — first cs frame at which the
       given op-0x03 sub-op fires, or -1. Sub-ops of interest (byte-confirmed in
       `oot3d-decomp/docs/title_gamestate_driver.md` §3):
         `0x1e` = Flags_SetEnv(play,3)  → logo FADE_IN   (title cs: frame 345)
         `0x1f` = Flags_SetEnv(play,4)  → logo FADE_OUT  (title cs: frame 1930)
     - `int  Zelda3D_TitleCsScreenFade(int* start, int* end)` — op-0x7c window
       (title cs: 2310..2460, the screen-level fade straddling the loop point).
     - `int  Zelda3D_TitleCsLoopFrame(void)` — op-0x3e8 destination sentinel; returns
       the BDQ header's end_frame (2400) if the cs had a destination marker, else -1.

4. **Logo phase gating** — `Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp`
   was drawing the logo continuously while title was active (always-on, free-running
   csab playhead). Now it:
     - Skips the draw entirely before the fade-in trigger (cs frame < 345) and after the
       fade-out completes (cs frame > 1930 + kFadeFrames).
     - Drives the csab playhead from `csFrame - fadeInFrame` clamped to [0, 119], so the
       120-frame letters-fly-in assembly animation plays ONCE when the logo appears and
       holds the end pose during the display phase. (Previously the playhead was a
       free-running float, so the assembly animation never aligned with the trigger.)
     - Applies a per-frame alpha: 0 → 255 over `kFadeFrames = 35` frames after fade-in
       (Display phase: alpha = 255); 255 → 0 over 35 frames after fade-out.

   **STOPGAP — alpha fade rate** (clearly marked in the file header): the +6/frame /
   35-frame rate is **N64's** `En_Mag_Update` `mainAlpha` ramp (`Shipwright/soh/src/
   overlays/actors/ovl_En_Mag/z_en_mag.c:226`), scaled so N64's mainAlpha cap of 210 maps
   to full 255 over the same wall-clock duration. OoT3D's En_Mag-equivalent actor is
   **not yet decompiled** (`title_gamestate_driver.md` §4 open item #1); its actual rate
   will likely differ (single wordmark vs N64's multi-layer effectAlpha+mainAlpha+
   subAlpha+copyrightAlpha). Replace `kFadeFrames`/`kFadeStep` with the OoT3D-derived
   rate once that actor is RE'd.

## Current state at handoff

- Build: **clean** (`ninja soh.elf` succeeds; the four touched files compile).
- Boot: **clean** — `tools/zelda3d_game.sh start` (with `ZELDA3D_WARP=` empty, to boot
  the real title-demo flow) loads the game and the run log shows the new parser firing:
  `[Zelda3D] title cs: 15 rider cues, 2 misc cues, 1 fade cues, hasDest=1`.
- Verification: **NOT DONE**. No screenshots captured at the new phase boundaries.

## What the next session should do first (in order)

1. **Verify the phase behavior with screenshots.** Launch the title demo headless:
   ```
   ZELDA3D_WARP= tools/zelda3d_game.sh start
   ```
   Then via `tools/zelda3d_repl.py` (or the harness `tools/title_ab.py`, now headless),
   capture frames at the phase boundaries and confirm:
     - cs frame < 345 (e.g. 200): **no logo visible**
     - cs frame 345 + 0..35 (e.g. 360, 380): logo fading in
     - cs frame 345 + 35 .. 1930 (e.g. 500, 1000, 1500): logo at full alpha
     - cs frame 1930 + 0..35 (e.g. 1950): logo fading out
     - cs frame > 1965 (e.g. 2000, 2100): **no logo visible**
   The SoH-side title-cs cursor lags the oracle's by a phase offset (see
   `debug_journal/2026-07-08-title-daytime-schedule-re.md`) — drive off SoH's OWN
   `Zelda3D_TitleCsFrame()` for these checks (that's what the code reads), not the oracle's
   frame number.

2. **If verified, post evidence + commit.** The card on the kanban for this is not yet
   filed — open one with `tools/kanban.py add --title "Title: logo phase gating from cs
   misc triggers (fade-in @345, fade-out @1930)" --labels type:render,anim` once the
   screenshot is in hand. Or just commit on `main` with a clear message citing the
   decomp doc; the change is small and self-contained.

3. **Then continue the title-port arc.** Ranked open gaps (largest first):
     a. **spot99 scene itself** — SoH3D renders `spot00` (full Hyrule Field, SCENE_HYRULE_FIELD
        in `title_presentation.cpp:49`) but OoT3D loads `spot99` (separate scene, ~13% the
        collision poly count, ~79% the room mesh size). See
        `oot3d-decomp/docs/title_scene_spot99.md` §7 for the concrete port spec; the
        existing spot00 import pipeline needs re-running against `spot99_info.zsi` +
        `spot99_0_info.zsi`. Largest single visual-divergence cause.
     b. **OoT3D En_Mag-equivalent actor RE** — would replace the STOPGAP fade rate above
        with the real one. Open decomp item (`title_gamestate_driver.md` §4 #1). Attack:
        live-oracle actor-list walk at settled title filtering for the actor whose
        `objBankIndex` resolves to `zelda_mag.zar`, OR a disasm sweep for register-offset
        readers of `play + 0x5f98` (the env-flags bitfield the cs interpreter writes).
        The MOVW/MOV-with-0x5f98 sweep already tried this session returned 0 hits — the
        compiler builds the offset via two `ADD Rd, Rn, #0x5f00` + `#0x98` (18 candidate
        sites found; none are the En_Mag reader — they're envCtx accesses at +0x5f00 for
        other fields). The reader probably uses a different shape (constant pool, or
        reads via a computed index); needs more work. NOTE: spot99's static actor table
        does NOT contain an En_Mag-shaped id, so it's dynamically spawned somewhere —
        start by finding the spawner.
     c. **Logo overlay is 3D-camera-relative, not true 2D orthographic** — the placement
        works for the title cs's fixed camera path but is technically a hack; the proper
        port is a dedicated ortho/screen-space CMB draw pass. See
        `title_2d_overlay_logo.md` §5.1 for the spec.
     d. **Screen-level loop fade not applied** — op-0x7c (cs frames 2310..2460) is parsed
        but not consumed; the engine's `transitionType` / fade needs driving from the
        ported cs's fade window. Separate from the logo alpha (which is its own fade).
     e. **g_title_fire cmab + copyright block** — Phase 3 of `title_2d_overlay_logo.md`
        §5. The fire-glow effect on the wordmark and the `copy_nintendo.cmb` copyright
        line below it are not drawn at all yet.
     f. **Title-cs cursor phase sync** — `debug_journal/2026-07-08-title-daytime-schedule-
        re.md` documents that the formula is correct but SoH's cursor runs at a different
        phase than the oracle's, so all cs-derived state (dayTime, lighting, logo phase)
        lands at a different wall-clock instant than Az. Not a faithfulness-of-formula
        issue; it's a when-does-cs-frame-N-occur issue. Lower priority than the above
        because the cs-derived values are each correct IN ISOLATION.

## How to run / verify (key facts)

- **Headless always**: `ZELDA3D_HEADLESS=1` for the game (NOT `SOH3D_HEADLESS=1`, which
  is stale and silently ignored). The harness uses `SOH3D_HARNESS_HEADLESS=1` (separate
  var, plus the new Xvfb setup brings up `:99` automatically).
- **Boot the title demo** (not warp-to-gameplay): set `ZELDA3D_WARP=` (empty) in the env
  or pass nothing — `tools/zelda3d_game.sh start` defaults `ZELDA3D_WARP=1` (auto-warp).
  The title flow only runs with the warp disabled.
- **Oracle compare (now headless)**: `source .env && tools/title_ab.py ab <az_frame>
  --soh <soh_frame> --name <label>` drives both engines in one process at matched content
  frames and writes side-by-side PNGs into `scratch/title_ab/`. The harness binary lives
  at `Azahar/build-libretro/bin/Release/soh3d_harness` and needs the asset symlinks
  (recreate per the command above if missing).
- **REPL**: `tools/zelda3d_repl.py` against the running game (FIFO at
  `scratch/zelda3d.ctl`). `soh_titlecs` REPL command pins/overrides the cs cursor
  (use sparingly — see the warning in `tools/title_ab.py`'s file header about why
  `soh_titlecs` is bad for A/B calibration: it re-derives dayTime from a stale schedule).

## Key files / docs to read first (don't re-walk solved ground)

- `debug_journal/2026-07-08-title-divergence-remeasure.md` — quantified real divergences
  at content-matched frames (terrain ~2-2.6x dark, stars too dim, sky frozen R/G).
- `debug_journal/2026-07-08-title-daytime-schedule-re.md` — why the dayTime formula is
  right but the cursor phase isn't, and what NOT to "fix" (the formula).
- `debug_journal/2026-07-08-oot3d-title-module-design.md` — the `Zelda3D::TitlePresentation`
  module design (already partially landed in `behaviors/title/`).
- `<oot3d-decomp>/docs/title_gamestate_driver.md` — the master synthesis
  of title-demo gamestate + per-frame driver + logo phase timing. Read this first.
- `<oot3d-decomp>/docs/title_2d_overlay_logo.md` — the 2D-overlay port spec
  (logo assets + placement + timing).
- `<oot3d-decomp>/docs/title_scene_spot99.md` — the spot99-vs-spot00 scene
  gap + concrete port recommendation.

## Quick links into the changed code

- `Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp` — `MiscCue`/`FadeCue`/`sHasDest`
  structs and the new op-0x03/0x7c/0x3e8 branches in `Zelda3D_TitleCsLoad`'s walk loop;
  new exports `Zelda3D_TitleCsMiscTriggerFrame` / `Zelda3D_TitleCsScreenFade` /
  `Zelda3D_TitleCsLoopFrame`.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp` — `resolveLogoPhase()` +
  the `LogoPhase` enum + the alpha-resolution block at the draw site. STOPGAP marker at
  the top of the file calls out the N64-derived fade rate.
- `tools/harness_ctl.py` — `_ensure_headless_env()` (called from `spawn()`).
- `tools/soh3d_harness.sh` — `setup_headless` bash function (mirrors `zelda3d_game.sh`).

## Verification result (2026-07-09)

Live-game quantified check PASSED:

- Logo absent at cs frames 200 and 2000 (outside the display window).
- Full alpha consistent at cs frames 500, 1000, 1500 (steady-display phase).
- Partial alpha at cs frame 1948 (inside the fade-out ramp starting at 1930).
- Fade-in window (cs 345-380) is NOT visually confirmable in this pass: the title-cs
  camera pitches into a ground close-up at cs ~355-392, so the logo-overlay region is
  off-frame/occluded during that exact window. This is a known open item — the logo is
  currently drawn as a camera-relative overlay rather than a true 2D ortho overlay, so
  camera framing during the cs can obscure it. Follow-up: port the logo draw to a fixed
  2D ortho pass (see `title_2d_overlay_logo.md`) so fade-in is checkable independent of
  camera pitch.
- Evidence PNGs: `scratch/screenshots/titlecs_*.png` (gitignored, not committed).
