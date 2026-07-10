# Dawn layers attributed: sun glow dome + kumo vcol-lerp + soft_smoke mist (2026-07-11)

Follow-up to `2026-07-10-dawn-hue-fog-rootcause.md`'s "secondary observed-but-unattributed
dawn layers." All three are now attributed with asset + formula + measured windows.
**Full port-ready spec: `<oot3d-decomp>/docs/title_dawn_layers.md`** (commit `5e053d6`).
This journal holds the soh3d-side pointers only.

| §12 layer | attribution | confidence |
|---|---|---|
| additive horizon glow (182,34,0) | **sun glow dome** `BlueSky.zar model/fine_sun.cmb`, handler `FUN_0045d018`; effective color = baked vcol × matDiffuse(255,127,127); additive srcAlpha/ONE; steady placement T(0,−660,1600)·rotX(5°)·rotY(0.7°/frame) off an orientation anchor (writer unpinned) | asset+blend+color VERIFIED per-pixel; placement decomp-derived, anchor matrix flagged |
| mauve haze band (tex 0x1834c100) | **the kumo_a cloud band itself** — texture `fine_a01` byte-exact (32768/32768); ONE draw with CPU-lerped variant vertex colors: implied w=0.297 vs dome schedule w=0.298 at dayTime 0x319d | COMPLETE (asset, formula, blend, schedule, window) |
| warm alpha layer (tex 0x20ace580) | **ground-mist billboards** — texture `zelda_keep_opening.zar soft/tex/soft_smoke.ctxb` byte-exact (32768/32768); TEV rgb=vcol·texA+const, const dayTime-lerped (31,22,10)→(49,30,7); 2 world quads ~170u at (3952,−3,6734) | texture+formula+blend+placement measured; OWNER FN not located |

## SoH-side implications (for the port session)

- SoH draws the kumo band as TWO passes (`zelda3d.c` cloudId+cloud2); the oracle does
  ONE pass with per-frame CPU-lerped vertex colors (69 verts). At the §12 probe pixels
  SoH's stack has no kumo fragment at all — port the lerp mechanism and re-probe.
- SoH never draws `fine_sun.cmb` (known residual "vertex glow-cap unused") — this IS
  §12's additive glow; port per the spec (multiply baked vcol by the CMB diffuse —
  SoH's CMB path may currently ignore material diffuse when vertex colors exist).
- The mist layer is a smaller term; port spec is implementable with a flagged
  STOPGAP color curve until the owner decomp.

## Evidence / tooling (scratch/dawn_layers/, gitignored)

- `sweep_layers.py` — 49-point full-loop presence sweep (draw_log signature classify)
  + `dumpphys` texture/vertex dumps at the canonical dawn frame; `layer_windows.csv`.
- `compare_dumps.py` — byte-attribution of the two textures (100.00% matches).
- `dump_warm_verts2.py` — smoke quad world positions.
- Note: the title cs restarts once ~az 405–450 after `title_settled.state`; the glow
  object only exists after that restart (savestate artifact — natural loops have it
  from frame 0). dayTime then runs continuously 0x2b2b→0x42xx over the loop.

## Dead ends (do not re-walk)

- `tex/fine_lensflare.ctxb`/`fine_sun.ctxb` are 128×128 ETC1 — wrong format for the
  warm layer (draw fmt=5 IA8); the lens flare is FUN_0045d018's separate noon-glare
  subsystem.
- The smoke vertex pool at FCRAM 0x20ac8140 parses as garbage at loader-0 offset;
  the real position stream is at loader-2 (0x20ac9940). Effect pool is 512 verts,
  only the live puff's 8 populated.
- The romfs-wide 128×128 IA8 scan is exhaustive: kumo `*_a01/b01` families,
  `soft_smoke`, `acto_thunder`, `vb_smoke`, `frezad_00`, 5 unnamed in
  `zelda_keep.zar` — the dumps matched uniquely.
