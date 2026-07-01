# zelda3d history (archived planning docs)

The `zelda3d` repo was an **abandoned** umbrella-project experiment (a separate PC app
meant to host OoT + MM under one renderer, initially via static recompilation for MM).
That approach was dropped: **MM-native now lives in this soh3d repo** (see
`docs/MM_NATIVE.md`), built on the shared libultraship + soh3d SDL3-GPU renderer + the
`soh/src/soh3d/asset/` CMB stack — no recomp.

These four docs are preserved here **for historical context only**. They are largely
self-superseded and NOT current guidance:

- `VISION.md` — the dual-game "definitive 3D edition of OoT + MM" product vision, captured
  from the user. The north-star framing; soh3d is now that home.
- `ARCHITECTURE.md` — explicitly marked *corrected/superseded* in its own header.
- `MIGRATION.md` — migration manifest for the abandoned clean-rebuild; its own header marks
  the Layer-2/3 framing superseded.
- `HIRES_TEXTURES.md` — the CMB hi-res texture path it describes is already **live** in soh3d
  (`asset/texpack.cpp`, `SOH3D_TEXPACK`); kept for the plan/status snapshot.

Everything else from `zelda3d` was either already migrated (the MM docs → `docs/`), an
actively-newer duplicate here (`src/cmb3d/` → `soh/src/soh3d/asset/`), or the deliberately
dead recomp path (`src/mm_host`, `src/n64dl`, `src/render`).
