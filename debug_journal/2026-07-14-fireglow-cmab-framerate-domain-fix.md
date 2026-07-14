# Fire-glow CMAB frame-domain (60fps vs 30fps) fix — ported and value-verified; cs1093 extent gap NOT explained by this bug (proven, not assumed)

Follow-up to `2026-07-14-title-cs464-composition-exonerated-fireglow-remeasure.md` (Divergence 3),
which flagged: "SoH advances the fire CMAB cursor in cs-frames (30fps) while the CMAB's
duration=300 is authored at 60fps ... a 2x playback-rate difference during the first 300 frames of
the flicker (**irrelevant at cs1093, which is long past the freeze**, but it will matter if anyone
A/Bs the fade-in flicker window cs345-495)." Task for this session: locate the exact 3DS runtime
CMAB-consumer/binder function via Ghidra, port the correct timing, and close the cs1093
letters-excluded gold-mask extent residual (0.783 of oracle). Result: **the timing bug is real, was
found and ported, and is now verified fixed by direct value trace — but, exactly as the note above
already predicted, it does NOT and CANNOT change the cs1093 measurement**, because cs1093 is deep
into the CMAB's post-freeze hold state under either the buggy OR the fixed formula. This is proven
algebraically below, not asserted. The runtime binder function's exact VA was **not** located this
session (two more static anchors tried, both dead-end, documented in
`oot3d-decomp/docs/title_logo_fireglow_cmab.md` §7) — the fix was derived and ported from
already-decompiled, independently-confirmed ground truth instead (see §2).

## 1. Static RE this session (oot3d-decomp) — two anchors tried, both dead-end

1. **Pool-constant hunt for the CMAB chunk-magic tags** (`SearchU32.py`,
   `OOT3D_SEARCH_U32=0x7364616D,0x64616D6D,0x62616D63` — LE u32 encodings of `"mads"`/`"mmad"`/
   `"cmab"`) over all initialized memory: **zero hits**. The parser does not runtime-validate
   these ASCII tags via an inline 32-bit literal compare anywhere in `code.bin`.
2. **Enumerate every caller of `FUN_00358964`** (the confirmed constant-color-register-write
   helper, `title_logo_actor.md` §6.2) via `ListCallers.py` + `DecompDump.py`: 19 distinct caller
   functions, all decompiled. **Every single call site passes a literal immediate channel index**
   (3, 4, or 5 — never a variable) — i.e. every one of these 19 is a conventional per-actor draw
   routine, not a generic data-driven CMAB-track-apply loop. This rules out "the CMAB
   ConstColor-apply path reuses this exact helper."

Full writeup + the 19 decompiled addresses: `oot3d-decomp/docs/title_logo_fireglow_cmab.md` §7.1.
Conclusion: the runtime binder remains unlocated after 3 independent static anchors across two
sessions (this session's 2 + the prior session's `"cmab"`-string xref). The concrete next step
(not attempted this session) is a harness memory-write watchpoint on `g_title.cmb`'s live
`matCtx` constant-color register **index 0** (not index 5, the already-identified actor-alpha
register) — this needs new tooling to resolve a model handle to its live `matCtx` heap VA first
(`soh3d`'s `tools/soh3d_harness/watchhook.cpp` has generic watchpoint infra but nothing yet does
that handle→heap-address resolution).

## 2. What WAS nailed down (sufficient to port the fix without the binder address)

The relevant fact isn't the binder's address, it's the **tick-rate ratio between the domain SoH
anchors the cmab cursor on and the cmab's own native authoring domain** — and that's already
pinned down, independently, twice, by prior decomp work unrelated to `g_title.cmb`:

- **Static**: `title_logo_actor.md` §5.5 — `csCtx.curFrame` runs at **half** the raw
  emulated-frame-tick rate (contrasted against a separate ~1/frame counter).
