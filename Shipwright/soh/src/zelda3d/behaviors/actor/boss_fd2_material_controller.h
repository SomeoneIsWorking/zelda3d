// Actor-local OoT3D material-animation clocks for the hole-form Volvagia draw.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIAL_CONTROLLER_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIAL_CONTROLLER_H

#include <cstdint>

namespace Zelda3D::BossFd2Materials {

class Controller {
  public:
    void tick(bool drawActive, bool faceExposed) {
        // OoT3D runs the actor/draw at 30 Hz while SoH updates at 20 Hz. Keep the exact 3:2
        // cadence so event-local material clocks see one, then two authored draw steps.
        mAuthoredPhase = static_cast<std::uint8_t>(mAuthoredPhase + 3);
        const int authoredDraws = mAuthoredPhase / 2;
        mAuthoredPhase %= 2;

        if (faceExposed && !mFaceExposed) {
            mPulseFrame = 0;
        }
        mFaceExposed = faceExposed;
        if (!drawActive) {
            return;
        }

        // FUN_0020A3B0/FUN_00335904 install play+0x7F44 (=2.0 in active gameplay) as the body and
        // fire-hair controller step before each 30 Hz submission. The exposed-face pulse retains
        // its constructor step of 1.0 and advances only while faceExposed is set.
        mBodyAndHairFrame += static_cast<std::uint64_t>(authoredDraws * 2);
        if (faceExposed) {
            mPulseFrame += static_cast<std::uint64_t>(authoredDraws);
        }
    }

    [[nodiscard]] float bodyAndHairFrame(int duration) const {
        return frameForDuration(mBodyAndHairFrame, duration);
    }

    [[nodiscard]] float pulseFrame(int duration) const {
        return frameForDuration(mPulseFrame, duration);
    }

    [[nodiscard]] bool faceExposed() const {
        return mFaceExposed;
    }

  private:
    static float frameForDuration(std::uint64_t frame, int duration) {
        if (duration <= 0) {
            return 0.0f;
        }
        return static_cast<float>(frame % static_cast<std::uint64_t>(duration));
    }

    std::uint64_t mBodyAndHairFrame = 0;
    std::uint64_t mPulseFrame = 0;
    std::uint8_t mAuthoredPhase = 0;
    bool mFaceExposed = false;
};

} // namespace Zelda3D::BossFd2Materials

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIAL_CONTROLLER_H
