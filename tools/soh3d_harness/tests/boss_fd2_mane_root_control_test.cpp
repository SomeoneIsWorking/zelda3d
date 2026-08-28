#include <cassert>

#include "boss_fd2_mane_root_control.h"

namespace {

using HarnessBossFd2ManeRootControl::Roots;
using HarnessBossFd2ManeRootControl::Status;
using HarnessBossFd2ManeRootControl::Trajectory;

Roots UniformRoots(float x, float y, float z) {
    Roots roots{};
    for (int chain = 0; chain < 3; ++chain) {
        roots[chain] = { x + chain * 10.0F, y, z - chain * 10.0F };
    }
    return roots;
}

void Translate(Roots* roots, float x, float y, float z) {
    for (auto& root : *roots) {
        root[0] += x;
        root[1] += y;
        root[2] += z;
    }
}

} // namespace

int main() {
    Trajectory control;
    const Roots oracleStart = UniformRoots(-220.0F, -970.0F, -226.0F);
    Roots sohStart = oracleStart;
    Translate(&sohStart, 500.0F, -300.0F, 20.0F);
    control.Arm(oracleStart, sohStart);
    assert(control.GetSnapshot().status == Status::Tracking);
    assert(control.CurrentWasObserved(oracleStart, sohStart));

    Roots oracleNext = oracleStart;
    Roots sohNext = sohStart;
    Translate(&oracleNext, 0.25F, -0.125F, 0.0F);
    Translate(&sohNext, 0.25F, -0.125F, 0.0F);
    const auto equalMotion = control.Observe(oracleNext, sohNext);
    assert(equalMotion.status == Status::Tracking);
    assert(equalMotion.observedSteps == 1);
    assert(control.CurrentWasObserved(oracleNext, sohNext));

    Roots unobserved = sohNext;
    Translate(&unobserved, 1.0F, 0.0F, 0.0F);
    assert(!control.CurrentWasObserved(oracleNext, unobserved));

    Trajectory positiveControl;
    positiveControl.Arm(oracleStart, sohStart);
    Roots oracleFault = oracleStart;
    Roots sohFault = sohStart;
    oracleFault[2][1] += 1.0F;
    const auto detected = positiveControl.Observe(oracleFault, sohFault);
    assert(detected.status == Status::Diverged);
    assert(detected.maximumStepDelta == 1.0F);
    assert(!positiveControl.CurrentWasObserved(oracleFault, sohFault));

    positiveControl.Reset();
    assert(positiveControl.GetSnapshot().status == Status::Unarmed);
    return 0;
}
