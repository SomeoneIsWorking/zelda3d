#include "entrance_validation.h"

#include "../zelda3d_repl.h"
#include "global.h"

namespace {
constexpr bool EntranceIndexIsValidForTable(int entrance) {
    return entrance >= 0 && entrance <= ENTR_MAX - 20;
}

static_assert(EntranceIndexIsValidForTable(0));
static_assert(EntranceIndexIsValidForTable(ENTR_MAX - 20));
static_assert(!EntranceIndexIsValidForTable(-1));
static_assert(!EntranceIndexIsValidForTable(ENTR_MAX - 19));
} // namespace

bool Zelda3D_EntranceIndexIsValid(int entrance) {
    // Play_Init may add a setup index as large as 19 before indexing gEntranceTable.
    return EntranceIndexIsValidForTable(entrance);
}

bool Zelda3D_ValidateEntrance(const char* command, int entrance, const char* outPath) {
    if (Zelda3D_EntranceIndexIsValid(entrance)) {
        return true;
    }
    Zelda3D_ReplReply(outPath,
                      "%s REFUSED 0x%x -- entrance index out of range (valid 0..%d, "
                      "gEntranceTable is ENTR_MAX=%d and Play_Init indexes it at "
                      "entranceIndex + sceneSetupIndex); nothing was changed",
                      command, entrance, ENTR_MAX - 20, ENTR_MAX);
    return false;
}
