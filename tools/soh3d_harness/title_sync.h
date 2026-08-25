// title_sync.h — TitleSyncController: frame-syncs the embedded harness's
// two engines (Azahar/OoT3D oracle + SoH3D) at the title screen, by
// DEFAULT, so the SBS window always shows the SAME title-cs instant on
// both sides with no REPL setup needed.
//
// See debug_journal/2026-07-14-harness-title-sync.md for the full
// derivation, falsification history, and verification numbers.
//
// SYNC MECHANISM — integers derived from RE'd state, NOT pixel matching:
//
//   1. Absolute anchor (HOLD -> LOCKED): the oracle is held at
//      scratch/title_settled.state; its title-cs frame is derived EXACTLY
//      by inverting its live camera eye (fixed VA 0x005BE6D4, RE'd title
//      camera basis) against the byte-exact ported OP97 camera spline
//      (Zelda3D_TitleCsCamera, same data both engines play). This is the
//      same inversion the 2026-07-09 rate-law derivation used (residual
//      <0.1 world units — exact, not approximate). The controller then
//      HOLDs until SoH's own cursor (Zelda3D_TitleCsFrame(), climbing
//      0.5/frame from cold boot) reaches that value — integer equality.
//   2. Rate model (while LOCKED): the oracle's u32 @ VA 0x0054CC3C is its
//      title-state VBLANK counter (NOT the cs cursor — that reading was
//      falsified 2026-07-04, see title-cs-re-pivot journal; writer
//      increments [0x0054CC34]+8 on event mask 0x400). The RE'd rate law
//      az_cs = anchor + 0.5 * vblanks (2026-07-09, exact across the whole
//      loop) turns it into a live cs-frame model:
//        azCs = azLockCs + (vbl - vblAtLock) / 2.
//      Each frame the governor compares that model against SoH's cursor
//      and steps the oracle 0/1/2 retro_run()s to hold the delta at 0.
//   3. Loop wrap: when SoH's cursor wraps (sharp drop), the controller
//      reloads the settled state and re-enters HOLD -- the SAME integer
//      lock mechanism as boot, no search, no replay. (The oracle's own
//      wrap timing is NOT assumed equal to SoH's -- whether the 3DS demo
//      inserts transition frames at the seam is a separate, real
//      content-divergence question the harness must not paper over.)
//
// DETERMINISM (prerequisite for #2): retro_run() is fully deterministic —
// scratch/az_determinism_check.py (2026-07-14 session 2) ran the same
// schedule in two fresh processes: vblank counter, CoreTiming global
// ticks, and framebuffer sha256 all identical at every checkpoint
// (100/500/1000/2000/4500). The earlier "retro_run advances a variable
// host-scheduling-dependent slice" caveat is about TICKS per call (slice
// boundaries within a frame), not FRAMES per call — it does NOT cause
// content drift and must not be cited as if it did.
//
// HISTORY (falsified approaches — do not resurrect):
//   - Content-search calibration (a C++ port of title_ab.py's
//     content_score sweeping hundreds of candidate frames, plus periodic
//     checkpoint recalibration) was implemented and RETIRED the same day
//     (2026-07-14): a pixel-similarity hack compensating for not reading
//     the integer state that already existed, and it demonstrably
//     mislocked on low-signal (near-black) frames. content_score remains
//     in use as a VERIFICATION metric (tools/title_sbs_verify.py) — its
//     proper job — never as the sync mechanism.
//   - Treating 0x0054CC3C as the cs cursor directly (pre-2026-07-04): it
//     is the vblank counter; only the RATE law above makes it usable.
#pragma once

#include <cstdint>

class TitleSyncController {
  public:
    enum class State { UNARMED, HOLD, LOCKED, DISABLED };

    // Emulated-memory VA of the oracle's title-state VBLANK counter (u32,
    // +1 per retro_run video frame). Provenance: located by runtime
    // dump-diff scan 2026-07-04; identified as a vblank counter (NOT the
    // cs cursor) by the FUN_003fd27c reader decomp the same day
    // (debug_journal/2026-07-04-title-cs-re-pivot.md).
    static constexpr uint32_t kAzVblankCounterVA = 0x0054CC3C;

    // Emulated-memory VA of the oracle's live title camera basis
    // (+0x00 eye Vec3f — provenance in
    // oot3d-decomp/docs/title_view_matrix_lh.md).
    static constexpr uint32_t kAzTitleCamEyeVA = 0x005BE6D4;

