# Audit round 2b — player / animation / scene (the three areas that died on API 500)

Re-ran via workflow resume. These areas produced NO results in the first attempt (all three agents
crashed), so nothing here was in the earlier write-up. **Verify phase was still running when this was
recorded — treat everything as UNVERIFIED leads, not results.** Numbers are the agent's own.

## Scene / collision — the one to look at first

**`cmb3d/asset/zcol.cpp:70` — "the polygon array is anchored at `polyList - 2` (stable across every
scene tested)"** (zcol.h:21, derived on SIX scenes). Claimed consequences, and this is GAMEPLAY
collision (default on; `zelda3d_collision.cpp` builds SoH's CollisionHeader from it):
* the LAST real polygon of EVERY scene is never loaded — **114 missing triangles game-wide: 82 walls,
  25 floors, 7 ceilings**;
* the parser fabricates a record 0 from vertex-array tail bytes, rejected by the index check in 107
  scenes but ACCEPTED in 7.
A six-scene derivation generalised to all 114 is exactly the failure shape round 1 was built around.

## Animation (`cmb3d/asset/csab.cpp`)

1. **`:115` — "only the header offsets differ between OoT3D subversion 3 and MM3D subversion 5; the
   anod/track layout is identical"**. Claimed: on the 2ship3d/MM3D branch **100% of 138820 tracks in
   2443 clips are silently discarded** — every skinned MM actor frozen in bind pose forever, with no
   error log and a correct-looking duration. If true this is the largest single defect found so far,
   and it is invisible on the OoT side.
2. **`:295` — the int16 short-way unwrap applied to FLOAT hermite segments too**. Any rotation turning
   more than 180 deg between keyframes becomes its short-way complement: 360 deg spins collapse to no
   motion, 190-360 deg turns reverse. 103 clips across 47 actors — boss intros/deaths, enemy attack
   and knockdown, Zelda's turn-around.
3. **`:344` — "for a non-root bone, ignore a static translation track and keep the rig's rest offset"**.
   Authored constant limb/hip placement discarded: Link's fall-wait and hookshot-fly lose a
   4500-7400-unit body offset, Wolfos runs 480 units high, carry side-walk loses a 635-unit shift.
   Same class as the already-fixed #204 ladder float.
4. **`tools/csab.py:75` — "a faithful twin of the C++ sampler"** (internal, but it poisons measurement):
   every offline consumer — `csab_render.py`, `csab_xcheck.py`, `link_sweep.py`, `model_match.py`, the
   pose-parity A/B — sees a different pose than the game draws, and the divergence is largest on
   Link, the rig the parity work targets. An offline "match" can certify a wrong pose. **This is an
   INSTRUMENT defect: it invalidates conclusions, not just renders.**

## Player mesh policy — many small, very visible ones

Upstream cause first: **`player/link_mesh_id_map.md` has NINE wrong labels** and is cited by
`link_midmask.h` as THE mesh-id reference, so the policy was written against bad labels. Most of the
below are downstream of it.

* Mirror Shield never appears anywhere in the game — adult raising it shows a HYLIAN shield
  (`link_midmask.cpp:32`); child holding it shows a Hylian shield on his back (`:53`).
* Child's Kokiri sword renders as the adult Master Sword — a blade nearly as long as he is tall, in
  every child swing/block/idle (`zelda3d_link.cpp:314`).
* Child slingshot: holds an **Ocarina of Time** instead; the slingshot never appears (`:329`).
* Megaton Hammer entirely missing — adult swings holding nothing (`link_midmask.cpp:23`).
* Hookshot/longshot and Ocarina show a flat open palm; `PLAYER_MODELTYPE_RH_OOT` isn't even in the
  switch (`link_midmask.cpp:36`).
* Broken Giant's Knife renders with the full intact 5100-unit blade; and with the BGS equipped Link
  wears a Master Sword on his back (a whole parallel back-geometry table set is never selected)
  (`link_midmask.cpp:20`).
* Child with Hylian/Mirror shield raised shows a DEKU shield (`zelda3d_link.cpp:326`).
* Child boomerang shows an object OoT3D never draws (`:315`).
* Empty sheath/strap vanishes when the sword is drawn with no shield (`link_midmask.cpp:53`).
* **Link's hands stay flat-open whenever he runs** — every locomotion state, both ages, the
  most-seen pose in the game (`zelda3d_link.cpp:311`); needs draw-time pose substitution, not just
  handType.
* Child Link sinks ~800 units (37% of hip height) on child-space clips without the root-motion Y pin —
  shield-block and tunnel crawl are ordinary gameplay (`zelda3d_link.cpp:466`).
* Bow/slingshot waist piece never drawn (adult mid 43, child mid 22) (`:306`).

## Useful negatives (do NOT spend a session on these)

* **mid 47 / child mid 25 are NOT missing geometry.** Posed-vertex comparison: mid 47 contributes only
  2 positions not already in mid 46, and child 25 shares 80 of its 97 unique points with 24 — they are
  co-located low-poly overlays that would z-fight rather than add silhouette. The existing code comment
  guesses "plausibly the far-LOD body"; the conclusion is right, the stated reason is not.
* `Zsi::envSettings()` has ZERO consumers — a dead parser whose header comments contradict the correct
  doc. Latent trap, no current effect.
* `tools/gen_scene_names.py` writes to a stale path and would drop the SCENE_TITLE/spot99 row if a
  session moved its output into place.

---

# CONFIRMED: MM3D CSABs are 100% frozen (operator verification, 2026-07-30)

Finding 1 above is real. Verified with a purpose-built harness compiled against the AUTHORITATIVE
C++ parser — deliberately not `tools/csab.py`, which finding 4 says diverges from the runtime
sampler and would therefore answer the wrong question. New tool: `tools/csab_anim_check.cpp`.

Method: sample `Csab::localTransforms` at frame 0 and mid-clip; if no bone's local rotation or
translation moves, every track was discarded and the actor is frozen in bind pose.

    MM3D  (subversion 5): 12 archives, 109 clips ->  ANIMATES=0   FROZEN=109  unparsed=0
    OoT3D (subversion 3):  6 archives,  61 clips ->  ANIMATES=60  FROZEN=1    unparsed=0  <- CONTROL

The OoT3D row is what makes the MM3D row meaningful: the check demonstrably detects animation, so
0/109 is a real result rather than a dead harness. Note `unparsed=0` on both — MM CSABs report
`ok()`, carry a plausible duration, and silently produce no motion. Nothing logs.

So `csab.cpp:115`'s "only the header field offsets + the anod-table base differ; the anod node layout
and the track encoding are identical" is FALSE for subversion 5. The header offsets it switches on are
evidently right enough to parse a node count and duration, but the per-node track layout is not.

NOT FIXED. This needs the MM3D anod/track layout reverse-engineered — the parser currently applies
the OoT3D track encoding to MM data, so the fix is a format port, not an offset tweak. It is also
squarely on the 2ship3d branch, which the codemap describes as early/native, so it may not be
blocking anything today; what matters is that it fails SILENTLY and would be read as "MM animation
isn't wired up yet" rather than "the parser is wrong".
