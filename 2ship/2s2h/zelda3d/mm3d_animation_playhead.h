#pragma once

#include <string>

namespace Zelda3D::MM3D {

struct ActorAnimationPlayhead {
    int modelId = -1;
    float frame = 0.0f;
    bool hasFrame = false;
    std::string lastCsab;
    float lastFrame = 0.0f;
    std::string morphOut;
    float morphOutFrame = 0.0f;

    // A form change selects a different body model and animation namespace. Outgoing morph clips
    // must not survive that boundary and be loaded directly from another form's shared GAR path.
    bool beginModel(int nextModelId) {
        if (modelId == nextModelId) {
            return false;
        }
        *this = ActorAnimationPlayhead{};
        modelId = nextModelId;
        return true;
    }
};

} // namespace Zelda3D::MM3D