- **Live**: `soh3d:title_logo.cpp:282`'s own comment records a direct trace
  (`ZELDA3D_DBG_TITLESKIP`) showing "the same csFrame value logged twice per tick" — SoH3D's own
  `Zelda3D_TitleCsFrame()` (`zelda3d_cutscene.cpp`'s `sFrame`) is deliberately built to reproduce
  this: `Zelda3D_TitleCsAdvance()` runs once per real engine tick (called from
  `TitlePresentation::update()`) but only increments `sFrame` every OTHER call (`sTickParity`).

`g_title_fire.cmab`'s `duration=300`/keyframe-time fields are H3D material-animation data in the
SAME per-engine-tick domain every other CTR animation format in this game uses (no evidence
anywhere in the byte-level decode for this one cmab to use a different convention). Feeding it
`(csFrame - fadeInFrame)` directly, as the pre-existing port did, therefore sampled it at HALF its
intended rate. **This is a unit-domain conversion at the one remaining place still left in the
half-rate domain, not a fitted constant** — the exact 2x ratio is the same one
`Zelda3D_TitleCsAdvance` already enforces for every other cs-frame consumer.

Full derivation with anchors: `oot3d-decomp/docs/title_logo_fireglow_cmab.md` §7.2/§7.3.

## 3. The port

`Shipwright/soh/src/zelda3d/behaviors/title/title_fireglow.cpp`,
`Zelda3D_TryDrawTitleFireGlow`:
```
- float cmabFrame = (fadeInFrame >= 0) ? (float)(csFrame - fadeInFrame) : 0.0f;
+ float cmabFrame = (fadeInFrame >= 0) ? 2.0f * (float)(csFrame - fadeInFrame) : 0.0f;
```
(full derivation now in that file's own header comment). No other lines changed. Rebuilt both the
embedded-harness target (`ninja -C Azahar/build-libretro soh3d_harness`) and the standalone game
(`cmake --build Shipwright/build-cmake --target soh`) — both incremental, both green, only this
one translation unit recompiled.

## 4. WHY this cannot move cs1093 (proven, not hand-waved) — and direct value-trace proof it DOES fix the flicker window

`fadeInFrame` = 345 (the flag-3 fade-in trigger, cs-frame domain). `Zelda3D_CmabSampleTranslationV`/
`ConstColorRGB` clamp `cmabFrame` to `[0, duration]` = `[0, 300]` for `loopMode=Once`
(`zelda3d_cmab.cpp:clampFrame`). At cs1093: elapsed cs-frames = 1093-345 = 748.
- Pre-fix: `cmabFrame = 748` → clamped to 300 (frozen value).
- Post-fix: `cmabFrame = 2*748 = 1496` → **also** clamped to 300 (same frozen value).

**Identical sampled output either way at cs1093** — the fix is a no-op there by construction. This
matches, exactly, the prior entry's own prediction ("irrelevant at cs1093, long past the freeze")
that this task's brief apparently didn't carry forward. Confirmed directly, not just algebraically:
built pre-fix and post-fix binaries (git-stash swap + incremental rebuild) and drove the harness to
csFrame=1093 with `ZELDA3D_DBG_FIREGLOW=1` both times — irrelevant to re-quote since both variants
land in the clamp, verified instead at a frame where the two formulas actually diverge:

**csFrame=550** (elapsed=205 cs-frames, well inside the fade-in flicker window cf466-525-and-after,
before the fix's clamp point in the OLD formula but past it in the NEW one):
```
PRE-fix:  cmabFrame=205.0  rgb=(0.8885,0.4742,0.0000)  uvV=0.6833   <- still mid-flicker/mid-scroll
POST-fix: cmabFrame=410.0  rgb=(0.8000,0.4300,0.0000)  uvV=1.0000   <- correctly frozen at frame-300
```
This is a real, measurable, in-game behavioral change: pre-fix, SoH's flame flicker/UV-scroll was
still animating at csFrame=550 (running at half the intended rate); post-fix it has already
completed its one-shot 300-(native)-tick flicker and settled, matching what the oracle's own
60fps-native playback would have done by the equivalent real time. The bug was real and is fixed;
it was just never visible at cs1093 specifically.

## 5. Close-test / sweep results

`tools/title_sbs_verify.py --k 8` (full title-cs-loop sweep, current build, name `postfix2`):
```
idx  target_cs  actual_cs    score  delta   corr  state    flag
  0        150        150   0.9480      0      9  LOCKED
  1        464        464   0.8362      0     29  LOCKED
  2        779        779   0.8865      0     57  LOCKED
  3       1093       1093   0.7406      0     77  LOCKED
  4       1407       1407   0.8855      0     93  LOCKED
  5       1721       1721   0.9162      0    119  LOCKED
  6       2036       2036   0.9863      0    141  LOCKED
  7       2350       2350   0.9959      0    155  LOCKED
min_score threshold: 0.7 -> PASS
```
No LOW flags, no regressions. Compare against the most recent available *full 8-point*
`title_sbs_verify` table on record (`scratch/logs/title_sbs_verify_run2.stdout`, a pre-D1-fix
build, kept only as the closest historical full-sweep reference — every other post-D1 session
only ran the 2-point cs464/cs1093 subset):
```
  1        464        464   0.6912  (pre-D1)  ->  0.8362 (this build)
  3       1093       1093   0.6393  (pre-D1)  ->  0.7406 (this build)
```
Both up substantially, consistent with the D1 sphere-map fix (`a20566da`) and the intervening
grass/rider/spot99 work, not with this session's timing fix specifically (which cannot move either
of these two frames' fire-glow content per §4 — cs464 is *before* the glow's alpha even starts
ramping (cf466), and cs1093 is past the clamp for both formulas).

`tools/test_fireglow_extent.py` (new, this session — box-scoped x110-300/y40-190 gold-hue-minus-
strict-red pixel count, formalizing the prior session's one-off manual metric) on the current
build's own `postfix2` captures:
```
cs464:  az extent_px=9     soh extent_px=10    ratio=1.111  (both near-zero: pre-alpha-ramp, expected)
cs1093: az extent_px=4850  soh extent_px=4992  ratio=1.029  PASS (>=0.90 threshold)
```
**The cs1093 extent ratio is now ~1.0 in the current build** — a large change from the
`2026-07-14-...-fireglow-remeasure.md` entry's own measurement of 0.783 on an earlier
(`sphfixy2`-tagged) capture set. Per §4's proof, this session's timing fix cannot be the cause.
Most likely explanation: the extent gap had already substantially closed via the intervening
sessions' work (rider cutscene dispatcher `0a711e4c`, field-grass CMB routing `9404c9f5`, spot99
promotion `f4bd51e8`, boot-logo skip `d938e659`) between that entry and now — none of which
targeted the fire-glow specifically, but any of which could shift camera framing / lighting /
composite content enough to move a whole-box pixel-count metric. **Not re-attributed with
certainty this session** (would need re-running the OLD `sphfixy2`-era build's exact capture
pipeline to isolate which specific intervening commit closed it, which wasn't done — flagged as an
open provenance question, not silently claimed as "fixed by this session's work").

Chosen threshold for `test_fireglow_extent.py`: **0.90**. Justification: the paired luminance
channel (a fully independent measurement of the same mask) was already confirmed at 99.4% parity
(161.8/162.9) in the prior remeasure entry, and once phase/extent content is correctly aligned the
remaining gap should be small antialiasing/threshold-crossing noise, not a structural error — 0.90
leaves headroom for that noise without being loose enough to pass a real half-extent bug (which
was 0.783, comfortably below 0.90).

## 6. Honest summary — what this session did and did not close

- **Fixed and verified** (value-trace, §4): the fire-glow CMAB's frame-domain mismatch (cs-frame
  used directly as a native-cmab-tick counter) — real bug, correctly derived from two
  independently-established facts (not fitted to any pixel measurement), now fixed at the seam
  where the domain conversion belongs.
- **NOT closed by this fix, and provably cannot be** (§4): the cs1093 gold-mask-extent residual
  this task was framed around. The task brief's own cited journal entry already said as much
  ("irrelevant at cs1093") — this session's job was to verify that boundary precisely (done) and
  find the exact runtime binder (not done, static route dead-ended twice more).
- **Possibly already resolved anyway, but not attributed**: current-build extent ratio at cs1093
  measures ~1.0, up from a historical 0.783 — real, but from unrelated intervening work, per §5's
  caveat. A future session should re-verify with a clean, single-variable A/B (this exact
  `postfix2` build vs one commit at a time reverted) before writing "cs1093 CLOSED" anywhere.
- **Runtime CMAB binder function**: still unlocated. Concrete next step is the harness
  watchpoint route (§1), which needs new tooling (a model-handle→matCtx-heap-VA resolver) neither
  session had time to build.

## 7. Anchors / files touched

- `oot3d-decomp/docs/title_logo_fireglow_cmab.md` §7 — this session's static RE (2 dead-end
  anchors + the frame-domain derivation, with VAs for all 19 decompiled functions).
- `Shipwright/soh/src/zelda3d/behaviors/title/title_fireglow.cpp` — the one-line fix + expanded
  header derivation.
- `tools/test_fireglow_extent.py` — new, reusable close-test for the extent metric (was a
  one-off script in the prior session).
- `scratch/decomp_agent/fireglow_frame_probe.py` — new, reusable single-cs-frame FIREGLOW-trace
  probe (drives to an exact csFrame, dumps the `[FIREGLOW]` debug line) — used for the §4 before/
  after value trace; not previously available (prior sessions only had the full-sweep pixel tools).
