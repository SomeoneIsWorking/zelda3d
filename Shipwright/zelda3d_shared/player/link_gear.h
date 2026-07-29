// LinkGear — game-agnostic snapshot of Link's per-frame equipment / hand-pose state.
//
// Each game (OoT-soh, MM-2s2h) resolves its own Player struct into this POD (see the
// game-side adapter, e.g. Shipwright/soh/src/zelda3d/link_gear_adapter.h). The shared
// mesh-mask + retarget policy consumes ONLY this — never the raw Player — so the
// policy compiles cleanly on both sides without pulling divergent decomp headers into
// the shared library.
//
// Design notes:
//  - Enums are FLATTENED and named after the concept, not the game (SwordOneHand, not
//    OoT's LH_SWORD or MM's LH_ONE_HAND_SWORD). Each game maps in its own translation.
//  - Kept intentionally small (a handful of enums, one POD). Growth adds NEW fields
//    (never widens a hand type into a bitfield); each addition needs a per-game
//    adapter update and the mask-policy update to consume it.
//  - Header-only, no cpp; safe to include from a plain-C bridge via extern "C" wrapper.
#ifndef ZELDA3D_SHARED_PLAYER_LINK_GEAR_H
#define ZELDA3D_SHARED_PLAYER_LINK_GEAR_H

namespace Zelda3D {

// What Link's LEFT hand is doing this frame (his sword hand in normal orientation).
enum class LinkHandLeft : int {
    Open = 0,
    Closed,          // fist, no visible item
    SwordOneHand,    // Kokiri sword / Master sword / MM Kokiri
    SwordTwoHand,    // Biggoron/giant knife (OoT) or MM two-hand variant
    Bottle,          // holding a bottle
    Hammer,          // Megaton hammer / MM equivalent stow pose
    Boomerang,       // held boomerang (child OoT only, kept for future)
};

// What Link's RIGHT hand is doing this frame (his shield/bow hand).
enum class LinkHandRight : int {
    Open = 0,
    Closed,          // fist
    Shield,          // shield strapped to forearm
    Bow,             // bow drawn (includes slingshot)
    Hookshot,        // adult hookshot / MM long-hook variant
    Ocarina,         // playing ocarina
};

// Back-worn geometry: what the sheath / back sits as (sheath + shield combos are common).
enum class LinkSheath : int {
    ShieldOnBackSwordSheathed = 0, // typical stow pose (both back-worn)
    ShieldOnBackSwordDrawn,        // sword drawn, shield still on back
    SwordOnBackNoShield,           // no shield equipped, sword on back
    EmptySheathNoShield,           // sword drawn, no shield (idle mid-swap)
};

// Which shield Link has equipped RIGHT NOW (independent of whether it's worn).
enum class LinkShield : int {
    None = 0,
    Hylian,   // default adult (OoT) / Hero's Shield (MM)
    Mirror,   // mirror shield
    Deku,     // child OoT only
};

// The gear snapshot handed to shared policy. Small; passed by value.
struct LinkGear {
    LinkHandLeft  leftHand  = LinkHandLeft::Open;
    LinkHandRight rightHand = LinkHandRight::Open;
    LinkSheath    sheath    = LinkSheath::EmptySheathNoShield;
    LinkShield    shield    = LinkShield::None;
    // Strength upgrade, 0..3 (none / Goron bracelet / silver gauntlets / gold). The gauntlet PLATES
    // are separate meshes gated on >= 2, so gear that omits this cannot draw them at all — which is
    // exactly the bug this field was added for (debug_journal/2026-07-29-adult-gauntlet-plates-...).
    // MM has its own strength upgrade in the same slot, so this stays game-agnostic.
    int strengthUpgrade = 0;
    // Equipped boots: 0 normal, 1 iron, 2 hover. Iron and hover each add their own pair of meshes,
    // so gear without this cannot draw them — the same gap the gauntlet plates had.
    int boots = 0;
};

// Which of the two rest-pose skeletons Link is using this frame. MM's HUMAN form
// shares the adult rig with OoT; MM's Deku/Goron/Zora/Fierce Deity are separate
// enums that never reach this header — they live on the MM side as LinkFormStrategy.
enum class LinkAge : int { Child = 0, Adult };

} // namespace Zelda3D

#endif // ZELDA3D_SHARED_PLAYER_LINK_GEAR_H
