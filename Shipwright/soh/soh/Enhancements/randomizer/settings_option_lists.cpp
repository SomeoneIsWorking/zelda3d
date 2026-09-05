#include "settings_option_lists.h"

namespace Rando {

std::vector<std::string> NumOpts(const int min, const int max, const int step, const std::string& textBefore,
                                 const std::string& textAfter) {
    std::vector<std::string> options;
    options.reserve((max - min) / step + 1);
    for (int i = min; i <= max; i += step) {
        options.push_back(textBefore + std::to_string(i) += textAfter);
    }
    return options;
}

std::vector<std::string> MultiVecOpts(const std::vector<std::vector<std::string>>& optionsVector) {
    size_t totalSize = 0;
    for (const auto& vector : optionsVector) {
        totalSize += vector.size();
    }
    std::vector<std::string> options;
    options.reserve(totalSize);
    for (const auto& vector : optionsVector) {
        for (const auto& op : vector) {
            options.push_back(op);
        }
    }
    return options;
}

} // namespace Rando
