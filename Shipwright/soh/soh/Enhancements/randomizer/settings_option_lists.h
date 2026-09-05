#pragma once

#include <string>
#include <vector>

namespace Rando {

std::vector<std::string> NumOpts(int min, int max, int step = 1, const std::string& textBefore = {},
                                 const std::string& textAfter = {});
std::vector<std::string> MultiVecOpts(const std::vector<std::vector<std::string>>& optionsVector);

} // namespace Rando
