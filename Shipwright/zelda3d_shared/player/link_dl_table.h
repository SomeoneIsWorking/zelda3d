// OoT3D player display-list table, ported as DATA.
//
// This is `sPlayerDLists[PLAYER_MODELTYPE_MAX]` from OoT3D's code.bin (@0x0053c698): 21 pointers,
// one per PLAYER_MODELTYPE_*, each to a row of four s32 { adultNear, childNear, adultFar, childFar }.
// On N64 those four slots hold display-list POINTERS; on 3DS they hold CMB **mesh ids**, which is
// exactly what our renderer's mesh-id mask consumes — so the table ports across directly.
//
// Ground truth and the derivation (addresses, the five independent checks that pin the index
// assignment, and the shield-variant runs) are in `oot3d-decomp/docs/player_dl_tables.md`.
// Claim C027.
//
// WHY THIS IS A TABLE AND NOT A SWITCH: every one of the ~7 mesh-id divergences the audit found was
// a hand-written case that guessed a value — hammer drawing nothing, the bottle using a fist, the
// hookshot and ocarina using an open palm, the third-person bow using the FIRST-person arm mesh. The
// values were never the hard part; writing them by hand one at a time was. Keep new state coming
// from this table.
#ifndef ZELDA3D_SHARED_PLAYER_LINK_DL_TABLE_H
#define ZELDA3D_SHARED_PLAYER_LINK_DL_TABLE_H

#include "link_gear.h"

namespace Zelda3D {

// PLAYER_MODELTYPE_* (Shipwright/soh/include/z64player.h). Values are the real indices into
// sPlayerDLists, so they must not be renumbered.
enum class LinkModelType : int {
    LhOpen = 0x00,
    LhClosed = 0x01,
    LhSword = 0x02,
    LhSword2 = 0x03, // unused on N64; OoT3D holds the same values as LhSword
    LhBgs = 0x04,
    LhHammer = 0x05,
    LhBoomerang = 0x06,
    LhBottle = 0x07,

    RhOpen = 0x08,
    RhClosed = 0x09,
    RhShield = 0x0A, // base of a 12-row shield-variant run
    RhBowSlingshot = 0x0B,
    RhBowSlingshot2 = 0x0C, // unused on N64; same values as RhBowSlingshot
    RhOcarina = 0x0D,
    RhOot = 0x0E,
    RhHookshot = 0x0F,

    Sheath16 = 0x10,
    Sheath17 = 0x11,
    Sheath18 = 0x12,
    Sheath19 = 0x13, // base of an 8-row shield-variant run
    Waist = 0x14,

    Max = 0x15,
};

// One table row. -1 means "draw nothing" (OoT3D stores a null DL slot as -1).
struct LinkDlRow {
    short adult;
    short child;
};

// sPlayerDLists[type]. Out-of-range yields { -1, -1 } rather than reading past the table.
LinkDlRow linkDlRow(LinkModelType type);

// Mesh id for one age, or -1 for "draw nothing".
short linkDlMesh(LinkModelType type, LinkAge age);

} // namespace Zelda3D

#endif // ZELDA3D_SHARED_PLAYER_LINK_DL_TABLE_H
