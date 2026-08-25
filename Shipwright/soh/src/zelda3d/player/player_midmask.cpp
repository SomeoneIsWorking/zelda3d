#include "player_draw_policy.h"
#include "player_midmask.h"

#include "player/link_midmask.h"

// #85 carry-WALK upper-body bone mask for the two-source per-limb blend (Zelda3D_UpdateAnimTwoSource).
// The OoT3D analogue of N64 sUpperBodyLimbCopyMap (z_player.c:397): the N64 marks limbs UPPER..TORSO
// (PLAYER_LIMB 10-21) as upper-body. Mapping those to the shared 25-bone link rig via the verified
// bone<->limb correspondence (zelda3d_link_bonecorr.inc / oot3d-decomp/docs/link_bone_map.md): N64
// UPPER->b9, the extra OoT3D chest/collar bones b10/b13/b17 (TORSO/COLLAR region, no own N64 limb),
// HEAD->b11, HAT->b12, L arm->b14/15/16, R arm->b18/19/20, SHEATH->b21. So bones 9..21 are upper;
// 0..8 (root/waist/lower-pivot/legs) and 22..24 (aux root2) stay on the lower locomotion clip. Same
// partition for child (childlink_v2) and adult (link_v2) — they share the rig.
const unsigned char kLinkUpperBodyMask[25] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,             // b0..b8  lower: root, waist, lower-pivot, R/L legs
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // b9..b21 upper: spine, chest, head, hat, both arms, sheath
    0, 0, 0                                // b22..b24 aux root2 -> lower
};

// Compute which childlink_v2 mesh_ids are visible this frame from Link's live state. The CMB bakes
// EVERY hand-pose + held-equipment variant on its own mesh_id; the game (N64 and OoT3D) shows a
// state-dependent subset. N64 already resolves the selection per limb each frame into
// player->leftHandType / rightHandType / sheathType (PlayerModelType) + currentShield; we translate
// those to the matching childlink_v2 mesh_ids. The N64 state is self-consistent (sword drawn =>
// empty sheath on back + sword in left hand + shield on right arm; stowed => open hands + shield +
// sword on back), so just composing per-limb avoids double shields/swords. mesh_id map: see
// link_mesh_id_map.md (next to this file) (texture + posed-geometry + render-sweep identification).
#define LINK_MID(n) (1ull << (n))

