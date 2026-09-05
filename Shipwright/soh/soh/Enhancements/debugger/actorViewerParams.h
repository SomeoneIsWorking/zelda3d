#pragma once

#include "z64actor.h"

bool ActorViewerActorUsesParams(u16 actorId);
bool ActorViewerHasCustomParameterEditor(u16 actorId);
s16 ActorViewerDrawCustomParameterEditor(u16 actorId, s16 params);
