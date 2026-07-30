# Audit round 2b — player / animation / scene (the three areas that died on API 500)

Re-ran via workflow resume. These areas produced NO results in the first attempt (all three agents
crashed), so nothing here was in the earlier write-up. **Verify phase was still running when this was
recorded — treat everything as UNVERIFIED leads, not results.** Numbers are the agent's own.

## Scene / collision — CONFIRMED, and my first fix for it was WRONG (see the correction below)

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

---

# CONFIRMED + FIXED: tools/csab.py diverged from the C++ sampler — and I caused it

Finding 4 is real, and the cause is my own incomplete fix from earlier the same session.

On 2026-07-29 I fixed `cmb3d/asset/csab.cpp` to decode LINEAR rotation tracks inside an int16 anod at
the quantized `{u16 time, s16 angle}` layout instead of `{u32 time, f32 value}` (2951 tracks, 2932 of
them Link's). I did NOT mirror it into `tools/csab.py`. So from that commit until now the two
decoders disagreed on exactly the tracks the parity work targets, and every offline consumer —
`csab_render.py`, `csab_xcheck.py`, `link_sweep.py`, `model_match.py`, the pose-parity A/B — was
reading denormals (~1e-45 standing in for real angles) and outliers like 1.77e22 rad while the game
drew the correct pose. An offline "match" could have certified a wrong pose, and an offline-vs-runtime
gap would have been read as a runtime bug.

Fixed by porting the same branch. Verified: `boy/anim/sude_nwait.csab` now decodes through python to

    bone  5 rY  0.0948      bone 11 rX  3.1415      bone 11 rY -3.1415
    bone 15 rX -0.0453      bone 16 rY  0.1521      bone 16 rZ -1.5556
    bone 19 rX  0.0455      bone 20 rY -0.1176

which is value-for-value what the C++ fix produced when it was measured on 2026-07-29 (±pi and
-pi/2 land exactly).

SCOPE LIMIT: this closes ONE concrete divergence — the one I introduced. The finding asserted a
general "not a faithful twin", and I have not proven the two are now equivalent across every path
(hermite tangents, the wrap/unwrap rules, the static-translation rule at csab.cpp:344 which is itself
finding 3). A real equivalence check would sample both decoders over the whole CSAB corpus and diff;
`tools/csab_anim_check.cpp` now gives the C++ side of that harness.

LESSON worth keeping: a fix applied to one of two parallel implementations SILENTLY creates an
instrument that disagrees with the runtime. The twin is not documentation, it is a measuring device.

---

# Child mid 16 ("Kokiri sword renders as the adult Master Sword") — NOT CONFIRMED

Checked with `linkmid only <n>` on child Link (REPL mesh-id isolation), frozen, `acam 120 z`:

* **mid 24** renders the child body (tunic, boots) — matches the map.
* **mid 16** renders a SWORD mesh, correctly positioned at the left hand, with a blue/purple hilt,
  at a blade length that looks proportionate for the child. Render:
  `scratch/screenshots/mid_compare.png`.

The finding claimed "a blade nearly as long as he is tall". That is NOT what the render shows. The
mesh is a plausible child sword. Distinguishing Kokiri from Master conclusively would need a
proportional comparison against the adult rig's blade or a texture identification — neither of which
I did — so this is "not confirmed and looks doubtful", not "refuted".

MEASUREMENT NOTE: my first attempt measured pixel extents by differencing each isolated-mesh frame
against a `linkmid only 63` "empty" frame. Different meshes came back with IDENTICAL widths (682,
682 and 551, 551), which is the tell that the difference was scene content, not the mesh — mask 63 is
not empty. The VISUAL is the evidence here; the pixel numbers from that pass are worthless and are
not quoted.

Consequence for the rest of the player cluster: those findings all descend from
`link_mesh_id_map.md`'s labels, and one spot-check already fails to reproduce. They should be treated
as individually unverified rather than as a block, and the nine "wrong labels" claim needs its own
per-label check before anyone rewrites the policy against it.

---

# CONFIRMED: child mid 18 is the OCARINA, not the slingshot (2026-07-30)

Second of the nine claimed mislabels independently confirmed, after the Mirror Shield one.

Isolated child hand meshes in game (`linkmid only <n>`, child Link, frozen, `acam 110 z`) —
renders in `scratch/screenshots/child_hand_ids.png` and `slingshot_check.png`:

    mid 18  hand holding a BLUE instrument with finger holes   -> OCARINA OF TIME
    mid 19  hand holding a straight brown shaft with red ends  -> NOT a forked slingshot
    mid 22  nothing renders
    mid  8  a grey/white bottle
    mid  6  hand holding an orange stick  -> Deku stick

The mesh map calls 18 "SLINGSHOT, p_tex04". It is not. Consequences of that one label:
* `RH_BOW_SLINGSHOT -> mid 18` draws the OCARINA when Link aims the slingshot (the reported bug,
  CONFIRMED), and
* `RH_OCARINA` fell through to the default `mid 3` = OPEN HAND, so playing the ocarina showed an
  empty palm **while the correct mesh sat unused**. The audit reported that as a separate finding;
  both are the same mislabel.

FIXED the half that is certain: `RH_OCARINA -> mid 18`.

NOT fixed, deliberately: the slingshot still points at 18. Its real mesh is unidentified — mid 19 is
the obvious neighbour and is clearly NOT it. Remapping on a guess would swap one wrong item for
another, and drawing the ocarina there is the pre-existing behaviour rather than a new regression.
Identifying it needs a sweep of the remaining child hand mids (bones 19/20) against the slingshot's
actual silhouette.

## Slingshot mesh NOT FOUND after a 12-mid sweep — it may not be in the Link CMB at all

Swept child mids 6, 7, 8, 9, 10, 12, 18, 19, 20, 21, 22, 23 with `linkmid only <n>` (child Link,
frozen, `acam 110 z`). Renders: `scratch/screenshots/child_hand_ids.png`, `slingshot_check.png`,
`child_sweep.png`. Identified:

    6   hand + orange stick        -> Deku stick
    7   small fist
    8   grey/white bottle
    9   Hylian shield + sword hilt on back
    10  Hylian shield + sword hilt on back  (near-identical to 9)
    12  DEKU shield (round, wooden) + sword hilt on back
    18  hand + blue instrument with finger holes -> OCARINA  (confirmed above)
    19  hand + straight brown shaft with red ends -> NOT a forked slingshot
    20  forearm with a red grip at the wrist
    21  sword hilt only on back
    22  renders nothing
    23  tall thin brown pole

**No forked slingshot frame appears in any of them.** Two possibilities, and I am not guessing
between them:
1. it is in a child mid outside this set, or
2. **the slingshot is a SEPARATE object, not baked into childlink_v2.cmb** — which is exactly what
   the adult code already assumes for the hookshot ("adult hookshot model drawn separately -> empty
   hand"). If so, "the slingshot never appears" is a bug in drawing that separate model, NOT a
   mesh-id mapping bug, and remapping any mid would be the wrong fix entirely.

Deciding this needs the OoT3D decomp checked for how `PLAYER_MODELTYPE_RH_BOW_SLINGSHOT` sources its
geometry — i.e. whether the 3DS player draw pulls a second model for it. That is RE work, not a
sweep, and it is the correct next step rather than more isolation captures.

Useful side effect: the ids above are FIRST-HAND identifications and can be used to check
`link_mesh_id_map.md` per-label instead of trusting it. Note 9 and 10 look near-identical here, which
matches the map's own "9 = Hylian shield + sword on back / 10 = Hylian shield on back" only loosely —
the difference is not visible at this camera.

## Decomp check for the slingshot's geometry — the available fragment does not cover it

Checked `oot3d-decomp/build/decomp/004c11f4.c` (Player_DrawImpl) for a second model draw that would
mean the slingshot is a separate object. It does not answer the question: the `SetMeshVisible` calls in
that fragment are ids 4, 0x11(17), 5|6, 0x12(18)|0x13(19), 0xf(15), plus two table-driven ones — i.e.
the GAUNTLET-plate and sword/shield-table portion (the same pattern already ported: plate 1 on both
arms, then an open/closed variant per hand). There is no held-item/slingshot branch in it.

So the slingshot question is still open, and answering it needs the CHILD model-type -> mesh table
located in code.bin — not this fragment.

CAUTION worth recording, because I nearly tripped on it: **mesh ids are PER-RIG.** Adult 18/19 are
gauntlet plates (link_v2); child 18 is the OCARINA (childlink_v2), verified visually. Both are true and
they do not conflict. `link_mesh_id_map.md` has separate adult and child sections for exactly this
reason, and any cross-referencing between a decomp fragment and a mid must first establish which rig
the fragment is drawing.

---

# CONFIRMED + FIXED: the short-way unwrap was applied to FLOAT hermite rotation (finding 2)

Real, but **9 clips, not the 103 claimed** — an ~11x overstatement.

`sampleLocalTRS` passed `rotation=true` to `sampleTrack` for every rotation track, so the unwrap
`p1 = p0 + (fmod(p1-p0+PI+TAU, TAU) - PI)` forced |delta| <= pi on float tracks too. Its own comment
justifies it for int16 values "wrapped to [-pi,pi)", which floats are not.

Swept the whole OoT3D corpus (151 zar with csab, 2465 clips):

    float hermite rotation segments examined : 597120
    segments with |delta| > pi               :     10
    clips affected                           :      9

**That distribution is itself the proof floats are not wrapped.** If they were, any bone rotating past
+-pi would produce a straddling pair, and |delta|>pi segments would be everywhere. Ten out of 597120,
with two of them EXACTLY +-2pi, means the tracks are continuous and those ten are authored large turns:

    fire_dancer_jump      rX -6.283  (exactly -2pi -> collapsed to NO motion)
    gnf_lonlyTSUKI02      rY +6.283  (exactly +2pi -> collapsed to NO motion)
    vba_tyokkai           rX  5.078  (291 deg -> reversed to -69 deg)
    vba_hit               rZ -4.621  (264 deg -> reversed to +95 deg)
    vba_hit               rX  4.053
    gnf_Ddamage           rX  3.932

Affected actors: Ganondorf (gnf), the Flare Dancer, Barinade (vba) — boss animations, which is why
"boss intros/deaths" was in the original report even though the count was wrong.

FIX: gate the unwrap on `node->isRotInt16` instead of applying it unconditionally.

NO REGRESSION RISK to Link, and this was checked rather than assumed: his rotation tracks are 100%
int16 (2171 adult / 2210 child, ZERO float), so the unwrap that originally fixed his spinning
head/back-shield/arms is fully preserved. Verified live at Kokiri — correct pose, no spinning bones.


---

# Correction: the collision defect was the ANCHOR, not the count

I fixed this twice. The first attempt was a bandaid and this records why, because the reasoning error is
more useful than the fix.

My test asked: "is there a valid polygon record PAST the end of the parsed range?" Answer: yes, in
114/114 scenes. I concluded the count field must be a last-index and read nPoly+1 records. That made the
symptom go away — the missing polygon came back — but it never asked whether the array STARTS in the
right place. It does not: `pPoly - 2` is exactly one 20-byte record early, so my change produced a
superset: all the real polygons PLUS a phantom record 0 fabricated from vertex-array tail bytes.

The audit agent brute-forced anchor x layout instead of testing one hypothesis, which is what found it:

    pPoly + 18 : 170175/170175 valid (100.000%)   index -1 valid in 0 scenes, index nPoly in 0
    pPoly -  2 : 170061/170175 valid ( 99.933%)   index -1 valid in 0 scenes, index nPoly in 114

Reproduced independently before changing anything. The tell I missed is in the second column: at the
right anchor NOTHING valid exists on either side of the array. A fix that leaves a valid record hanging
just past the end is not done.

Also fixed by the correct anchor, which my version left broken: 107 out-of-bounds vertex indices and 7
non-unit normals that the bounds check was silently absorbing — one of them classified as a WALL in 7
scenes, and in Gerudo Valley a `dist` of 7.66e34 that `zelda3d_collision.cpp:441` casts to s16
(undefined behaviour).

LESSON: "the symptom is gone" is not "the cause is found" — the project's own no-bandaids rule, and I
broke it. A single-hypothesis test confirms the hypothesis; a SWEEP over the parameter space finds the
truth. Sweeping anchor x layout cost the agent one script and settled it at 100.000%.