    // Max |eye - spline(frame)| (world units) for an inversion candidate
    // to count as a hit. The 2026-07-09 inversions saw residuals <0.1;
    // 1.0 is comfortable headroom while still rejecting wrong segments.
    static constexpr float kEyeInvertMaxResidual = 1.0f;

    // A SoH cursor drop of at least this much between consecutive LOCKED
    // frames = loop wrap (real wraps drop ~2400).
    static constexpr int kWrapDropThreshold = 1500;

    // |cursor delta| at/above which the LOCKED governor logs a warning
    // (it always corrects; the warning means the delta left the +-1
    // tick-parity band, which determinism says should never happen).
    static constexpr int kDeltaWarnThreshold = 2;

    void Arm(bool armed) {
        state_ = armed ? State::HOLD : State::DISABLED;
    }

    bool IsUnarmed() const {
        return state_ == State::UNARMED;
    }
    bool IsActive() const {
        return state_ == State::HOLD || state_ == State::LOCKED;
    }
    State state() const {
        return state_;
    }

    uint64_t sohFrameCount() const {
        return sohFrameCount_;
    }
    uint64_t azFrameCount() const {
        return azFrameCount_;
    }
    int azLockCs() const {
        return azLockCs_;
    }
    int lastDelta() const {
        return lastDelta_;
    }
    int corrections() const {
        return corrections_;
    }
    int maxAbsDelta() const {
        return maxAbsDelta_;
    }
    int locks() const {
        return locks_;
    }

    void NoteSohFrame() {
        ++sohFrameCount_;
    } // once per SoH RunFrame()
    void BumpAzFrame() {
        ++azFrameCount_;
    } // once per oracle retro_run()

    // Set once the arm-time eye-inversion has produced the oracle's held
    // cs frame (lazily, during HOLD, once SoH's cs data is loaded so the
    // ported spline is available to invert against).
    void SetAzLockCs(int cs) {
        azLockCs_ = cs;
    }
    bool HasAzLockCs() const {
        return azLockCs_ >= 0;
    }

    // HOLD -> LOCKED test: SoH's climbing cursor reached the oracle's
    // held frame.
    bool ShouldLock(int sohCs) const {
        return state_ == State::HOLD && azLockCs_ >= 0 && sohCs >= 1 && sohCs >= azLockCs_;
    }
    // `vblNow` = vblank counter at the lock instant — the rate-model
    // anchor: azCs(vbl) = azLockCs_ + (vbl - vblAtLock_) / 2.
    void SetLocked(uint32_t vblNow) {
        state_ = State::LOCKED;
        vblAtLock_ = vblNow;
        lastSohCs_ = -1;
        ++locks_;
    }
    int ModelAzCs(uint32_t vblNow) const {
        return azLockCs_ + static_cast<int>((vblNow - vblAtLock_) / 2);
    }

    // LOCKED wrap test: SoH's cursor dropped sharply -> caller reloads
    // the settled state and re-enters HOLD (same lock mechanism as boot).
    bool DetectWrap(int sohCs) {
        bool wrapped = lastSohCs_ >= 0 && (lastSohCs_ - sohCs) >= kWrapDropThreshold;
        lastSohCs_ = sohCs;
        return wrapped;
    }
    void ReHold() {
        state_ = State::HOLD;
    }

    // LOCKED governor: how many retro_run() calls this frame (0/1/2) so
    // the (sohCs - modelAzCs) delta converges to 0 and stays there.
    int GovernorSteps(int sohCs, int modelAzCs) {
        int d = sohCs - modelAzCs;
        lastDelta_ = d;
        if (d > maxAbsDelta_)
            maxAbsDelta_ = d;
        if (-d > maxAbsDelta_)
            maxAbsDelta_ = -d;
        if (d == 0)
            return 1;
        ++corrections_;
        // NOTE: an extra/skipped retro_run moves the vblank counter too,
        // so the model self-consistently absorbs the correction.
        return d > 0 ? 2 : 0; // SoH ahead -> oracle catches up; behind -> hold
    }

  private:
    State state_ = State::UNARMED;
    uint64_t sohFrameCount_ = 0;
    uint64_t azFrameCount_ = 0;
    int azLockCs_ = -1;
    uint32_t vblAtLock_ = 0;
    int lastSohCs_ = -1;
    int lastDelta_ = 0;
    int maxAbsDelta_ = 0;
    int corrections_ = 0;
    int locks_ = 0;
};

extern TitleSyncController g_titleSync;

// Path to the settled title save-state the controller loads on arm and on
// every loop-wrap re-hold (auto-generated via tools/title_settle.py if
// missing -- see HarnessTitleSyncRuntime::EnsureArmed()).
extern const char* const kTitleSettledStatePath;
