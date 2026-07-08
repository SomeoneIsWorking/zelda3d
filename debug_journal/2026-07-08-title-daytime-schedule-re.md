# 2026-07-08 — Title dayTime schedule RE: the rate is correct; the bug is CURSOR PHASE, not the formula

Task framing was: "SoH's title dayTime advances at a DIFFERENT RATE than the oracle;
fix `Zelda3D_TitleCsTimeOfDay` to interpolate between the REAL adjacent ROM keyframes
instead of a linear extrapolation from a sparse anchor." After RE'ing the actual oracle
schedule from the ROM cs stream + the OoT3D decomp + the existing live-verified samples,
that framing's premise does **not** hold. Writing this down so the next session (or the
coordinator building the fix) does not chase a rate/keyframe change that ground truth
rules out.

## Ground truth 1 — the title cs has ONE time value, not a sparse keyframe set

`spot99_info.zsi` " BDQ" stream (BDQ @ 0x3528, `cmd_count=13`, `end_frame=2400`),
walked with `tools/walk_oot3d_cs.py`:

- op-0x8c (SET-TIME) appears **once**, with `cnt=2` records, BOTH `hour=4 min=0`
  (→ dayTime 0x2AD7 = 4:01 AM): at cs-frame 0 and cs-frame 301. That is the entire
  time-cue content. There is **no** second distinct time value to interpolate toward.
- The full cs opcode set is 0x0a (actor cues), 0x0d, 0x03 (misc triggers), 0x3e8
  (loop marker), 0x8c (set-time), 0x7c (fade), 0x97 (camera spline) — confirmed both
  by the walk and by `oot3d-decomp/docs/title_gamestate_driver.md §2`. **There is no
  per-frame time-advance / time-increment opcode in the title cs.**

So "interpolate between the real adjacent ROM keyframes" is a non-starter: adjacent
keyframes are (0, 0x2AD7) and (301, 0x2AD7) — identical. A slope fit between them is 0.

## Ground truth 2 — the oracle DOES flow dayTime, at ~6 units/cs-frame, and SoH already matches it EXACTLY when frames are aligned

- spot99's scene header has **no** ZSI cmd-0x10 (timeSettings) — walked the header
  cmd list: {0x18,0x15,0x04,0x19,0x03,0x06,0x07,0x0d,0x00,0x11,0x13,0x0f,0x14}. So the
  N64/3DS `Scene_CommandTimeSettings` path never sets a scene `timeIncrement` for the
  title. Yet the oracle's live dayTime is **not** frozen at 0x2AD7 — it climbs.
- `2026-07-07-title-lighting-solved.md` verified (harness, **frames pinned equal** via
  ab_lighting2.py) SoH dayTime == Az dayTime **EXACTLY** at four dawn samples:
  0x2d83 / 0x2f45 / 0x310d / 0x32cf (monotone-increasing, ≈+0x1C2 per ~75-frame gap
  → ≈ +6 dayTime/cs-frame). That is the current SoH formula
  (`dayTime = 0x2AD7 + 6·(frame-cue)`) reproducing the oracle to ±0 across the tested
  range.

Conclusion: **the +6/cs-frame RATE is RE-correct and already verified against the
oracle.** It is a constant per-frame increment (the title gamestate's own clock advance,
NOT a cs keyframe schedule and NOT the scene timeIncrement) — there is nothing sparse to
interpolate, and no evidence the rate itself is wrong.

## So why the measured divergence at content-matched frames?

Because the two engines' title-cs CURSORS are out of phase at matched CONTENT — the
already-documented `tools/title_ab.py` result:

| content | Az cs-frame | SoH TitleCsFrame | Δ (soh-az) |
|---|---|---|---|
| early night, moon rising | 200 | 397 | +197 |
| rider crossing field     | 360 | 449 | +89  |
| grass close-up push      | 550 | 593 | +43  |

SoH's `Zelda3D_TitleCsTimeOfDay` is indexed by `Zelda3D_TitleCsFrame()` — SoH's OWN
ported cursor (`Zelda3D_TitleCsAdvance`, +1 per `Zelda3D_ApplyTitleCam` call). At the
SAME on-screen content, that cursor reads a DIFFERENT value than the oracle's cs cursor
(here consistently AHEAD, by a gap that shrinks from +197 to +43 as the demo runs — i.e.
the two cursors run at different phase/rate, converging over the loop). Feed a different
frame into an otherwise-correct `dayTime(frame)` and you get a different dayTime at the
same instant — SoH lands further along the dawn ramp than the oracle → SoH's sky/lighting
is time-shifted vs the oracle. This is the phase offset the terrain-ambient ~2x back-solved
to; it is a CURSOR-SYNC defect, not a dayTime-formula defect.

## Why NOT to "fix" `Zelda3D_TitleCsTimeOfDay`

Changing the rate/keyframe math in `Zelda3D_TitleCsTimeOfDay` to make matched-frame
dayTimes line up would be fitting a constant to mask a cursor-phase bug — a bandaid. The
formula is already the oracle's (verified ±0 at aligned frames). The real fix is to align
SoH's title-cs cursor advance with the oracle's 60 fps / 2400-frame cs cursor so that at
equal CONTENT the two cursors read equal frames (then dayTime matches automatically). The
cursor phase/rate law (why SoH leads by +197 early, converging to +43) still needs to be
pinned: candidates are SoH's N64 splash/boot consuming a different number of pre-cs frames
and/or SoH ticking the ported cursor at a different fps than the 3DS 60 fps cs.

## The measurement that decides it (tooling built this session, NOT yet run)

Added `az_daytime` to the harness (reads Az `gSaveContext.dayTime` @ fixed .bss VA
0x00587958+0x0C — a global, valid during the title/opening GameState where gPlayState==0)
plus two drivers:

- `tools/title_daytime_scan.py` — Az-only: maps Az cs-frame → Az dayTime across the whole
  2400-frame loop (settles whether the rate stays ≈6 late in the demo or changes).
- `tools/title_daytime_verify.py` — at each content-matched pair (az200/soh397,
  az360/soh449, az550/soh593): reads Az dayTime (`az_daytime`), SoH dayTime + envCtx
  (`soh_env`), and samples sky RGB from both snapshots. This is the direct before/after
  proof.

These could not be RUN this session: the harness rebuild (needed to embed the new
`az_daytime` command) was killed for RAM budget before it finished. NEXT step is a single
`-j4` harness build, then `title_daytime_verify.py 200:397 360:449 550:593` — that prints,
per pair, whether SoH's dayTime already tracks the oracle's (expected: it does NOT in the
free-running case, by exactly the cursor-phase gap above) and by how much, which pins the
cursor-alignment correction quantitatively.

## Bottom line for whoever lands this

Do **not** ship a `Zelda3D_TitleCsTimeOfDay` rate/keyframe edit — ground truth says the
formula is correct. The divergence is title-cs cursor phase (SoH's `Zelda3D_TitleCsFrame`
vs the oracle's cs cursor); fix that seam, or drive SoH's title dayTime off a cursor that
is content-equal to the oracle's. Verify with the two scripts above after one `-j4` build.
