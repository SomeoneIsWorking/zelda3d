#pragma once

#include <libultraship/libultra.h>

namespace Rando {
class Location;
}

Rando::Location* ResolveRandomizerCheckFromActor(s16 actorId, s16 sceneNum, s32 actorParams);
