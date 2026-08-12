# 0021 — the REPL `warp` inherited title state, so every headless warp landed on the wrong entrance

status: FIXED 2026-08-12
found: 2026-08-12, by the first run of the deep check's new warp tour
severity: two SIGSEGVs on ordinary destinations, and — worse — years of headless measurements taken
at a spawn point nobody asked for, with no symptom at all

## How it was found

`tools/zelda3d_deep_check.sh` held each core 60 s in-game and called that "past the first frame". Its
own verdict said what that omitted: *"dwell sits wherever the core spawns — it is time in-game, not
coverage of the game."* Replacing the dwell-only run with a **warp tour** (two Kokiri Forest spawns,
Zora's River; Termina Field and Great Bay Coast for MM) crashed on its first run, twice, in different
places. Both were this command.

## What was wrong

The headless boot reaches gameplay from the title attract demo, and the REPL's `warp` wrote
`nextEntranceIndex` + `transitionTrigger` and nothing else. Two pieces of save state carried over
that a real entrance transition never carries, because leaving the title normally goes through
file-select / `Sram_OpenSave`:

- **`cutsceneIndex` = 0xFFF3**, set by `z_opening.c:12`.
- **`gameMode` = GAMEMODE_TITLE_SCREEN**, set by the same function.

`z_play.c:515` selects the scene setup layer as `4 + (cutsceneIndex & 0xF)` if **either**
`gameMode != GAMEMODE_NORMAL` **or** `cutsceneIndex >= 0xFFF0`. So every plain REPL warp selected a
cutscene layer, and `Play_SpawnScene` then read `gEntranceTable[entranceIndex + sceneSetupIndex]` —
a *different entrance* from the one asked for.

Two crashes, one silent wrong answer:

1. `warp 0x109` → layer 7 → `Scene_CommandAlternateHeaderList` read header 6 of a scene with 6,
   SIGSEGV inside `Play_Init`.
2. `warp 0x209` (Kokiri Forest) → after fixing only `cutsceneIndex`, still layer 4 → entrance
   `0x20D`, spawn 2, in a scene whose header lists 2 entrances → `setupEntranceList[2]` out of range
   → a garbage start-position index → no Player spawned → SIGSEGV in `func_8002C0C0` on the empty
   PLAYER actor list.
3. **`warp 0xEE` never crashed and was never right.** Before the fix it put Link at
   `(4167,-171,-539)`; after, at `(-68,-79,941)`, the actual Kokiri Forest spawn. Every headless
   measurement framed as "same camera, same approach" after a `warp` was consistent, and consistently
   at the wrong entrance.

## The fix

`warp` now clears both, which is what SoH's own warp paths already did —
`Enhancements/Warping.cpp`'s `Warp()` and `debugconsole.cpp`'s `LoadSceneHandler` both set
`GAMEMODE_NORMAL`; the REPL warp was the one entry point that did not. It sets
`nextCutsceneIndex = 0` (the same door `cswarp` and `introcs` use to *request* a layer, with "none"
written on it) and `gameMode = GAMEMODE_NORMAL`. The reply now prints both previous values, so a warp
that inherits something is visible rather than inferred.

## Two bounds checks that stay

The crashes were symptoms, but the code they crashed in was independently wrong: the port turned
N64's "absent means a NULL entry in a ROM-authored array" into "absent means past the end of a
std::vector", and nothing checked. Both now report their denominators and fall back the way the N64
code falls back on NULL:

- `Scene_CommandAlternateHeaderList` (OoT `z_scene_otr.cpp`, and the identical MM copy in
  `z_scene_2SH.cpp`, which had never been driven into it) — an out-of-range header is treated as
  absent, the same as a null entry.
- `Scene_CommandSpawnList` — checks `curSpawn` against the entrance-list length (now kept beside the
  pointer, which `PlayState` carries without one) and the resolved start position against
  `numStartPositions`. This is where the "no Player" failure actually originates, three frames before
  the null-deref that reports it.

Each check prints scene, index and count, so the next occurrence names itself instead of arriving as
a backtrace in `func_8002C0C0`.

## What the tour found next: the magic meter read 64 bytes past its texture, every frame

With the warps landing correctly, the sanitizer run reached real gameplay with a magic bar on screen
and reported a **`heap-buffer-overflow` READ** in `Zelda3D_HudDecode`, from `Interface_DrawMagicBar`,
at the last byte of a 64-byte resource.

The numbers, once the decoder was made to state them:

    HUD texture __OTR__textures/parameter_static/gMagicMeterFillTex:
      call site says fmt I4 16x16 = 128 bytes, but the resource has 64 (8x8 per its own header).

`gMagicMeterFillTex` is 64 bytes (`parameter_static.xml`: 8x8 ia8 at 0x3AC0). The N64 display list
below asks the RSP for a 16x16 4-bit tile — 128 bytes — and gets 64 of texture plus 64 of whatever
follows it in the ROM. That is harmless *there*, because the texrect only ever samples the first 7
rows. It is not harmless in the native HUD path, which hashes and decodes **every byte it is told
exists**, so the fill overran by 64 bytes on every frame the bar drew — since the HUD conversion
shipped, silently, because the bytes after it are mapped and the sampled rows were never affected.

Fixed at the call site: `16x8`, which as I4 is exactly the 64 bytes the asset has and still covers
all 7 rows that are sampled. Not by clamping in the decoder — the decoder's job is to decode what it
is told, and a call site that lies about an asset should be corrected, not accommodated.

`Zelda3D_HudDecode` also gained the check that named this, because it could have named it years
earlier: it compares the byte count derived from (format, width, height) against the resource's real
size and, on a mismatch, logs **both sides plus the resource's own dimensions** and refuses. Once per
texture, not once per frame. A HUD element that vanishes with a log line is debuggable; an
out-of-bounds read is not.

Verified after: zero `HUD texture` errors in a Kokiri Forest run, and the fill still draws — 2,054
pixels within 40/255 of the magic green in a 158x13 box, found by searching the whole top-left
quadrant (201,984 px) rather than a guessed box, so a miss could not have been blamed on the search
area.
