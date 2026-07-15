# childlink_v2.cmb mesh_id (mid) map — child Link equipment/hand variants

Derived 2026-06-18 by texture identification (tools/pica_texture dumps) + posed-geometry
classification (per-mesh bind-pose centroid/bbox) + in-game render sweep (REPL `linkmid only <n>`).

The CMB bakes EVERY hand-pose + held-equipment variant on distinct mesh_ids; OoT3D (like N64)
shows a state-dependent subset. N64 resolves the selection per-frame into
`player->leftHandType` / `rightHandType` / `sheathType` (PlayerModelType enum) + `currentShield`.
We translate those live values -> the matching mid set and push via Zelda3D_GL_SetMidMask.

Textures: p_tex02 = Hylian shield face (blue crest); p_tex17 = DEKU shield face (orange swirl);
p_tex20 = sword sheath/hilt wood; p_tex22 = gold sword guard/ring; p_tex21 = Deku stick (green bands);
p_tex08 = slingshot/sword blade (blue/grey); p_tex07 = boomerang (round); p_tex04/p_tex18/p_tex19 = misc.

## BODY (always shown)
- 24 = full body skin (torso/legs/arms childlink_00/01)
- 26 = head + face + eyes + mouth (childlink_01/f00/f01, c_eye, c_mouth)
- 25 = far-LOD body (near-empty) -> NEVER show

## LEFT hand (bones 15,16) — Link is left-handed: the SWORD hand
- 0  = open empty hand          (LH_OPEN, idle default)
- 1  = closed empty hand        (LH_CLOSED, also bottle)
- 2  = holding rod forward      (LH_BGS / giant-knife-ish)
- 6  = holding DEKU STICK       (p_tex21)
- 7  = open variant
- 8  = holding BOOMERANG         (p_tex07; LH_BOOMERANG, child)
- 15 = misc item (p_tex25, bone15)
- 16 = SWORD in hand            (LH_SWORD; blue blade)

## RIGHT hand (bones 19,20) — the SHIELD hand
- 3  = open empty hand          (RH_OPEN, idle default)
- 4  = closed empty hand        (RH_CLOSED)
- 5  = DEKU SHIELD on arm       (RH_SHIELD + deku; p_tex17)
- 17 = misc (p_tex18)
- 18 = SLINGSHOT                (p_tex04; RH_BOW_SLINGSHOT, child)
- 19 = misc (p_tex19)
- 20 = misc (p_tex19+p_tex23)

## BACK / sheath (bone 21)
- 9  = Hylian shield + sword on back   (SHEATH_18 + hylian)
- 10 = Hylian shield on back           (SHEATH_19 + hylian)
- 11 = DEKU shield + sword on back     (SHEATH_18 + deku)  <- normal child loadout
- 12 = Deku shield + guard on back
- 13 = DEKU shield on back (no sword)  (SHEATH_19 + deku)
- 14 = sword on back, NO shield        (SHEATH_16)
- 21 = guard/ring only (empty sheath)  (SHEATH_17)

## WAIST (bones 23,24)
- 22 = item (bone23, p_tex27)
- 23 = scabbard, long (bone24, p_tex24) — sword holder on back; currently left OFF

## Policy (Zelda3D_LinkComputeMidMask): body(24,26) + LH(leftHandType) + RH(rightHandType,currentShield)
##   + sheath(sheathType,currentShield). N64 keeps these self-consistent (sword-drawn => empty
##   sheath on back + sword in LH + shield on RH arm; stowed => open hands + shield+sword on back).

