// See link_midmask.h. Adult mesh-id policy ported verbatim from OoT's boyMidMask —
// the only substitution is the input type (LinkGear instead of Player*). Any deviation
// from the OoT behavior here is a regression: this file's rules are what the mesh_id
// identification sweep already validated against link_v2.cmb.
#include "link_midmask.h"

namespace Zelda3D {

#define LINK_MID(n) (1ull << (n))

unsigned long long linkAdultMidMask(const LinkGear& gear) {
    unsigned long long m = LINK_MID(45) | LINK_MID(46); // full body + head/face always
    bool hylian = (gear.shield == LinkShield::Hylian);
    bool mirror = (gear.shield == LinkShield::Mirror);
    bool haveShield = hylian || mirror;

    // LEFT hand (sword hand), bones 15/16.
    switch (gear.leftHand) {
        case LinkHandLeft::SwordOneHand: m |= LINK_MID(16); break; // Master sword in hand
        case LinkHandLeft::SwordTwoHand: m |= LINK_MID(37); break; // Biggoron / giant knife blade
        case LinkHandLeft::Closed:
        case LinkHandLeft::Bottle:
        case LinkHandLeft::Hammer:       m |= LINK_MID(14); break; // closed fist
        case LinkHandLeft::Open:
        case LinkHandLeft::Boomerang:
        default:                         m |= LINK_MID(13); break; // open empty hand (idle)
    }

    // RIGHT hand (shield / bow hand), bones 19/20.
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
        case LinkHandRight::Bow:      m |= LINK_MID(30); break; // bow drawn
        case LinkHandRight::Closed:   m |= LINK_MID(21); break; // closed fist
        case LinkHandRight::Open:
        case LinkHandRight::Hookshot: // adult hookshot model drawn separately -> empty hand
        case LinkHandRight::Ocarina:
        default:                      m |= LINK_MID(20); break; // open empty hand
    }

    // BACK (shield panel + sheath), bone 21. Combine sheath state with equipped shield.
    switch (gear.sheath) {
        case LinkSheath::ShieldOnBackSwordSheathed:
            m |= hylian ? LINK_MID(0) : (mirror ? LINK_MID(2) : LINK_MID(31));
            break;
        case LinkSheath::ShieldOnBackSwordDrawn:
            m |= hylian ? LINK_MID(1) : (mirror ? LINK_MID(3) : 0ull);
            break;
        case LinkSheath::SwordOnBackNoShield:
            m |= LINK_MID(31);
            break;
        case LinkSheath::EmptySheathNoShield:
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
    // Deliberately NOT copied from OoT3D's always-on set {45, 46, 47}: we start from {45, 46} and 47
    // is unaccounted for — plausibly the far-LOD body, which we would double-draw since we always
    // render near LOD. Adding it needs its own identification pass.
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
