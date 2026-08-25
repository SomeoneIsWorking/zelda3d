#include "../behaviors/actor/gerudo_white.h"
#include "../behaviors/actor/actor_assets.h"
#include "replacement_catalog.h"

#include <cstddef>
// Per-actor OoT3D model table. Maps an N64 actor id to the OoT3D model dlist that
// replaces its N64 draw, plus that model's world scale. This is the generalised
// divert: instead of editing each actor's Draw with an `if (Zelda3D_Enabled())`
// block, Actor_Draw consults this table once for every actor (Zelda3D_TryDrawActor)
// and, on a hit, draws the OoT3D model and skips the N64 draw. Add an object by
// adding a row here — no actor-source edits.
// En_Ge1 (white Gerudo): map her N64 animation -> the OoT3D CSAB, and phase-sync to her
// SkelAnime clock (Zelda3D_ResolveAnim_EnGe1 / Zelda3D_Joints_EnGe1, ported to
// behaviors/actor/gerudo_white.cpp — see gerudo_white.h for the declarations used below).

// Non-const so the REPL can tune worldScale/groundOffset live.
Zelda3D_ModelEntry sModelTable[4] = {
    { ACTOR_OBJ_TSUBO, "pot", ZELDA3D_POT_WORLD_SCALE, 3, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_OBJ_KIBAKO2, "kibako", ZELDA3D_KIBAKO_WORLD_SCALE, 1, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_EN_KUSA, "kusa", 0.5f, 2, NULL, 0.0f, NULL, NULL, 0 }, // bush (scale tuned live via REPL)
    { ACTOR_EN_GE1, "geldwoman", ZELDA3D_GELDWOMAN_WORLD_SCALE, 0, "ge1_s_wait", ZELDA3D_GELDWOMAN_GROUND_OFFSET,
      Zelda3D_ResolveAnim_EnGe1, Zelda3D_Joints_EnGe1, 1 },
};

// Per-ACTOR forced CMB override. Some ZARs hold multiple CMBs — one per actor sharing the
// object bank slot (e.g. zelda_mu.zar has couple.cmb for EN_TG's dancing couple + marketpeople.cmb
// for EN_MU's haggling townspeople). AUTO's "largest CMB" heuristic picks one CMB per zar and
// so gives every actor sharing that zar the SAME model — the wrong one for at least one of them.
// This table lets a specific actor id (optionally scoped by params) force AUTO to use
// "<zar>|<cmbSubstr>" instead, routed through its OWN sAuto slot (below) so a peer actor's
// correct CMB isn't stomped.
//
// Two divergence patterns are supported:
//  1. Actor id ⇔ CMB (paramMask = 0):  EN_TG → couple.cmb (zelda_mu.zar) regardless of params.
//  2. Actor id + (params & mask == value) ⇔ CMB:  Obj_Syokudai's three torch styles select via
//     `params >> 12` in N64 z_obj_syokudai.c:263 (Golden/Timed/Wooden) — the same actor draws
//     three DIFFERENT display lists at N64 time, so on OoT3D it must pick different CMBs from
//     zelda_syokudai.zar (syokudai_gn / syokudai_model / syokudai_ki_model) per bucket.
//
// Verified structurally by tools/en_tg_cmb_close_test.py + tools/syokudai_cmb_close_test.py.

