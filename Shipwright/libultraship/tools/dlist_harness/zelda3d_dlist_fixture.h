#pragma once

#include <string>

#include "dlist_fixture.h"

namespace Zelda3D::DlistHarness {

bool BuildZelda3DDlistFixture(DlistFixture& fixture, const std::string& zarPath, int modelId, float rotationX,
                              float rotationY, float rotationZ);

} // namespace Zelda3D::DlistHarness
