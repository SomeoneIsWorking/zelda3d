// See link_midmask.h. Adult mesh-id policy ported verbatim from OoT's boyMidMask —
// the only substitution is the input type (LinkGear instead of Player*). Any deviation
// from the OoT behavior here is a regression: this file's rules are what the mesh_id
// identification sweep already validated against link_v2.cmb.
#include "link_midmask.h"
#include "link_dl_table.h"

namespace Zelda3D {

#define LINK_MID(n) (1ull << (n))

// Gear enum -> PLAYER_MODELTYPE_*. Pure naming: no mesh ids live here, so a wrong value cannot be
// introduced by editing this function -- only a wrong model type, which is far easier to eyeball.
static LinkModelType leftHandModelType(LinkHandLeft h) {
    switch (h) {
        case LinkHandLeft::Closed:       return LinkModelType::LhClosed;
        case LinkHandLeft::SwordOneHand: return LinkModelType::LhSword;
        case LinkHandLeft::SwordTwoHand: return LinkModelType::LhBgs;
        case LinkHandLeft::Bottle:       return LinkModelType::LhBottle;
        case LinkHandLeft::Hammer:       return LinkModelType::LhHammer;
        case LinkHandLeft::Boomerang:    return LinkModelType::LhBoomerang;
        case LinkHandLeft::Open:
        default:                         return LinkModelType::LhOpen;
    }
}

static LinkModelType rightHandModelType(LinkHandRight h) {
    switch (h) {
        case LinkHandRight::Closed:   return LinkModelType::RhClosed;
        case LinkHandRight::Bow:      return LinkModelType::RhBowSlingshot;
        case LinkHandRight::Hookshot: return LinkModelType::RhHookshot;
        case LinkHandRight::Ocarina:  return LinkModelType::RhOcarina;
        case LinkHandRight::Open:
        default:                      return LinkModelType::RhOpen;
    }
}

unsigned long long linkAdultMidMask(const LinkGear& gear) {
    unsigned long long m = LINK_MID(45) | LINK_MID(46); // full body + head/face always
    bool hylian = (gear.shield == LinkShield::Hylian);
    bool mirror = (gear.shield == LinkShield::Mirror);
    bool haveShield = hylian || mirror;

    // LEFT and RIGHT hand mesh ids now come from the PORTED OoT3D table (sPlayerDLists, see
    // link_dl_table.h) instead of hand-written cases. Every hand divergence the audit found was a
    // guessed constant here: the hammer and the bottle both fell through to the plain fist (14), so
    // the Megaton Hammer was entirely absent from the game and the bottle was gripped by a closed
    // fist rather than the cupped hand the bottle model is posed against; the hookshot and the
    // ocarina both fell through to the open palm (20), so the hookshot chain emerged from nothing
    // and the Ocarina of Time was played with an open hand; PLAYER_MODELTYPE_RH_OOT was not
    // represented at all; and the bow used 29's FIRST-person arm mesh 30 in third person.
    // The mapping below only translates our gear enums to model types -- the VALUES are the ROM's.
    m |= LINK_MID(linkDlMesh(leftHandModelType(gear.leftHand), LinkAge::Adult));

    // RIGHT hand (shield / bow hand), bones 19/20. The shield case is NOT a plain table row: it is
    // the base of a 12-row shield-variant run, and its per-shield selection below was derived by
    // hand and then independently CONFIRMED by that run, so it stays as it is.
    switch (gear.rightHand) {
        case LinkHandRight::Shield:
            // Mirror shield has its OWN forearm mesh (39); 23 is the HYLIAN one. This used to be
            // `haveShield ? 23 : 20` for both, so an adult raising the Mirror Shield displayed a
            // Hylian shield and the Mirror Shield never appeared on the arm anywhere in the game.
            // Verified from the CMB: link_v2's Hylian shield texture p_tex02 (tex 37) is used by
            // mesh ids 0,1,10,11,23 and the Mirror texture p_tex03 (tex 31) by 2,3,7,8,39. Isolating
            // them in game (`linkmid only <n>`) puts 23 and 39 at the SAME forearm position and
            // orientation, while 7/8 are back-worn — so 39 is 23's Mirror counterpart. The back rows
            // below already distinguished the two shields (0/1 vs 2/3); only the forearm did not.
            if (mirror) {
                m |= LINK_MID(39);
            } else {
                m |= haveShield ? LINK_MID(23) : LINK_MID(20);
            }
            break;
        default:
            m |= LINK_MID(linkDlMesh(rightHandModelType(gear.rightHand), LinkAge::Adult));
            break;
    }

    // BACK (shield panel + sheath), bone 21. Combine sheath state with equipped shield.
    switch (gear.sheath) {
        case LinkSheath::ShieldOnBackSwordSheathed:
            m |= hylian ? LINK_MID(0) : (mirror ? LINK_MID(2) : LINK_MID(31));
            break;
        case LinkSheath::ShieldOnBackSwordDrawn:
            // No shield equipped still draws the EMPTY SHEATH (mid 42), it is not nothing.
            // sSheathWithoutSwordDLs @0x0053c4d8, 8-byte stride, (adult,child) as s16 at +0/+4,
            // read straight out of code.bin: NONE=(42,21) DEKU=(42,12) HYLIAN=(1,10) MIRROR=(3,21).
            m |= hylian ? LINK_MID(1) : (mirror ? LINK_MID(3) : LINK_MID(42));
            break;
        case LinkSheath::SwordOnBackNoShield:
            m |= LINK_MID(31);
            break;
        case LinkSheath::EmptySheathNoShield:
            // SHEATH_17 is NOT "draws nothing": sSheathDLs @0x0053c5e8 = (adult 42, child 21).
            // Adult 42 is the empty sheath strap -- confirmed visually by isolating it in game
            // (`linkmid only 42`), which renders the slim diagonal strap on Link's back, alongside
            // mid 1 (Hylian shield + sheath) and mid 3 (Mirror shield + sheath).
            // So with the sword drawn and no shield, Link's sheath used to vanish off his back.
            m |= LINK_MID(42);
            break;
        default:
            break;
    }

    // GAUNTLET PLATES, forearms. Silver/gold gauntlets add plate geometry that the Goron bracelet
    // (upgrade 1) does not, so the gate is `>= 2` — the plates are their own meshes, and before this
    // existed they were simply never drawn (debug_journal/2026-07-29-adult-gauntlet-plates-never-drawn.md).
    //
    // Ported from OoT3D `Player_DrawImpl` (0x004c11f4), which corresponds line-for-line with N64
    // `z_player_lib.c:1114-1141`: plate 1 on both arms unconditionally, then an open/closed variant
    // per hand. The 3DS selects the right-hand variant with `cfg[0x40] == 8`, and
    // `PLAYER_MODELTYPE_RH_OPEN` IS 8 — the same `(hand == OPEN) ? plate2 : plate3` test N64 writes.
    //
    // Deliberately NOT copied from OoT3D's always-on set {45, 46, 47}: we start from {45, 46}.
    //
    // The "47 is plausibly the far-LOD body, which we would double-draw" guess that used to sit here
    // was WRONG, and it is worth saying so rather than deleting it, because it is the sort of
    // plausible rationale that gets reused. Mid 47 is a co-located low-poly OVERLAY of 46, not a
    // separate LOD: comparing posed vertex sets, 47 contributes only TWO positions not already in 46
    // (six of its eight unique points coincide exactly). The child pair is the same shape -- mid 25
    // shares 80 of its 97 unique points with 24. Drawing either would z-fight its partner rather than
    // add any silhouette.
    //
    // So omitting 47 is correct, but NOT for the stated reason, and the real reason also means there
    // is nothing to gain from an identification pass here. Do not spend a session "fixing" this.
    // (Same applies to the `25 = far-LOD` note on the child body line in zelda3d_link.cpp.)
    if (gear.strengthUpgrade >= 2) {
        m |= LINK_MID(4) | LINK_MID(17);                                        // plate 1, both arms
        m |= (gear.leftHand  == LinkHandLeft::Open)  ? LINK_MID(5)  : LINK_MID(6);
        m |= (gear.rightHand == LinkHandRight::Open) ? LINK_MID(18) : LINK_MID(19);
    }

    // BOOTS. Iron and hover each add a PAIR of meshes; normal boots add none, which is why N64 guards
    // the whole thing on `boots != 0` and indexes `sBootDListGroups[boots - 1]`. The 3DS reads the
    // same two entries per row from the table at 0x0053c74c (base + boots*8, read at -8 and -4), and
    // that table has exactly two valid rows — iron and hover — matching N64's array length.
    // Like the gauntlets, these meshes were previously never enabled at all.
    if (gear.boots == 1) {
        m |= LINK_MID(35) | LINK_MID(36); // iron
    } else if (gear.boots == 2) {
        m |= LINK_MID(15) | LINK_MID(22); // hover
    }
    return m;
}

} // namespace Zelda3D
