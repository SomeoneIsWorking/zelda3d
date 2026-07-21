# 2026-07-21 — MM3D anim table generated: kMMAnimMaps 4 → 1386 (192 objects, 107 actors full)

## Why this was the right target

MM3D skinned-actor rendering was finished and verified months earlier but shipped **gated off**
(`ZELDA3D_MM_SKINNED=1`) for exactly one reason: the N64-anim → 3DS-CSAB table had **4 entries, all
object_dog**, against ~1845 animations. Every unmapped animation falls back to the default idle CSAB,
so MM's entire cast stood still. That is an entire game's actors — orders of magnitude more impact
than the per-object OoT prop work that preceded it.

## The unlock: the decomp already recorded the answer

Lexical matching stalled at **11.5%** (142/1234). The wall is a vocabulary gap, not an algorithm gap:
MM3D names its CSAB clips in Japanese romaji (`an_hokiwalk`, `dnt_iyaiyaTOmuun`, `tategiri`) while the
2ship symbols are English decomp names, so 566 symbols (46%) shared no token with any clip in their own
GAR and whole actors scored zero (object_rd 0/19, object_osn 0/24, object_dnq 0/23).

The 2ship asset XMLs already carry the mapping:

```xml
<Animation Name="gDogBarkAnim" Offset="0x998" />  <!-- Original name is "dog_bark" -->
<Animation Name="gDogLyingDownAnim" ... />        <!-- Original name is "dog_fuse" -->
```

**1555 of 1746** `<Animation>` entries carry an `Original name is "..."` annotation — the decomp
authors' record of the asset's original name, which is *verbatim* what the MM3D GAR calls the clip.
Taking it at confidence 1.0 whenever that clip exists in the actor's GAR is **authoritative, not
heuristic**, and it is the only signal that crosses the naming gap. This also made the planned
duration-ranking fallback (port of the OoT-side `zelda3d_anim_export.py` approach) unnecessary.

- 142 → **878** matched (11.5% → 71.2%); "no shared token" 566 → 51.

## Second find: the enumerator was blind to half the animations

The generator scanned object headers for `/g\w+Anim/`. That silently missed the **566 animations that
are still ADDRESS-named** (`object_daiku_Anim_00B690`) because they have no symbolic name yet — and
**536 of those are annotated**, i.e. perfectly mappable. Enumerating from the asset XMLs instead
(authoritative, both naming styles) took the table to **1386 across 192 objects**.

This was caught by the LIVE run, not by inspection: `object_daiku_Anim_00B690` appeared in the
`[MM3D-ANIM] unmapped` log.

## Verification

- All **1386** emitted clip names checked to exist in their actor's GAR — **0 missing**.
- The 4 hand-written pairs reproduce exactly; object_dog 4 → 9/10 (only miss is a non-skeletal
  texture anim), now including `gDogLyingDown → dog_fuse` / `gDogLyingDownLoop → dog_fusetamama`,
  which the lexical pass had wrongly reported as having no MM3D clip at all.
- Built MM clean; live headless Clock Town run with `ZELDA3D_MM_SKINNED=1`: unmapped animations fell
  from ~every actor to **ONE**, 11 MM3D models loaded and animating, **no crashes**.
- 107 actors FULLY mapped, 147 with ≥1 mapping.

## Honest remainder

- **Gate NOT removed.** Selection correctness is structural (authoritative names); per-animation
  playback correctness (timing/phase) is NOT spot-checked. ~40 actors are PARTIALLY mapped, which can
  read worse than uniform N64 — animating for some actions and idling for others. Removing the gate is
  a user-visible change across a whole game and needs visual verification first.
- **Known gap, deliberately not "fixed":** 11 animation symbols live in `assets/overlays/` (e.g.
  `ovl_En_Sth/gEnSthLookUpAnim` — the one remaining live unmapped). An overlay has no MM3D actor GAR
  of its own (its model comes from some object) and that association is not recorded in the XML, so
  scanning overlays would only emit unresolvable entries. Reverted that change rather than ship it.
- Of 459 unmatched, **342 are structural and correct**: 184 objects with no MM3D actor GAR, 96 GARs
  shipping no CSAB, 62 non-skeletal (Tex/UV/eye/mouth). Against actually-mappable animations that is
  **1386/1503 = 92%**.
- Next lever if more recall is wanted: N64 `AnimationHeader.frameCount` vs CSAB duration to resolve the
  46 weak / 7 ambiguous cases. Needs MM ROM object extraction (dmadata + Yaz0) — not currently wired.

Commits: generator `e4e742fc`, XML-annotation source `2924598b`, wired table `77c88a27`.