static Zelda3D_ActorForcedAutoSlot sActorForcedAuto[] = {
    // Dancing couple (peer EN_MU keeps marketpeople.cmb via default AUTO).
    { ACTOR_EN_TG, 0, 0, "couple", 0, { 0 } },
    // Wooden torch — Obj_Syokudai draws gWoodenTorchDL when (params >> 12) == 2. Route to
    // syokudai_ki_model.cmb ("ki" = wood, JP). Golden torches (params >> 12 == 0) keep the
    // default AUTO pick (syokudai_gn_model.cmb — largest CMB, correct default). Timed torches
    // (params >> 12 == 1) still fall through to AUTO for now — their OoT3D CMB match is
    // uncertain between syokudai_model.cmb and a variant; separate follow-up.
    // WITHDRAWN 2026-07-30 -- this routing DELETED THE FLAME and shipped that way.
    // ObjSyokudai_Draw emits the torch stand AND, whenever litTimer != 0, gEffFire1DL at its own
    // billboarded matrix -- two display lists in ONE draw. A forced slot replaces the actor's whole
    // draw (`if (!Zelda3D_TryDrawActor(...)) actor->draw(...)` in z_actor.c), so substituting the
    // stand silently dropped the fire from every lit wooden torch. A lit torch with no flame is a
    // broken game object; an N64-shaped torch is not, so the faithful draw wins until the mechanism
    // can do better.
    // THE PROPER FIX is per-DISPLAY-LIST routing (substitute the stand, let the actor's remaining
    // lists through) or a supplementary-draw hook. Both are real mechanism work, not a table edit --
    // note that simply drawing ours AND running actor->draw double-draws the stand, so that shortcut
    // is not available. Restore this entry only together with one of those.
    // { ACTOR_OBJ_SYOKUDAI, 0xF000, 0x2000, "syokudai_ki", 0, {0} },
    // Deku Tree mouth. zelda_spo04_objects.zar (note: "spo04", OoT3D drops the t) holds ELEVEN CMBs
    // -- the mouth plus a set of Y_*/ousei_* cutscene models -- so AUTO's largest-CMB pick would give
    // Bg_Treemouth a cutscene mesh. Route it explicitly. "kuchi" is Japanese for mouth.
    // This is a non-skinned entry, so it depends on the forced-slot measure-key fix: before that,
    // non-skinned forced entries could never complete a measurement and always fell back to N64.
    { ACTOR_BG_TREEMOUTH, 0, 0, "spot04_kuchi", /*noBaseAnchor=*/1, { 0 } },
    // Forest Temple props. SEVEN actors declare OBJECT_MORI_OBJECTS (confirmed from each one's
    // ActorInit, not just a mention), and zelda_mori_objects.zar holds nine CMBs -- so AUTO's
    // largest-CMB pick handed ALL of them l_elevator_model.cmb (41216 bytes, the biggest). Six of the
    // seven therefore rendered as the elevator platform.
    // The mapping is name-exact once the Japanese is read: bigst = big stone, hasira = pillar (4 of
    // them), hasigo = ladder, idomizu = well water, kaiten = rotating (wall), tenjyou = ceiling
    // (rakka = falling, i.e. the falling-ceiling trap). Every actor gets its own mesh:
    { ACTOR_BG_MORI_BIGST, 0, 0, "l_bigst", 0, { 0 } },
    { ACTOR_BG_MORI_HASHIRA4, 0, 0, "l_4hasira", 0, { 0 } },
    { ACTOR_BG_MORI_ELEVATOR, 0, 0, "l_elevator", 0, { 0 } },
    // Bg_Mori_Hashigo is TWO props, not one: BgMoriHashigo_Draw switches on params between
    // gMoriHashigoClaspDL (HASHIGO_CLASP = -1, i.e. 0xFFFF as u16) and gMoriHashigoLadderDL
    // (HASHIGO_LADDER = 0). A params-agnostic slot gave the CLASP the ladder mesh. The clasp's CMB
    // was sitting in this same ZAR the whole time, listed below as an unrouted "ladder variant/stop":
    // l_hasigotome -- "tome" is the Japanese for catch/clasp -- and its geometry settles it, a 20 x 46
    // x 40 bracket against the ladder's 32 x 227 x 2 rail.
    { ACTOR_BG_MORI_HASHIGO, 0xFFFF, 0xFFFF, "l_hasigotome", 0, { 0 } },
    { ACTOR_BG_MORI_HASHIGO, 0xFFFF, 0x0000, "l_hasigo", 0, { 0 } },
    // INERT BY CONSTRUCTION, kept for intent. l_idomizu is the Forest Temple well WATER: bbox
    // 2763 x 0 x 289, i.e. a horizontal plane with EXACTLY ZERO height. The bbox-height measure can
    // never derive a scale for it (the modelH > 1e-3 guard fails), so this slot parks at
    // ZELDA3D_AUTO_NOMEAS forever and the N64 draw stays -- which is why it reads state=4 in autostate
    // rather than that being a transient miss. Making it real needs footprint sizing via
    // Zelda3D_AutoModelExtentXZ (the path Bg_Spot01_Idomizu's well water already uses), which in turn
    // needs an XZ measure since the bracket only reports height.
    { ACTOR_BG_MORI_IDOMIZU, 0, 0, "l_idomizu", 0, { 0 } },
    { ACTOR_BG_MORI_KAITENKABE, 0, 0, "l_kaiten", 0, { 0 } },
    { ACTOR_BG_MORI_RAKKATENJO, 0, 0, "l_tenjyou", 0, { 0 } },
    // Dodongo's Cavern props. THREE actors declare OBJECT_DDAN_OBJECTS and the ZAR holds five CMBs,
    // so AUTO's largest-CMB pick gave all three ddanh_kaidan_model (56832 bytes, the staircase) --
    // two of the three were rendering a staircase. The Japanese resolves every one of them:
    //   kd       = KAIDAN, staircase          -> ddanh_kaidan  (this is the one AUTO already picked,
    //                                            i.e. correct only by accident; routed to make it
    //                                            explicit so a future CMB-size change cannot silently
    //                                            move it)
    //   dodoago  = Dodongo + AGO, jaw         -> ddanh_ago     (was the staircase)
    //   jd                                    -> ddanh_jd      (was the staircase; exact name match)
    // Wallmaster / Floormaster share zelda_wm2.zar, and floormaster.cmb and fallmaster.cmb are the
    // SAME SIZE (26496 bytes each) -- so "largest CMB" is a coin flip between them and En_Wallmas was
    // getting the Floormaster mesh. Both routed explicitly, because a tie-break by file order is not
    // something to leave load-bearing. ("fallmaster" is Grezzo's spelling of Wallmaster.)
    // Deku Tree WALL web. Selector is params & 0xF == 1 -- Bg_Ydan_Sp's Init rewrites params to the
    // type, so match the LIVE value, not the packed 0xF000 field (z_bg_ydan_sp.c:100; WEB_FLOOR=0,
    // WEB_WALL=1). CMB names agree: spkabe (kabe = wall) / spyuka (yuka = floor).
    //
    // THIS WAS REVERTED TWICE ON A BAD MEASUREMENT, so the reasoning is recorded here. The web is a
    // FLAT single-sided plane (bbox 2800 x 2888 x ZERO) and its material is cull=1, so it is visible
    // only from its front hemisphere -- which is CORRECT, and matches what OoT3D itself does. My
    // `ahide` pixel check used one camera angle that happened to sit on its BACK, read 0 px, and I
    // called it a regression. An orbit sweep settles it:
    //     azimuth   0 /  45 /  90 / 135  ->     0 px   (behind the plane: correctly culled)
    //     azimuth 180 / 225 / 270 / 315  -> 13589 / 19865 / 20478 / 11919 px   (visible)
    // The N64 mesh draws from both sides, which is why the N64 web appeared from the angle where ours
    // does not. Matching OoT3D is the goal, so single-sided is right.
    //
    // Also confirmed offline, against the same three volumes used as controls: this web winds 100%
    // CCW-from-normal, exactly like every other model (l_elevator 576/576, ddanh_jd 56/56,
    // floormaster 484/484). So the asset is NOT mis-wound and the global front-face convention is not
    // implicated -- an earlier note claiming otherwise was wrong and is retracted.
    //
    // The FLOOR web stays unrouted for an unrelated reason: it is horizontal, so its model height is
    // ~0 and the bbox-height measure cannot derive a scale. Flat props need Zelda3D_AutoModelExtentXZ
    // footprint sizing, as Bg_Spot01_Idomizu's well water already does.
    // Deku Tree webs and water. All three are FLAT or near-flat, which is why they only became
    // routable once the measure bracket started reporting a FOOTPRINT as well as a height.
    //   WEB_WALL  (params & 0xF == 1) -> ydan_spkabe (kabe = wall)
    //   WEB_FLOOR (params & 0xF == 0) -> ydan_spyuka (yuka = floor)
    // Match the LIVE params: Bg_Ydan_Sp's Init rewrites actor->params to the type nibble
    // (z_bg_ydan_sp.c:100), so the packed 0xF000 field is wrong here.
    // Both webs are cull=1 single-sided planes -- verify them with an ORBIT sweep, never one camera
    // angle. A single angle on the back of a plane reads 0 px and looks exactly like a broken routing;
    // that mistake cost two spurious reverts of this very entry.
    { ACTOR_BG_YDAN_SP, 0x000F, 0x0001, "ydan_spkabe", 0, { 0 } },
    { ACTOR_BG_YDAN_SP, 0x000F, 0x0000, "ydan_spyuka", 0, { 0 } },
    //   Bg_Ydan_Hasi -> ydan_mizu (mizu = water). Its N64 draw is gDTWaterPlaneDL, the WATER PLANE, not
    //   a bridge: "hasi" (bridge) and "hasigo" (ladder) are different words that a substring matcher
    //   conflates, which is why the actor's draw code decides the mapping and the CMB name only
    //   suggests it. cull=3 here (double-sided), so any camera angle is valid for this one.
    { ACTOR_BG_YDAN_HASI, 0, 0, "ydan_mizu", 0, { 0 } },
    // Bg_Ydan_Maruta. Its draw branches on params: 0 = gDTRollingSpikeTrapDL, non-zero =
    // gDTFallingLadderDL (z_bg_ydan_maruta.c:203). The Deku Tree instances are all params=1, i.e. the
    // FALLING LADDER, and the mesh was identified by MEASUREMENT rather than by name:
    //     N64 draw measured h=135  foot=32x2
    //     ydan_t_hasigo  323 x 1360 x 20  -> ratios h 0.0993, x 0.0991, z 0.100   ALL THREE AGREE
    //     ydan_maruta   4000 x 1243 x 3900 -> 0.109 / 0.008 / 0.0005              inconsistent
    //     ydan_ytoge    4816 x  440 x  414 -> 0.307 / 0.0066 / 0.0048             inconsistent
    // Name matching would have picked ydan_maruta ("maruta" = log) and been wrong by 200x on Z. It
    // would also have handed ydan_t_hasigo to Bg_Ydan_Hasi, which actually draws the WATER PLANE.
    //
    // params=0 (the rolling spiked log) is NOT routed: no params=0 instance exists in the rooms swept,
    // so there is no measurement for it. ydan_maruta_model at 4000x1243x3900 is the plausible candidate
    // on both name and bulk, but plausible is what this method exists to replace.
    { ACTOR_BG_YDAN_MARUTA, 0x00FF, 0x0001, "ydan_t_hasigo", 0, { 0 } },
    // Water Temple props. Four of the five OBJECT_MIZU_OBJECTS actors draw exactly one display list
    // (gObjectMizuObjects{Bwall,Movebg,Shutter,Water}DL; bg_mizu_uzu draws none) against 18 CMBs in the
    // archive, so each was identified by MEASURING its N64 draw and keeping the candidate whose
    // height/X/Z ratios agree.
    //
    //   water   n64 h=0 foot=1920x1900 -> m_Wsea00_Mov_modelT (1925 x 0 x 1880): both axes agree to
    //           0.4% (scale 1.00407). Flat water surface, so footprint-derived. CONFIRMED.
    //   movebg  n64 h=85 foot=120x120  -> m_WFloat00W_model (1200 x 853 x 1200): ratios 0.0996/0.1/0.1,
    //           spread 1.00x. Note this also DISCRIMINATES against its sibling m_WFloat00S (1200x800x1200)
    //           which is 6% off on height -- the three-axis test separates even near-identical variants.
    //   shutter n64 h=160 foot=160x0 -> m_Wshutter1_model, on NAME (geometry cannot decide: a flat plane
    //           yields only two ratios and m_WFloat01/m_Wbomb00E/m_Wbomb0eE/m_Wbomb0eW all share the same
    //           1200x1200 X/Y, tying at spread 1.00x). It DRAWS -- `submitted` reports 4584 submissions.
    //           It was briefly reverted on a 0-pixel reading, which was a FALSE NEGATIVE: the pixel check
    //           cannot see this prop from any camera tried (a closed shutter sits flush in its wall), and
    //           "no pixels" was mistaken for "not drawn". Mesh IDENTITY is still name-based and unproven;
    //           what is now proven is that the routing is not deleting the door.
    //   bwall   NOT ROUTED. Its model resolves as SKINNED, so it takes the bone-length scale path and
    //           never produces a bbox measurement (n64h=0 foot=0x0) -- this identification method cannot
    //           see it at all.
    { ACTOR_BG_MIZU_MOVEBG, 0, 0, "m_WFloat00W", 0, { 0 } },
    { ACTOR_BG_MIZU_WATER, 0, 0, "m_Wsea00", 0, { 0 } },
    { ACTOR_BG_BDAN_SWITCH, 0x00FF, 0x0000, "bdan_switch_b", 0, { 0 } },
    // Jabu-Jabu platforms + water. The cleanest row so far: z_bg_bdan_objects.c has an EXPLICIT DL table
    // indexed straight by params (Init masks params to 0xFF), and every entry is corroborated by the CMB
    // name, so there is no guessing at any step:
    //     params 0 -> gJabuObjectsLargeRotatingSpikePlatformDL -> bdan_toge   (toge = spike)
    //     params 1 -> gJabuElevatorPlatformDL                  -> bdan_ere    (erebeeta = elevator)
    //     params 2 -> gJabuWaterDL            (drawn XLU)      -> bdan_bmizu  (mizu = water; the CMB's
    //                                                            "modelT" suffix agrees it is translucent)
    //     params 3 -> gJabuFallingPlatformDL                   -> bdan_fdai   (dai = platform/stand)
    { ACTOR_BG_BDAN_OBJECTS, 0x00FF, 0x0000, "bdan_toge", 0, { 0 } },
    { ACTOR_BG_BDAN_OBJECTS, 0x00FF, 0x0001, "bdan_ere", 0, { 0 } },
    { ACTOR_BG_BDAN_OBJECTS, 0x00FF, 0x0002, "bdan_bmizu", 0, { 0 } },
    { ACTOR_BG_BDAN_OBJECTS, 0x00FF, 0x0003, "bdan_fdai", 0, { 0 } },
    { ACTOR_BG_MIZU_SHUTTER, 0, 0, "m_Wshutter1", 0, { 0 } },
    // Jabu-Jabu BLUE floor switch. Bg_Bdan_Switch selects on `params & 0xFF` (its header: 0 BLUE,
    // 1 YELLOW_HEAVY, 2 YELLOW, 3 YELLOW_TALL_1, 4 YELLOW_TALL_2), and the CMB names are corroborated by
    // the N64 DL names (bdan_switch_b/y <-> gJabuBlue/YellowFloorSwitchDL) -- two independent naming
    // systems agreeing, which is better evidence than one name.
    //
    // It DRAWS: `submitted` reports 708 submissions. It was briefly not routed because the pixel check
    // read 0 px across 6 instances -- a FALSE NEGATIVE, since a floor switch sits flush with the floor and
    // a hide/show diff cannot see it.
    //
    // The TALL variants (3, 4) are still NOT routed, on measurement rather than fear: type 4 measures
    // h=39.8 foot=37x37, a narrow PILLAR, against this wide flat pad -- a 5.2x gap. Types 1 and 2 are
    // unrouted only because no instance has been measured yet. Note the two switch CMBs are GEOMETRICALLY
    // IDENTICAL (921 x 191 x 921) and differ only in texture, so the three-axis test can reject a wrong
    // shape here but can never pick blue-vs-yellow.
    { ACTOR_EN_FLOORMAS, 0, 0, "floormaster", 0, { 0 } },
    { ACTOR_EN_WALLMAS, 0, 0, "fallmaster", 0, { 0 } },
    // King Dodongo's ZAR also holds his fire breath. AUTO picked kingdodongo.cmb (137216 bytes), so
    // En_Bdfire -- the fire -- was rendering the entire boss body. Boss_Dodongo routed too: it was
    // correct only because it happens to be the largest.
    { ACTOR_BOSS_DODONGO, 0, 0, "kingdodongo", 0, { 0 } },
    { ACTOR_EN_BDFIRE, 0, 0, "g_ddg2_fire_model", 0, { 0 } },
    // Gerudo Valley gate + fence (saku = fence). AUTO picked s12saku_model (49408 > 37120), so the
    // GATE was rendering as the fence.
    { ACTOR_BG_SPOT12_SAKU, 0, 0, "s12saku", 0, { 0 } },
    { ACTOR_BG_SPOT12_GATE, 0, 0, "s12gate", 0, { 0 } },
    { ACTOR_BG_DDAN_KD, 0, 0, "ddanh_kaidan", 0, { 0 } },
    { ACTOR_BG_DODOAGO, 0, 0, "ddanh_ago", 0, { 0 } },
    { ACTOR_BG_DDAN_JD, 0, 0, "ddanh_jd", 0, { 0 } },
    // Unrouted leftover in that ZAR: l_tikaori_model. No actor name matches it, so it is deliberately
    // left alone rather than guessed onto an actor. (l_hasigotome_model was on this list until
    // 2026-07-30, described as "a ladder variant/stop" -- it is the Bg_Mori_Hashigo CLASP and is now
    // routed above. The lesson: a leftover is a lead, not a dead end. Reading the actor's DL SWITCH
    // found its owner where reading CMB names alone had not.)
    //
    // Ice Cavern props. SIX actors reach OBJECT_ICE_OBJECTS and zelda_ice_objects.zar holds EIGHT
    // CMBs, so AUTO's largest-CMB pick handed every one of them ice_wall_modelT (1614 verts, the
    // biggest) -- five actors rendering a sheet of red ice. The N64 side has exactly eight display
    // lists in that object, and the split is readable from each actor's own draw:
    //
    //   Bg_Ice_Turara   -> DL_0023D0                  -> ice_turara  (tsurara = icicle; name-exact)
    //   Bg_Ice_Objects  -> DL_000190                  -> ice_brick   (the pushable block)
    //   Bg_Ice_Shutter  -> DL_002740                  -> ice_wall2   (opaque 1746 x 2008 x 77 slab)
    //   Bg_Haka_Sgami   -> DL_0021F0 (params != 0)    -> ice_trap    (the scythe trap; 6254 wide arm)
    //   Bg_Ice_Shelter  -> gRedIce{Block,Platform,Wall}DL, by (params >> 8) & 7
    //   Door_Shutter    -> DL_001D10                  -> ice_tobira  (already routed in
    //                                                    behaviors/actor/door_shutter.cpp)
    //
    // Bg_Ice_Shelter is the only param-split one. Its three DLs all go to POLY_XLU (red ice is
    // translucent), and the ZAR holds EXACTLY THREE `modelT` meshes -- ice_ice_modelT,
    // ice_ice3_modelT, ice_wall_modelT -- while every other CMB here is a plain `model`. That the
    // translucent set and the XLU draw set have the same size and membership is the corroboration;
    // within the set, block-vs-platform is separated by shape: ice_ice is near-cubic
    // (950 x 1005 x 945) like the block, ice_ice3 is wide and 3-group (1499 x 1027 x 1507) like the
    // "complex structure that can be climbed", and ice_wall is the sheet.
    //
    // THE BLOCK MESH NEEDS THREE SLOTS, NOT ONE, and this row is where that mechanism limit first
    // bites. A slot stores ONE derived scale, and that scale is measured off the N64 draw *including
    // the actor's own scale* -- Zelda3D_DrawModelGL applies `worldScale` alone and never multiplies
    // actor->scale. Bg_Ice_Shelter scales itself per type (`sRedIceScales[] = { 0.1, 0.06, 0.1, 0.1,
    // 0.25 }`), so LARGE/SMALL/KING_ZORA sharing one slot would render at whichever size happened to
    // be measured first -- a 4.2x error between the extremes. Measured proof that the scale really is
    // the actor's and not a re-authoring ratio: a SMALL instance derives scale 0.06000 against a CMB
    // 1005 units tall, i.e. exactly sRedIceScales[RED_ICE_SMALL], so the CMB is dimensionally 1:1 with
    // the N64 display list and everything else is actor scale.
    //
    // KING_ZORA is routed but is KNOWN IMPERFECT: it is the one type with a NON-UNIFORM scale
    // (kzIceScale = { 0.18, 0.27, 0.24 }), and a single `worldScale` cannot express that. Its height
    // measure yields 0.27, so the block comes out ~1.5x too wide in X. It is still routed because the
    // alternative is worse, not because it is right: unrouted it falls to AUTO's largest-CMB pick,
    // which is ice_wall_modelT -- a sheet of wall instead of a block. Right mesh at a wrong aspect
    // beats the wrong mesh. THE PROPER FIX is per-axis scale: the measure already reports three
    // independent numbers (n64h, measFootX, measFootZ) that the three-axis cross-check consumes, so
    // the data is there and only the draw path is uniform. Not done here because it would change the
    // transform of all ten already-verified routings at once.
    //
    // Order matters: Zelda3D_FindActorForcedSlot takes the FIRST match. Every entry here is fully
    // specified on 0x0700 so order is not load-bearing, but do not add a catch-all above them.
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0x0000, "ice_ice_modelT", 0, { 0 } },  // RED_ICE_LARGE     (0.1)
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0x0100, "ice_ice_modelT", 0, { 0 } },  // RED_ICE_SMALL     (0.06)
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0x0200, "ice_ice3_modelT", 0, { 0 } }, // RED_ICE_PLATFORM  (0.1)
    // INERT BY CONSTRUCTION, kept for intent (same class as l_idomizu above, different reason).
    // ice_wall_modelT is a SKINNED CMB (`autostate` reports skin=1 for this slot), so Zelda3D_TryAuto
    // jumps it straight to state 2 and defers to the SkelAnime hook to derive the scale and draw.
    // Bg_Ice_Shelter is a static Bg_ actor with no SkelAnime, so that hook never fires and the N64
    // wall keeps drawing. It reads `state=2 skin=1 submits=0`, which the skin column now separates
    // from the one real-failure signature (state=2 skin=0 submits=0). Making it real needs the auto
    // path to draw a skinned CMB in bind pose for actors that have no skeleton to drive it.
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0x0300, "ice_wall_modelT", 0, { 0 } }, // RED_ICE_WALL      (0.1)
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0x0400, "ice_ice_modelT", 0, { 0 } },  // RED_ICE_KING_ZORA (non-uniform)
    { ACTOR_BG_ICE_TURARA, 0, 0, "ice_turara", 0, { 0 } },
    { ACTOR_BG_ICE_OBJECTS, 0, 0, "ice_brick", 0, { 0 } },
    { ACTOR_BG_ICE_SHUTTER, 0, 0, "ice_wall2", 0, { 0 } },
    // params 0 is the Shadow Temple scythe, which loads OBJECT_HAKA_OBJECTS instead and draws a
    // different DL entirely -- so this must NOT match it. params 1 (Shadow Temple INVISIBLE) does
    // take the ice-objects branch in Init but draws nothing, so routing it is harmless.
    { ACTOR_BG_HAKA_SGAMI, 0x00FF, 0x0002, "ice_trap", 0, { 0 } },
    // Goron City props. FOUR actors, FIVE CMBs, five N64 display lists -- and this is the row where
    // both naming systems agree on every single entry, which is the strongest corroboration this
    // method produces. Each actor names its own DL, and the Japanese in the CMB name says the same
    // thing:
    //   Bg_Spot18_Basket  -> gGoronCityVaseDL       -> obj_s18tubo (tsubo = pot; 972 verts, the big one)
    //   Bg_Spot18_Futa    -> gGoronCityVaseLidDL    -> obj_185     (futa = lid, and the mesh is a flat
    //                                                   713 x 84 x 713 disc authored at y=2041, i.e.
    //                                                   sitting on top of the 2153-tall vase)
    //   Bg_Spot18_Shutter -> gGoronCityDoorDL       -> obj_186     (12 verts, 1226 x 1636 x 165 slab)
    //   Bg_Spot18_Obj     -> sDlists[params & 0xF]:
    //       0 -> gGoronCityStatueDL      -> obj_s18zou  (zou = statue)
    //       1 -> gGoronCityStatueSpearDL -> obj_s18yari (yari = spear)
    // None of the four is ACTORCAT_DOOR, so none is short-circuited by the articulated-door skip.
    //
    // TWO of these need noBaseAnchor, and the tell is the CMB's own minY. The generic anchor applies
    // goff = -AutoModelMinY to stand a prop's base on the actor's ground Y, so it is a NO-OP whenever
    // minY == 0 and only ever moves a model when minY != 0 -- which splits cleanly by sign:
    //   minY < 0  -> centre-origin prop (En_Goroiwa's sphere). The anchor is what stops it sinking.
    //   minY > 0  -> authored ABOVE its actor's origin ON PURPOSE, in the same space as the N64
    //                display list it replaces. Anchoring drags it back down to the origin.
    // obj_185 is the extreme case at minY = 2041, and it is MEASURED rather than reasoned: the lid
    // actor and the vase actor occupy the SAME position, pos=(3,-3,20) for both, so the lid's entire
    // height comes from the display list being authored at the vase's rim. Base-anchoring would drop
    // it 2041 * 0.0937 ~= 191 units, i.e. onto the floor inside the vase's base. obj_s18yari is the
    // same class, smaller: minY = 162 and its whole bbox is off-origin (x[189..361] z[111..1026])
    // because the spear is authored in the STATUE's space, exactly as its N64 DL is.
    { ACTOR_BG_SPOT18_BASKET, 0, 0, "obj_s18tubo", 0, { 0 } },
    { ACTOR_BG_SPOT18_FUTA, 0, 0, "obj_185", /*noBaseAnchor=*/1, { 0 } },
    { ACTOR_BG_SPOT18_SHUTTER, 0, 0, "obj_186", 0, { 0 } },
    { ACTOR_BG_SPOT18_OBJ, 0x000F, 0x0000, "obj_s18zou", 0, { 0 } },
    { ACTOR_BG_SPOT18_OBJ, 0x000F, 0x0001, "obj_s18yari", /*noBaseAnchor=*/1, { 0 } },
    // Bottom of the Well. zelda_hakach_objects.zar holds EIGHT CMBs against EXACTLY EIGHT gBotw*
    // display lists, and all eight map with an independent geometric signature backing each name:
    //   gBotwCoffinLidDL          -> m_Hkhuta            (huta = lid; a 500 x 118 x 1200 slab)
    //   gBotwBombSpotDL           -> m_HkotuBomb00       (name-exact)
    //   gBotwWaterFallDL          -> m_Hwat00_FO_modelT  (FO = fall; the one water mesh WITH height)
    //   gBotwWaterRingDL          -> m_Hwat00_Down_modelT(flat 22000 x 0 x 19600 surface)
    //   gBotwBloodSplatterDL      -> m_Hsec00_modelT     (the only remaining modelT: a flat 6-vert
    //                                                     decal sitting at y=20)
    //   gBotwFakeWallsAndFloorsDL -> m_Hsec00            (3 groups, height 2000 -- walls need it)
    //   gBotwThreeFakeFloorsDL    -> m_Hsec03            (flat, 54 verts: three floor planes)
    //   gBotwHoleTrap2DL          -> m_Hinv05            (bbox lies ENTIRELY BELOW the origin,
    //                                                     y[-2400..0] -- a pit, which is what a hole
    //                                                     trap is)
    //
    // ONLY TWO OF THE EIGHT ARE ROUTED. The mapping is not the blocker; the mechanism is, in three
    // distinct ways, and each is recorded here so the next pass does not re-derive the mapping to
    // discover the same walls:
    //
    // (1) A FORCED SLOT REPLACES THE ACTOR'S WHOLE DRAW. Zelda3D_TryAuto returns 1 and the caller
    //     skips the N64 draw entirely, so an actor that emits MORE THAN ONE display list loses every
    //     list but the one we substitute. That rules out Bg_Haka_Water (gBotwWaterRingDL plus
    //     gBotwWaterFallDL, the latter at its own Matrix_Translate(0,92,-1680) + Scale(0.1)) and
    //     Bg_Haka_Megane params 0, which draws sDLists[0] AND gBotwBloodSplatterDL. Routing either
    //     would delete geometry that currently renders -- a regression, not a partial win.
    // (2) THE ZAR COMES FROM THE ACTOR'S OBJECT BANK SLOT, NOT FROM WHERE ITS GEOMETRY LIVES.
    //     Bg_Haka_Zou's ActorInit declares OBJECT_GAMEPLAY_KEEP and it fetches HAKACH or HAKA at
    //     runtime into a private index, so Zelda3D_ActorObjectId reports GAMEPLAY_KEEP and the lookup
    //     can never reach this ZAR. Its params-2 entry (gBotwBombSpotDL -> m_HkotuBomb00) is
    //     therefore unreachable despite being name-exact.
    // (3) Bg_Haka_Megane params >= 3 uses OBJECT_HAKA_OBJECTS instead, so only params 0/1/2 are
    //     candidates here at all.
    //
    // What is left is params 1 and 2. params 1 (m_Hsec03) is FLAT (y extent 0), so the height measure
    // cannot size it and it depends on the footprint fallback -- the same position l_idomizu is in.
    // Shadow Temple traps. zelda_haka_objects.zar is the third-largest archive in the queue (32 CMBs,
    // 8 actors) and MOST of it is unreachable -- see the multi-DL screen in docs/multi_cmb_zar_risk.md.
    // Bg_Haka_Trap is the one actor there that draws exactly one list, `sDLists[params]`, so it is the
    // only one that can be routed without deleting geometry.
    //
    // ONLY ONE of the five is routed, and the other four are a lesson in trusting the MEASUREMENT over
    // the name. The initial pass picked all three "obvious" ones from Japanese alone; the three-axis
    // check then rejected two of them:
    //
    //   1 SPIKED_BOX      -> m_Hkenzan (kenzan = spike bed, a 2000 x 6121 x 2000 tower). Measured
    //                        0.0938 / 0.100 / 0.100 -- agrees, and it is routed.
    //
    //   4 PROPELLER       -> NOT m_Hsyarin. "syarin" = wheel and it looked perfect, but the measure
    //                        reads 0.0200 / 0.0218 / 0.0033 -- a 6x spread, which is the wrong-mesh
    //                        signature rather than a re-authoring gap. The N64 propeller measures
    //                        53 x 58 x 16, i.e. ~530 x 580 x 160 at scale 0.1, and the ZAR mesh with
    //                        that shape is m_Hfofo (576 x 537 x 193) -- 0.099 / 0.101 / 0.083, which
    //                        agrees. m_Hsyarin's long axis is Z (4828) while the N64 list's SHORTEST
    //                        axis is Z; no amount of scaling reconciles that.
    //
    //   0 GUILLOTINE_SLOW -> m_Hgiro. WITHDRAWN on the pass that identified it, RESTORED 2026-08-04
    //                        once the scale no longer came from height alone. X and Z hit 0.100 and
    //                        0.107 so the identification was never in doubt; what blocked it was that
    //                        height gives 0.0300 (a 13555-tall OoT3D mesh against a 407-tall N64 list)
    //                        and the derived scale was height-primary, so the blade would have
    //                        rendered 3.3x too narrow -- worse than the faithful N64 draw. This was
    //                        the third row where height dissented while two footprint axes agreed
    //                        (after the King Zora block and the Bottom of the Well coffin lid), and it
    //                        is what motivated the AXIS CONSENSUS derive. That mechanism rejects the
    //                        3.3x height outlier and takes the coherent X/Z pair, so the row can ship.
    //
    // params 2 and 3 (SPIKED_WALL, SPIKED_WALL_2) are LEFT UNROUTED ON PURPOSE. Their two candidates,
    // m_HhasamiN and m_HhasamiS (hasami = scissors), are MIRROR IMAGES -- identical 3563 x 1233 x 1014
    // extents with mirrored Z (z[-594..420] vs z[-420..594]) -- which is a lovely confirmation that the
    // pair belongs to the paired closing-walls trap, and useless for deciding WHICH wall is which. A
    // coin flip here renders a mirrored trap. THE DISCRIMINATOR is a live instance: compare a params-2
    // actor's shape.rot.y / world Z against a params-3 one and match the sign to the N/S suffix. That
    // needs an instance in the Shadow Temple, which this pass did not reach.
    //
    // m_Hsyarin gets noBaseAnchor because it is authored CENTRE-origin (y[-1328..1328]) about the hub
    // it spins on, exactly as its N64 list is, and Gfx_DrawDListOpa draws that list straight at the
    // actor matrix with no offset. The generic -minY anchor would shove the propeller 1328 units up.
    // (The other two have minY == 0, where the anchor is a no-op either way.) NOTE this refines the
    // minY-sign rule from the Goron City pass: minY < 0 means centre-origin, but whether to anchor
    // still depends on whether the ACTOR's Y is the prop's base or its hub -- a rolling boulder wants
    // the anchor, a propeller does not.
    { ACTOR_BG_HAKA_TRAP, 0x00FF, 0x0000, "m_Hgiro", 0, { 0 } }, // restored by axis consensus -- see above
    { ACTOR_BG_HAKA_TRAP, 0x00FF, 0x0001, "m_Hkenzan", 0, { 0 } },
    // m_Hfofo is authored centre-origin (y[-222..315]) about the hub it spins on, exactly as its N64
    // list is, and Gfx_DrawDListOpa draws that list straight at the actor matrix with no offset. The
    // generic -minY anchor would shove the propeller up by its own half-height. NOTE this refines the
    // minY-sign rule from the Goron City pass: minY < 0 means centre-origin, but whether to anchor
    // still depends on whether the ACTOR's Y is the prop's base or its hub -- a rolling boulder wants
    // the anchor, a propeller does not.
    { ACTOR_BG_HAKA_TRAP, 0x00FF, 0x0004, "m_Hfofo", /*noBaseAnchor=*/1, { 0 } },
    { ACTOR_BG_HAKA_HUTA, 0, 0, "m_Hkhuta", 0, { 0 } },
    { ACTOR_BG_HAKA_MEGANE, 0x00FF, 0x0001, "m_Hsec03", 0, { 0 } },
    { ACTOR_BG_HAKA_MEGANE, 0x00FF, 0x0002, "m_Hinv05", 0, { 0 } },

    // --- OBJECT_HIDAN_OBJECTS (Fire Temple) -- the largest row in the queue: 31 CMBs, and 13 actors
    // that ACTUALLY declare it (a grep says 16; Door_Shutter, Door_Killer and En_Door only reach it
    // through a per-scene secondary-object table and declare GAMEPLAY_KEEP / DOOR_KILLER themselves).
    //
    // MULTI-DL SCREEN FIRST, per the rule Pass 25 added. Four of the thirteen draw more than one list
    // and are therefore NOT routable to a single mesh at any params -- routing them would DELETE
    // geometry exactly as Obj_Syokudai would have deleted its flame:
    //   Bg_Hidan_Rock      1 or 2  (base block + a conditional vertical flame, gated on unk_16C)
    //   Bg_Hidan_Sima      1 to 5  (platform + a fireball burst loop)
    //   Bg_Hidan_Sekizou   1 to 9  (statue + per-direction flame timers)
    //   Bg_Hidan_Rsekizou  9 ALWAYS (spinning flamethrower + 8 fireballs)
    //
    // TWO MORE are excluded for a reason the screen does not catch, and both are the zelda_bombf
    // failure mode -- an actor whose measured extents are not a constant:
    //   Bg_Hidan_Firewall  the mesh is certain (m_Ffirewall_modelT is the only firewall mesh and the
    //                      actor draws one list) but its scale is NON-UNIFORM AND ANIMATED -- x=z=0.12
    //                      fixed while y sweeps 0.01..0.1 every cycle. A single worldScale cannot
    //                      express it, and the bbox measure would sample whichever y the animation
    //                      happened to be at, so the derived scale would be luck of the frame. Same
    //                      class as King Zora's ice: it needs a per-axis draw path, not a routing.
    //   Bg_Hidan_Kousi 1,2 m_Fkousi2 and m_Fkousi3 have IDENTICAL extents (1597.4 x 1199.2 x 44.6), so
    //                      the three-axis test cannot tell them apart -- the m_HhasamiN/S situation.
    //                      Routed anyway (see below) because unlike the mirrored spike walls these two
    //                      are same-size gratings, so a swap is at worst a mirrored fence, whereas
    //                      leaving them unrouted hands them AUTO's largest-CMB pick, which in THIS
    //                      archive is m_FOtiBFhead -- a 26154-unit pillar. Wrong grating beats a tower.
    //
    // Params mappings are read from each actor's Draw, NOT guessed. Note two traps: Bg_Hidan_Dalm's
    // Init does `params &= 0xFF` after lifting the switch flag out of the high byte, and
    // Bg_Hidan_Hrock's Init does `params = (params >> 8) & 0xFF`, so at DRAW time (which is when a
    // forced slot is matched) Hrock's params IS the dlists[] index. Both were verified in source.
    { ACTOR_BG_HIDAN_FWBIG, 0, 0, "m_FfirewallBIG", 0, { 0 } }, // one DL always, one candidate mesh
    { ACTOR_BG_HIDAN_FSLIFT, 0, 0, "m_Fhocklift", 0, { 0 } },   // "hocklift" = hookshot elevator
    // dlists[0] is gFireTempleTallestPillarAboveRoomBeforeBossDL and m_FOtiBFhead is 26154 units tall
    // -- by far the tallest mesh in the archive, which is the identification. 1 and 2 select the SAME
    // N64 DL (PillarInsertedInGround), so they share the mesh too.
    { ACTOR_BG_HIDAN_HROCK, 0x00FF, 0x0000, "m_FOtiBFhead", 0, { 0 } },
    { ACTOR_BG_HIDAN_HROCK, 0x00FF, 0x0001, "m_FOtiMINI", 0, { 0 } },
    { ACTOR_BG_HIDAN_HROCK, 0x00FF, 0x0002, "m_FOtiMINI", 0, { 0 } },
    // params 0 = CRACKED_STONE_FLOOR, and m_Fbmfl ("bomb floor") is the only HORIZONTAL slab in the
    // set (1601 x 72 x 1700); every other bm* mesh is a zero-thickness vertical wall.
    { ACTOR_BG_HIDAN_KOWARERUKABE, 0x00FF, 0x0000, "m_Fbmfl_model", 0, { 0 } },
    // params 1 = BOMBABLE_WALL. m_Fbmwall2 was the first guess (largest plausible wall) and the
    // measurement REJECTED it: the slot measures an N64 draw 100 tall x 60 wide, which against
    // m_Fbmwall2's 1600 x 1400 gives 0.0375 / 0.0714 -- neither near the actor's 0.1. The mesh that
    // IS 0.1 on both is m_Fbmwall1 (600 x 1000). Corrected by arithmetic, then re-measured.
    { ACTOR_BG_HIDAN_KOWARERUKABE, 0x00FF, 0x0001, "m_Fbmwall1", 0, { 0 } },
    // params 2 = LARGE_BOMBABLE_WALL is WITHDRAWN, not shipped on a guess. Its slot measures 120 tall
    // x 160 wide, and NO mesh in the archive is 1200 x 1600: m_Fbmwall2 (1600 x 1400) matches only on
    // X, m_Fbmwall3 (990 x 1200) only on height. The likely reason it cannot be pinned this way is a
    // real limitation of the measure rather than a missing mesh -- measFootX/measFootZ are WORLD-space
    // while the CMB extents are LOCAL, so for a rotated flat wall the two do not correspond axis to
    // axis at all. Every prop identified so far has been effectively axis-aligned, which is why this
    // has not bitten before. Fixing it means folding shape.rot.y into the footprint comparison; until
    // then a vertical wall's footprint is not evidence.
    // { ACTOR_BG_HIDAN_KOWARERUKABE, 0x00FF, 0x0002, "m_Fbmwall4", 0, {0} },  // withdrawn -- see above
    { ACTOR_BG_HIDAN_KOUSI, 0x00FF, 0x0000, "m_Fkousi1", 0, { 0 } },
    { ACTOR_BG_HIDAN_KOUSI, 0x00FF, 0x0001, "m_Fkousi2", 0, { 0 } },
    { ACTOR_BG_HIDAN_KOUSI, 0x00FF, 0x0002, "m_Fkousi3", 0, { 0 } },
    // params 0 -> body, ANY nonzero -> head, so the catch-all MUST follow the 0 row (first match wins).
    { ACTOR_BG_HIDAN_DALM, 0x00FF, 0x0000, "m_Fdalm_model", 0, { 0 } },
    { ACTOR_BG_HIDAN_DALM, 0, 0, "m_FdalmHEAD", 0, { 0 } },
    { ACTOR_BG_HIDAN_HAMSTEP, 0x00FF, 0x0000, "m_FhamSTEP_model", 0, { 0 } },
    { ACTOR_BG_HIDAN_HAMSTEP, 0, 0, "m_FhamSTEP_1", 0, { 0 } },
    // Bg_Hidan_Syoku draws gFireTempleFlareDancerPlatformDL and takes no params. The Flare Dancer is
    // the Fire Temple MINI-BOSS, and m_Fmboss ("mboss") is the only mesh named for one. That is a name
    // argument, which this arc has seen lose twice, so it is provisional until the ratios confirm it.
    { ACTOR_BG_HIDAN_SYOKU, 0, 0, "m_Fmboss", 0, { 0 } },
};

