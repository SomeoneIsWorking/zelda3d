// See link_dl_table.h. Values read byte-exact out of OoT3D's code.bin (file offset = VA - 0x100000).
#include "link_dl_table.h"

namespace Zelda3D {

// sPlayerDLists[PLAYER_MODELTYPE_MAX] @ VA 0x0053c698 -- 21 pointers, each to a 0x10 row of four
// s32 { adultNear, childNear, adultFar, childFar }. Every row in the region has near == far except
// the two adult-only first-person rows, so a single (adult, child) pair per type is faithful here;
// the `tableVa` column is kept so any value can be re-read from the ROM without re-deriving the
// master table.
//
// Index order is NOT assumed -- it is pinned by five independent checks (see the header's doc
// reference): the master table has exactly 21 slots matching PLAYER_MODELTYPE_MAX; the two entries
// N64 documents as "unused, same as X" hold identical values here while pointing at DIFFERENT
// addresses; both sheath slots land on tables already verified in-game; LhBgs=37 matches what this
// port already did; and the shield run reproduces this file's own previously hand-derived
// ShieldOnBackSwordSheathed line exactly.
namespace {
struct Entry {
    LinkModelType type;
    unsigned tableVa;
    short adult;
    short child;
};

// Ordered by model type so the array index equals the enum value; asserted below.
const Entry kPlayerDlTable[] = {
    { LinkModelType::LhOpen,          0x0053a5c8, 13,  0 },
    { LinkModelType::LhClosed,        0x0053a5d8, 14,  1 },
    { LinkModelType::LhSword,         0x0053c588, 16,  2 },
    { LinkModelType::LhSword2,        0x0053c578, 16,  2 },
    { LinkModelType::LhBgs,           0x0053a5a8, 37, 16 },
    { LinkModelType::LhHammer,        0x0053c658, 32,  0 },
    { LinkModelType::LhBoomerang,     0x0053a5e8, 13,  6 },
    { LinkModelType::LhBottle,        0x0053c668, 24,  7 },

    { LinkModelType::RhOpen,          0x0053c598, 20,  3 },
    { LinkModelType::RhClosed,        0x0053c5a8, 21,  4 },
    { LinkModelType::RhShield,        0x0053c3f8, 21,  4 }, // NONE row of the 12-row run
    { LinkModelType::RhBowSlingshot,  0x0053c5b8, 29, 19 },
    { LinkModelType::RhBowSlingshot2, 0x0053c618, 29, 19 },
    { LinkModelType::RhOcarina,       0x0053c628, 40, 17 },
    { LinkModelType::RhOot,           0x0053c638, 40, 18 },
    { LinkModelType::RhHookshot,      0x0053c648, 33,  3 },

    { LinkModelType::Sheath16,        0x0053c5c8, 31, 14 },
    { LinkModelType::Sheath17,        0x0053c5e8, 42, 21 },
    { LinkModelType::Sheath18,        0x0053c438, 31, 14 },
    { LinkModelType::Sheath19,        0x0053c4d8, 42, 21 }, // NONE row of the 8-row run
    { LinkModelType::Waist,           0x0053c608, -1, -1 },
};

static_assert(sizeof(kPlayerDlTable) / sizeof(kPlayerDlTable[0]) == (unsigned)LinkModelType::Max,
              "kPlayerDlTable must hold exactly PLAYER_MODELTYPE_MAX rows -- a missing row would "
              "silently shift every later type onto the wrong mesh ids");
} // namespace

LinkDlRow linkDlRow(LinkModelType type) {
    const int i = (int)type;
    if (i < 0 || i >= (int)LinkModelType::Max) {
        return LinkDlRow{ -1, -1 };
    }
    const Entry& e = kPlayerDlTable[i];
    // The array is ordered so index == enum value; if that ever drifts, fail loudly at the call
    // rather than returning a plausible-looking row from the wrong model type.
    if (e.type != type) {
        return LinkDlRow{ -1, -1 };
    }
    return LinkDlRow{ e.adult, e.child };
}

short linkDlMesh(LinkModelType type, LinkAge age) {
    const LinkDlRow r = linkDlRow(type);
    return (age == LinkAge::Adult) ? r.adult : r.child;
}

} // namespace Zelda3D
