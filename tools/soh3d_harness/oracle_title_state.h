#pragma once

#include <sstream>

namespace HarnessOracle {

struct TitleCameraBasis {
    float eye[3];
    float right[3];
    float up[3];
    float dir[3];
};

struct TitleLinkWorldPosition {
    float pos[3];
};

bool ReadTitleCameraBasis(TitleCameraBasis* out);
bool ReadTitleLinkWorldPosition(TitleLinkWorldPosition* out);
void HandleTitleActors(std::istringstream& arguments);

} // namespace HarnessOracle