# =====================================================================================
# ADULT / boy link_v2.cmb mesh_id (mid) map
# =====================================================================================
Derived 2026-06-18 the SAME way as child: dump per-mesh material/tex/bones/posed-centroid
(`tools/link_cmb_dump.py /actor/zelda_link_boy_new.zar`), texture id (`tools/pica_texture` ->
scratch/link_boy/tex), and an in-game `linkmid only/<mask>` render sweep on adult Link
(scratch/link_boy/sweep* + montages). link_v2.cmb = 25 bones / 119 meshes / 36 mat / 41 tex.
SAME rig as child: LH(sword hand)=bones 15,16; RH(shield hand)=bones 19,20; back/sheath=bone 21;
waist=bones 23,24. Adult shield set is Hylian (default) / Mirror (child's was Deku / Hylian).

Textures: p_tex02 = HYLIAN shield face (blue + silver border, Triforce/wing crest);
p_tex03 = MIRROR shield panels (pink/white); p_tex08/p_tex09 = sword blade+guard;
p_tex12 = metal gauntlet cuff; p_tex01 = glove/hand skin; p_tex28/p_tex05 = bow (wood/grip);
p_tex10 = hookshot claw; p_tex16 = wood (shield back); link_00/01g = body skin; link_e/f/m = face.

## BODY (always shown)
- 45 = full body skin (link_00 + link_01g, all bones)
- 46 = head + face + eyes + mouth (link_m00/f00/f01/e00 + link_01g, bones 10,11,12)
- 47 = small face/blink piece (link_f00, bone 11) -> not used (46 already covers the face)

## LEFT hand (bones 15,16) — the SWORD hand
- 13 = open empty hand (idle default; p_tex01)
- 14 = closed empty hand (also bottle/hammer fallback; p_tex01)
- 16 = MASTER SWORD in hand (p_tex08 blue blade)
- 37 = BIGGORON/giant-knife in hand (p_tex09, very long blade)
- 38 = sword in hand variant (shorter; p_tex09)
- 32 = HOOKSHOT-ish item in hand (p_tex10 claw) ; 24,25,26,27 = other hand/held variants (unmapped)
- 4,5,6 = gauntlet-only cuffs (p_tex12, bone 15/16)

## RIGHT hand (bones 19,20) — the SHIELD/bow hand
- 20 = open empty hand (idle default; p_tex01)
- 21 = closed empty hand
- 23 = HYLIAN (or Mirror) shield held on the forearm (p_tex02)
- 30 = BOW drawn (p_tex05) ; 29 = bow variant
- 33,34 = p_tex06 items ; 39,40,41 = p_tex03/p_tex04 items (unmapped) ; 17,18,19 = gauntlet cuffs

## BACK / sheath (bone 21)
- 0  = HYLIAN shield + sword on back   (normal stowed adult loadout)
- 1  = HYLIAN shield on back, empty sheath (sword drawn)
- 2  = MIRROR shield + sword on back
- 3  = MIRROR shield on back, empty sheath
- 9  = bow on back ; 10 = Hylian shield + bow ; 12 = bow + sword
- 31 = sword on back, NO shield (also the deku-shield-on-adult fallback) ; 42 = strap only

## WAIST (bones 23,24)
- 43 = item (bone23, p_tex26) ; 44 = scabbard, long (bone24, p_tex27) — left OFF (like child)
## bones 5,8 mids (15,22,35,36; p_tex14/p_tex15) = low symmetric leg-attached items — left OFF

## Policy (Zelda3D_LinkBoyMidMask): body(45,46) + LH(leftHandType) + RH(rightHandType,currentShield)
##   + back(sheathType,currentShield with HYLIAN/MIRROR). Zelda3D_LinkComputeMidMask dispatches
##   here when LINK_AGE_IN_YEARS != YEARS_CHILD. VERIFIED live (Hyrule Field, adult, linkgear 2 2):
##   stowed = Hylian shield+Master Sword on back + open hands (mask 0x...102001); after btnhold B =
##   Master Sword in LH(16) + Hylian shield on RH arm(23) + empty back (mask 0x...810000). Mirror
##   (linkgear 2 3) -> mid 2 on back.  REPL `linkgear <sword 0-3> <shield 0-3>` equips for testing;
##   REPL `age <0|1> [entrance]` toggles adult/child (sets linkAge + linkAgeOnLoad, reload on warp).
