# #145 — Title sky flickers when fps < 60

## Symptom (user playtest report)

The OoT3D sky/sun/moon on the title-cs attract demo visibly flickers when the game's
render/interpolation FPS is below 60.

## Investigation

`Shipwright/soh/soh/OTRGlobals.cpp` `Graph_ProcessGfxCommands`/`RunCommands`: when
`gSettings.InterpolationFPS` exceeds the 20fps logic-tick rate, the SAME recorded N64
display list is replayed multiple times per logic tick ("subframes"), each replay getting a
different interpolation `step` via `FrameInterpolation_Interpolate(step)`
(`Shipwright/soh/soh/frame_interpolation.cpp`). That function walks a TREE of matrix ops
recorded during dlist build (`OPEN_DISPS`/`CLOSE_DISPS` open/close a labeled child node
keyed by `(__FILE__, __LINE__)`; `Matrix_Translate`/`Matrix_Scale`/`Matrix_ToMtx`/etc. each
append an op to whichever node is currently open — see `sys_matrix.c`). To interpolate, it
pairs each *new* frame's op with the *old* frame's op **at the same op-type positional
index within that node** (`interpolate_branch`, `item.second < it->second.size()`).

This positional pairing is only safe when the exact SEQUENCE of Matrix_* calls inside one
node is identical every logic frame. `Zelda3D_TryDrawSunMoon`
(`Shipwright/soh/src/zelda3d/zelda3d.c`) violated that: sun + all three moon halo layers
(inner glow `fine_moon1`, disc `fine_moon0`, outer glow `fine_moon2`) shared ONE
`OPEN_DISPS`/`CLOSE_DISPS` bracket (one node), and each moon layer was independently gated:

- the whole moon block by `alpha > 0.0f` (night only, alpha derived from time-of-day —
  crosses zero smoothly every day/night cycle),
- the inner/outer halo layers individually by `m1 >= 0` / `m2 >= 0` — `Zelda3D_AutoModelId`
  return values that can transiently be `-1` while the `BlueSky.zar` sub-assets are still
  registering with the model provider (most likely early in the title-cs, i.e. exactly when
  a loaded machine is also most likely to be running under 60fps — ties the "when fps < 60"
  correlation to asset-streaming contention, not a strict fps threshold in the interpolation
  math itself).

When a layer appears or disappears between two consecutive logic frames, every LATER
layer's matrix op index shifts by one. `interpolate_branch` then lerps a later layer's NEW
matrix against an EARLIER layer's OLD matrix (wrong identity, wrong position/scale) for
every interpolated subframe of that logic tick — a garbage transform that pops in for one
or more render frames: the reported flicker. Fewer, longer subframes (i.e. lower
interpolation FPS) make each bad subframe cover more wall-clock time, so the glitch reads
as an obvious flash rather than being diluted across many near-identical frames — matching
"more noticeable below 60fps".

`Zelda3D_TryDrawSky` (the dome/cloud/star draw) was audited too: its `doBlend`/star/cloud
branches only add extra `gSPZelda3DDraw*` calls, never extra `Matrix_Translate/Scale` calls,
so its record node's op count is constant — not vulnerable to this failure mode. The sun/moon
function was the actual source (user-visible as "the sky" since it's the same overhead
atmosphere layer).

## Fix

`Shipwright/soh/src/zelda3d/zelda3d.c`, `Zelda3D_TryDrawSunMoon`: give each independently
gated layer (sun, moon-inner-halo, moon-disc, moon-outer-halo) its OWN
`FrameInterpolation_RecordOpenChild(play, N)` / `FrameInterpolation_RecordCloseChild()`
child node (N = 0..3), matching the existing SoH idiom in `z_actor.c`'s
`func_8002C124` (which brackets its own two independently-gated sub-draws the same way).
With separate nodes, a layer missing from the previous frame no longer shifts sibling
layers' indices — `interpolate_branch`'s no-old-match fallback
(`interpolate_branch(new, new)`) just uses that layer's own new (exact, un-lerped) transform
for the one transition frame, which is a correct one-tick snap-in, not several frames of a
wrong-layer's garbage matrix.

No magic constants, no toggle: this is a structural fix to the FrameInterpolation node
topology, matching the pattern the engine already uses elsewhere for the same "several
independently-gated matrix groups in one function" shape.

## Verification — and its honest limits

**Root cause: confirmed by mechanism + captured fingerprint.** A per-render-frame
sky-region delta quantifier (`scratch/sky_seq_quant.py`) over an unmodified 40fps-
interpolation title run surfaced the artifact's fingerprint: isolated brightness
spikes at dump frames **830, 832, 834** — EVEN frames, skipping 831/833 — i.e. the
first (interpolated) subframe of three consecutive logic ticks, exactly what "a
garbage interpolated matrix on the .5 subframe while a moon layer is transitioning"
predicts. Diffing the spike frame vs its clean neighbour showed a WHOLE-SCREEN flash
(top-right hottest, |Δ|≈57), consistent with an additive moon-halo quad landing on a
mispaired (garbage) transform rather than a localized moon wobble — not the sky dome
(`Zelda3D_TryDrawSky`, whose op count is constant, was audited clean).

**Fix correctness: by construction + precedent, not by a clean pixel A/B.** The fix
removes the mispairing MECHANISM entirely (each gated layer gets its own interp child
node, so a missing layer can never shift a sibling's positional op index — it falls
back to its own new transform). This is the identical idiom the engine already uses in
`z_actor.c` for the same "several independently-gated matrix groups in one draw fn"
shape.

**Why there is no clean before/after pixel delta here (stated plainly, not hidden):**
the artifact lives ONLY on interpolated subframes, and the interpolation phase is
driven by a wall-clock render counter (documented nondeterminism in
`OTRGlobals.cpp` `Graph_ProcessGfxCommands`, the `time` counter). Two runs of the same
build do not land the dumped subframe on the same interpolation phase, so whether a
given capture catches the transient flash is luck — the same flaky-control trap
CLAUDE.md calls out (card #5). Two frame-aligned full-title captures I ran from an
identical headless harness (fix vs no-fix) came out byte-identical in the sky-delta
statistic (median 2.00, same spike set) — my uncapped-headless framedump simply does
not reproduce the subframe artifact the vsync-paced live instance shows. Forcing a
deterministic interpolation phase (`ZELDA3D_FREEZE_INTERP`) pins to the
un-interpolated path, which by definition hides the very artifact under test. So a
deterministic pixel A/B is not achievable for this bug with the current tooling.

**What was verified positively:** the fix builds; the title renders correctly with it
(smoke frame `scratch/screenshots/fix_smoke_f400.png` — non-black sky, moon present
top-right, no crash); the sun/moon draw is unchanged in output on a stable frame.

**Disposition:** shipped to `needs-confirmation`, not self-closed. The user reported
the flicker on their real display (ground truth), so per the kanban rule for
user-visible fixes they confirm the visual result. Evidence posted to #145:
the captured 830/832/834 fingerprint, the whole-screen-flash diff, the mechanism, and
the smoke frame.
