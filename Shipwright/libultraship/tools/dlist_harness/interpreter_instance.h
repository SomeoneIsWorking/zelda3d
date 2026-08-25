#pragma once

#include <memory>

namespace Fast {

class Interpreter;
void GfxSetInstance(std::shared_ptr<Interpreter> interpreter);

} // namespace Fast