// ADULT/boy link_v2.cmb mesh_id map. Same 25-bone rig as child (LH=bones15/16, RH=19/20,
// back/sheath=21, waist=23/24) but a DIFFERENT mesh_id layout — identified the same way as child
// (texture dump + posed-geometry + in-game `linkmid only <n>` render sweep, see link_mesh_id_map.md
// "## ADULT"). Boy's shield set is Hylian (default) / Mirror, vs child's Deku / Hylian.
//
// Stage 2a of the MM/OoT unification (scratch/plans/mm_oot_link_unify.md): the actual mesh_id policy
// now lives in Shipwright/zelda3d_shared/player/link_midmask.cpp and consumes a game-agnostic
// LinkGear POD. This function is the OoT-side ADAPTER — translates the live OoT Player fields into
// LinkGear, hands off to the shared computation, then returns. Zero behavior change (the shared
// code is a verbatim port); MM will add its own translator in Stage 2b when MmPlayerBehavior lands.
unsigned long long Zelda3D::LinkMidMask::boyMidMask(Player* player) const {
    Zelda3D::LinkGear g;

    switch (player->leftHandType) {
        case PLAYER_MODELTYPE_LH_SWORD:
        case PLAYER_MODELTYPE_LH_SWORD_2:
            g.leftHand = LinkHandLeft::SwordOneHand;
            break;
        case PLAYER_MODELTYPE_LH_BGS:
            g.leftHand = LinkHandLeft::SwordTwoHand;
            break;
        case PLAYER_MODELTYPE_LH_BOTTLE:
            g.leftHand = LinkHandLeft::Bottle;
            break;
        case PLAYER_MODELTYPE_LH_HAMMER:
            g.leftHand = LinkHandLeft::Hammer;
            break;
        case PLAYER_MODELTYPE_LH_CLOSED:
            g.leftHand = LinkHandLeft::Closed;
            break;
        case PLAYER_MODELTYPE_LH_OPEN:
        default:
            g.leftHand = LinkHandLeft::Open;
            break;
    }
    switch (player->rightHandType) {
        case PLAYER_MODELTYPE_RH_SHIELD:
            g.rightHand = LinkHandRight::Shield;
            break;
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2:
            g.rightHand = LinkHandRight::Bow;
            break;
        case PLAYER_MODELTYPE_RH_CLOSED:
            g.rightHand = LinkHandRight::Closed;
            break;
        case PLAYER_MODELTYPE_RH_HOOKSHOT:
            g.rightHand = LinkHandRight::Hookshot;
            break;
        case PLAYER_MODELTYPE_RH_OCARINA:
            g.rightHand = LinkHandRight::Ocarina;
            break;
        case PLAYER_MODELTYPE_RH_OPEN:
        default:
            g.rightHand = LinkHandRight::Open;
            break;
    }
    switch (player->sheathType) {
        case PLAYER_MODELTYPE_SHEATH_18:
            g.sheath = LinkSheath::ShieldOnBackSwordSheathed;
            break;
        case PLAYER_MODELTYPE_SHEATH_19:
            g.sheath = LinkSheath::ShieldOnBackSwordDrawn;
            break;
        case PLAYER_MODELTYPE_SHEATH_16:
            g.sheath = LinkSheath::SwordOnBackNoShield;
            break;
        case PLAYER_MODELTYPE_SHEATH_17:
        default:
            g.sheath = LinkSheath::EmptySheathNoShield;
            break;
    }
    switch (player->currentShield) {
        case PLAYER_SHIELD_HYLIAN:
            g.shield = LinkShield::Hylian;
            break;
        case PLAYER_SHIELD_MIRROR:
            g.shield = LinkShield::Mirror;
            break;
        default:
            g.shield = LinkShield::None;
            break;
    }
    // Gauntlet plates are gated on the strength upgrade (>= 2 = silver/gold). Reading it here rather
    // than inside the shared policy keeps that file free of gSaveContext, the same way every other
    // field is translated from Player-native state.
    g.strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);
    g.boots = player->currentBoots; // iron/hover add their own mesh pair
    return Zelda3D::linkAdultMidMask(g);
}