int Zelda3D_ForcedSlotCount(void) {
    return (int)ARRAY_COUNT(sActorForcedAuto);
}
const Zelda3D_AutoEntry* Zelda3D_ForcedSlotInfo(int i, short* outActorId, const char** outCmbSubstr) {
    if (i < 0 || i >= (int)ARRAY_COUNT(sActorForcedAuto))
        return NULL;
    if (outActorId != NULL)
        *outActorId = sActorForcedAuto[i].actorId;
    if (outCmbSubstr != NULL)
        *outCmbSubstr = sActorForcedAuto[i].cmbSubstr;
    return &sActorForcedAuto[i].entry;
}

Zelda3D_ActorForcedAutoSlot* Zelda3D_FindActorForcedSlot(s16 actorId, u16 params) {
    for (size_t i = 0; i < ARRAY_COUNT(sActorForcedAuto); i++) {
        Zelda3D_ActorForcedAutoSlot* s = &sActorForcedAuto[i];
        if (s->actorId != actorId)
            continue;
        if (s->paramMask == 0 || (params & s->paramMask) == s->paramValue)
            return s;
    }
    return NULL;
}

Zelda3D_ActorForcedAutoSlot* Zelda3D_ForcedSlotAt(int index) {
    if (index < 0 || index >= Zelda3D_ForcedSlotCount()) {
        return NULL;
    }
    return &sActorForcedAuto[index];
}

int Zelda3D_ForcedSlotIndex(const Zelda3D_ActorForcedAutoSlot* slot) {
    if (slot < sActorForcedAuto || slot >= sActorForcedAuto + ARRAY_COUNT(sActorForcedAuto)) {
        return -1;
    }
    return (int)(slot - sActorForcedAuto);
}

int Zelda3D_ExplicitReplacementCount(void) {
    return (int)ARRAY_COUNT(sModelTable);
}

Zelda3D_ModelEntry* Zelda3D_ExplicitReplacementAt(int index) {
    if (index < 0 || index >= Zelda3D_ExplicitReplacementCount()) {
        return NULL;
    }
    return &sModelTable[index];
}
