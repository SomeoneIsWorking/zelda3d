#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD2_MANE_ROOT_CONTROL_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD2_MANE_ROOT_CONTROL_H

#include <algorithm>
#include <array>
#include <cmath>

namespace HarnessBossFd2ManeRootControl {

using Roots = std::array<std::array<float, 3>, 3>;

enum class Status {
    Unarmed,
    Tracking,
    Diverged,
};

struct Snapshot {
    Status status = Status::Unarmed;
    int observedSteps = 0;
    float maximumStepDelta = 0.0F;
};

class Trajectory {
  public:
    void Arm(const Roots& oracle, const Roots& soh) {
        mOraclePrevious = oracle;
        mSohPrevious = soh;
        mSnapshot = { Status::Tracking, 0, 0.0F };
    }

    Snapshot Observe(const Roots& oracle, const Roots& soh) {
        if (mSnapshot.status == Status::Unarmed) {
            return mSnapshot;
        }
        for (int chain = 0; chain < 3; ++chain) {
            for (int axis = 0; axis < 3; ++axis) {
                const float oracleStep = oracle[chain][axis] - mOraclePrevious[chain][axis];
                const float sohStep = soh[chain][axis] - mSohPrevious[chain][axis];
                const float delta = std::fabs(sohStep - oracleStep);
                mSnapshot.maximumStepDelta = std::max(mSnapshot.maximumStepDelta, delta);
                if (!EquivalentStep(oracleStep, sohStep, delta)) {
                    mSnapshot.status = Status::Diverged;
                }
            }
        }
        mOraclePrevious = oracle;
        mSohPrevious = soh;
        ++mSnapshot.observedSteps;
        return mSnapshot;
    }

    bool CurrentWasObserved(const Roots& oracle, const Roots& soh) const {
        if (mSnapshot.status != Status::Tracking) {
            return false;
        }
        for (int chain = 0; chain < 3; ++chain) {
            for (int axis = 0; axis < 3; ++axis) {
                if (!EquivalentValue(oracle[chain][axis], mOraclePrevious[chain][axis]) ||
                    !EquivalentValue(soh[chain][axis], mSohPrevious[chain][axis])) {
                    return false;
                }
            }
        }
        return true;
    }

    Snapshot GetSnapshot() const {
        return mSnapshot;
    }

    void Reset() {
        mSnapshot = {};
        mOraclePrevious = {};
        mSohPrevious = {};
    }

  private:
    // This comparator asks whether exact solver inputs produce exact solver outputs. Even a one-bit
    // root-displacement mismatch is a different input, so there is deliberately no residual-derived
    // tolerance here.
    static bool EquivalentStep(float oracleStep, float sohStep, float delta) {
        return std::isfinite(delta) && oracleStep == sohStep;
    }

    static bool EquivalentValue(float value, float previous) {
        return std::isfinite(value) && std::isfinite(previous) && value == previous;
    }

    Roots mOraclePrevious{};
    Roots mSohPrevious{};
    Snapshot mSnapshot{};
};

} // namespace HarnessBossFd2ManeRootControl

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_BOSS_FD2_MANE_ROOT_CONTROL_H