unsigned long long Zelda3D::LinkMidMask::compute(Player* player) const {
    unsigned long long m;
    int deku, hylian;
    if (overrideSet) {
        return overrideMask; // REPL `linkmid` debug override (identification sweep)
    }
    if (LINK_AGE_IN_YEARS != YEARS_CHILD) {
        return boyMidMask(player); // adult uses link_v2.cmb's own mesh_id layout
    }
    // body + head/face always. 25 is omitted, but it is NOT a far-LOD as this line used to claim:
    // it is a co-located low-poly overlay of 24, sharing 80 of its 97 unique posed vertex positions,
    // so drawing it would z-fight rather than add silhouette. See the matching note in
    // zelda3d_shared/player/link_midmask.cpp for the adult 46/47 pair.
    m = LINK_MID(24) | LINK_MID(26);
    deku = (player->currentShield == PLAYER_SHIELD_DEKU);
    hylian = (player->currentShield == PLAYER_SHIELD_HYLIAN || player->currentShield == PLAYER_SHIELD_MIRROR);

    // LEFT hand (the sword hand). childlink left-hand variants on bones 15/16.
    // Child mesh ids from the PORTED OoT3D table (sPlayerDLists child column, link_dl_table.h,
    // claim C027). These four cases were hand-written and three were wrong:
    //   LH_SWORD  16 -> 2 : 16 is the row for the child MASTER sword (LH_BGS). Drawing it for the
    //                       ordinary Kokiri sword is why child Link swung a blade nearly as long as
    //                       he is tall. The audit reported this and two refuters could not confirm
    //                       it, so it was recorded DISPUTED -- the ROM table settles it: LhSword's
    //                       child value is 2 and LhBgs's is 16, they are different meshes.
    //   LH_BOOMERANG 8 -> 6 : 8 is not the boomerang row at all.
    //   LH_BOTTLE    1 -> 7 : fell through to the plain closed fist.
    // LH_OPEN 0 and LH_HAMMER 0 were already right (OoT3D really does give the child hammer an
    // empty hand -- LhHammer's child value IS 0), so they are unchanged.
    switch (player->leftHandType) {
        case PLAYER_MODELTYPE_LH_SWORD:
        case PLAYER_MODELTYPE_LH_SWORD_2:
            m |= LINK_MID(2);
            break; // Kokiri sword in hand
        case PLAYER_MODELTYPE_LH_BGS:
            m |= LINK_MID(16);
            break; // child Master Sword
        case PLAYER_MODELTYPE_LH_BOOMERANG:
            m |= LINK_MID(6);
            break; // boomerang
        case PLAYER_MODELTYPE_LH_BOTTLE:
            m |= LINK_MID(7);
            break; // cupped hand for the bottle
        case PLAYER_MODELTYPE_LH_CLOSED:
            m |= LINK_MID(1);
            break; // closed fist
        case PLAYER_MODELTYPE_LH_OPEN:
        case PLAYER_MODELTYPE_LH_HAMMER: /* child hammer = empty, per the table */
        default:
            m |= LINK_MID(0);
            break; // open empty hand
    }

    // RIGHT hand (the shield hand). childlink right-hand variants on bones 19/20.
    switch (player->rightHandType) {
        case PLAYER_MODELTYPE_RH_SHIELD:
            // RhShield is the base of the 12-row shield-variant run; its first group's child column
            // is NONE=4 DEKU=5 HYLIAN=4 MIRROR=4. So ONLY the Deku shield gets its own arm mesh on
            // the child, and every other case -- including no shield at all -- is the plain fist 4.
            // This was `(deku || hylian) ? 5 : 3`, which strapped a DEKU shield to the child's arm
            // whenever he carried a Hylian or Mirror shield, and showed an OPEN PALM (3) rather than
            // a fist when he carried none.
            m |= deku ? LINK_MID(5) : LINK_MID(4);
            break;
        // mid 18 is the OCARINA, not the slingshot. Verified by isolating it in game
        // (`linkmid only 18` on child Link): it renders a hand holding a blue instrument with
        // finger holes. The mesh map labels it "SLINGSHOT, p_tex04", which is what this policy was
        // written against -- and mid 18 is why RH_OCARINA previously drew an OPEN HAND while the
        // real ocarina mesh sat unused.
        //
        // The slingshot's own mesh is NOT yet identified and is deliberately left pointing at 18:
        // mid 19 (the obvious neighbour) renders a straight brown shaft with red ends, not a forked
        // slingshot frame, so remapping to it would swap one wrong item for another. Drawing the
        // ocarina for the slingshot is the PRE-EXISTING behaviour, not a new regression -- see
        // debug_journal/2026-07-30-audit-round2b-player-animation-scene.md.
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2:
            m |= LINK_MID(19);
            break; // Fairy Slingshot
        // The table resolves the ocarina/slingshot tangle above, and CONFIRMS the earlier visual
        // finding rather than contradicting it: RhOot's child value is 18 and mid 18 was isolated in
        // game as "a hand holding a BLUE instrument with finger holes" -- the Ocarina of Time is the
        // blue one. So 18 was always an ocarina, just the wrong ocarina to use for everything.
        // RhOcarina's child value is 17 (the Fairy Ocarina) and RhBowSlingshot's is 19.
        case PLAYER_MODELTYPE_RH_OCARINA:
            m |= LINK_MID(17);
            break; // Fairy Ocarina
        case PLAYER_MODELTYPE_RH_OOT:
            m |= LINK_MID(18);
            break; // Ocarina of Time
        case PLAYER_MODELTYPE_RH_CLOSED:
            m |= LINK_MID(4);
            break; // closed
        case PLAYER_MODELTYPE_RH_OPEN:
        case PLAYER_MODELTYPE_RH_HOOKSHOT: /* child hookshot = empty */
        default:
            m |= LINK_MID(3);
            break; // open empty hand
    }

    // BACK (sheath + back shield), bone 21. Combine sheathType with currentShield. The sword-on-back
    // hilt (mid 11/14) and shield panel are baked together per combination, so this is one choice.
    //
    // #201 e — THE SWORD-NOT-YET-OWNED RULE. `sheathType` comes from the model GROUP, which is
    // derived from what Link is holding and knows nothing about whether he owns a sword at all. Both
    // N64 and OoT3D therefore apply a second, draw-time override that suppresses the back-worn sword
    // when the child has no Kokiri sword on B — and without it a swordless child wears a sword he has
    // never picked up. Ported from OoT3D `0x004c70c4` (oot3d-decomp/docs/player_draw_impl_located.md),
    // whose fallback table at `0x0053c4b8` is byte-for-byte N64's two rows commented
    // "(child, no sword)" in `sSheathWithSwordDLs` (z_player_lib.c:212-223) — `-1` there is the
    // draw-nothing sentinel, and its Deku row's mesh id 13 is exactly our LINK_MID(13).
    //
    // N64 (z_player_lib.c:1420-1441) is TWO rules, not one:
    //   * SHEATH_18/19 (shield on back): `dLists += currentShield * 4`, then if the child has no
    //     Kokiri sword AND `currentShield < PLAYER_SHIELD_HYLIAN`, `dLists += PLAYER_SHIELD_MAX * 4`
    //     — landing on the (child, no sword) row for NONE or DEKU. Hylian/Mirror are excluded, so a
    //     child carrying one of those keeps his normal back geometry.
    //   * SHEATH_16/17 (no shield on back): the base is replaced outright by the all-NULL row, i.e.
    //     nothing on the back.
    const bool noKokiriSword = (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI);
    switch (player->sheathType) {
        case PLAYER_MODELTYPE_SHEATH_18: // sword sheathed AND shield on back
            if (noKokiriSword && player->currentShield < PLAYER_SHIELD_HYLIAN) {
                m |= deku ? LINK_MID(13) : 0ull; // (child, no sword) row: Deku shield alone, else bare
            } else {
                m |= deku ? LINK_MID(11) : (hylian ? LINK_MID(9) : LINK_MID(14));
            }
            break;
        case PLAYER_MODELTYPE_SHEATH_19: // shield on back, empty sheath (sword drawn)
            if (noKokiriSword && player->currentShield < PLAYER_SHIELD_HYLIAN) {
                m |= deku ? LINK_MID(13) : 0ull;
            } else {
                m |= deku ? LINK_MID(13) : (hylian ? LINK_MID(10) : 0ull);
            }
            break;
        case PLAYER_MODELTYPE_SHEATH_16: // sword on back, no shield
            m |= noKokiriSword ? 0ull : LINK_MID(14);
            break;
        case PLAYER_MODELTYPE_SHEATH_17: // empty sheath, no shield (sword drawn, no shield)
        default:
            break; // nothing on the back
    }

    // GORON BRACELET, the child's counterpart of the adult gauntlet plates and previously in the same
    // state: mesh 15 existed in the CMB and was never enabled, so a child with the bracelet showed
    // nothing. OoT3D Player_DrawImpl's child arm is `else if (strengthUpgrade != 0) show(0xf)`, and
    // N64 is `if (Player_GetStrength() > PLAYER_STR_NONE) gSPDisplayList(gLinkChildGoronBraceletDL)`
    // — so the gate is >= 1 here, NOT the >= 2 the adult plates use (the bracelet IS upgrade 1).
    if (CUR_UPG_VALUE(UPG_STRENGTH) > 0) {
        m |= LINK_MID(15);
    }
    return m;
}
