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
            m |= haveShield ? LINK_MID(23) : LINK_MID(20); // shield on forearm
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
    return m;
}

} // namespace Zelda3D
