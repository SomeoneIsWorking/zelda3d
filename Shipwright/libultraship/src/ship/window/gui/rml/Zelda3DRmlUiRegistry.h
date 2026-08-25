#pragma once

namespace Ship {

class SohRmlUi;

namespace Zelda3DRmlUiRegistry {

void Attach(SohRmlUi* menu);
void Detach(const SohRmlUi* menu);
SohRmlUi* Get();

} // namespace Zelda3DRmlUiRegistry
} // namespace Ship
