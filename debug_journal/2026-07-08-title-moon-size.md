# #146 — Title moon "too big": quantified diagnosis (fix NOT a scale constant)

User playtest report: the title-screen moon is too big vs OoT3D.

## Ground truth (Az / OoT3D, from oot3d-decomp docs/title_moon_composition.md)

3-layer composite on the 400×240 top screen at settled title:
- disc `fine_moon0` (128×128, alpha crescent): screen bbox (377,0)-(480,91) → **103×91**, ~25.8% of width, edge-clipped top-right.
- inner glow `fine_moon1` (64×64, additive): 188×133.
- outer glow `fine_moon2` (64×64, additive): 200×139.

## Measured (harness Az-vs-SoH SxS, same cs frame 100, 400×240)

`scratch/measure_moon.py` + a lum-threshold sweep on
`scratch/screenshots/moon_f100.{az,soh}.ppm`:

| | Az | SoH |
|---|---|---|
| bright footprint (lum≥100) | 180×95, **752 px** | 117×138, **13625 px** |
| bright core (lum≥150) | 81×18 @ x220-300 | 94×128 @ x306-399 |
| peak (lum≥200) | present (81×18) | **none** |

Three distinct divergences, not one:

1. **Size.** SoH footprint 138 px tall vs Az 95 → SoH is **≥1.45× too tall**,
   and SoH's is CLIPPED (top y=0, right x=399), so the true oversize is larger.
2. **Fill / opacity.** SoH has **18× more bright pixels** (13625 vs 752). Az's
   moon is a thin crescent + faint halo (halo textures have transparent centres,
   drawn dim); SoH renders a big solid bright blob. SoH's additive halo layers
   (1.65× and 1.85× the disc, drawn at full moon alpha `aA`) are far too
   bright/large — this is most of the "too big" the user sees.
3. **Position.** Az's bright core is at x220-300 (mid-right); SoH's is at
   x306-399 (jammed in the clipped far corner), ~90 px further right and higher.

## Root cause (why it is NOT a one-line scale tweak)

SoH draws the moon via the **dynamic environment sun/moon path**
(`Zelda3D_TryDrawSunMoon`, zelda3d.c): moon world pos = `eye − sunPos`,
`|sunPos| = 120*25 = 3000`, and `scale = (-15*color)+25`, halos ×1.65/×1.85 at
full alpha. Az's title moon is a **fixed-framing composite** baked for the title
shot (see the standing comment in `Zelda3D_TryDrawSunMoon` and
title_moon_composition.md). Because the two use different placement AND different
halo compositing, SoH's moon differs from Az's in position, size, and fill
simultaneously. Matching Az means matching its FRAMING (screen position + disc
angular size + halo alpha/scale), not just multiplying one `scale`.

A scale-only reduction would make the disc smaller at frame 100 while leaving the
wrong position and the over-bright halos — i.e. tuning one number to "look right"
at one frame, which is the bandaid the project rules forbid.

## The correct fix (derivation, for the next pass)

Two sub-parts, both quantitative:

1. **Disc angular size.** Az disc = 91 px on 240 at vertical FOV 35° (shot-0 fov)
   → subtends (91/240)*35° = **13.3° vertically**. The SoH disc's on-screen height
   is linear in the draw `scale` at fixed distance, so once an UNCLIPPED SoH disc
   height `H_soh` is measured (needs a cs frame where SoH's moon is fully in
   frame — capture shot-0 across cs 0..300 and pick the centred frame), the disc
   scale correction is `scale *= (13.3° target px) / H_soh`. Do NOT eyeball it —
   compute from the measured px.
2. **Halo alpha + scale.** Az halos are additive with transparent centres and
   read far dimmer than SoH's. Match Az's screen ratios (inner 1.72× wider /1.46×
   taller than disc; outer 1.94×/1.53×) AND reduce the halo alpha so the bright
   footprint matches Az's ~752 px, not 13625. The current full-`aA` additive
   halos are the dominant over-brightness.
3. **Position.** Confirm whether the dynamic `eye − sunPos` placement is close
   enough to Az's baked framing at the shots where the moon is visible, or whether
   the moon must be pinned to Az's title framing (the open RE item the
   `Zelda3D_TryDrawSunMoon` comment already flags). Measure Az's moon screen
   centre across shots 0-2 and compare to SoH's before deciding.

## Status

Diagnosis complete and quantified; NO fix applied this session (a verified fix
needs build+capture iteration + the position decision above, and a scale-only
change would be a bandaid). Card kept in-progress with this analysis + the SxS
measurement posted as evidence. Tooling: `scratch/measure_moon.py`,
`scratch/moon_sxs.py` (harness Az/SoH SxS at an aligned cs frame).
