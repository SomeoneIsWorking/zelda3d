// title_sync.h — TitleSyncController: frame-syncs the embedded harness's
// two engines (Azahar/OoT3D oracle + SoH3D) at the title screen, by
// DEFAULT, so the SBS window always shows the SAME title-cs instant on
// both sides with no REPL setup needed.
//
// See debug_journal/2026-07-14-harness-title-sync.md for the full
// derivation and verification numbers. Summary of the architecture:
//
//   HOLD:  the oracle is loaded to scratch/title_settled.state and NOT
//          stepped (no retro_run()) -- it keeps presenting that single
//          frame -- while SoH3D boots completely cold (N64 logo -> title
//          cs) and its own raw engine-frame counter (sohFrameCount_, one
//          tick per RunFrame() call since soh_boot) climbs from 0.
//   LOCKED: once sohFrameCount_ passes kSyncSohFrame (a safe floor -- well
//          past SoH's own boot-splash tick count -- NOT an exact-alignment
//          claim, see below), main.cpp's CalibrateAndLock() runs a content
//          search (native port of tools/title_ab.py's calibrate(): a
//          zero-mean/unit-norm grayscale dot-product on both engines'
//          CURRENT framebuffers) to find exactly which az-step out of Az's
//          own title_settled.state timeline matches SoH's current frame,
//          then locks the oracle onto that az-step and steps it 1:1 with
//          every subsequent SoH frame.
//
// WHY A ONE-SHOT AFFINE LAW ISN'T ENOUGH (and why there IS a resync path):
// tools/title_ab.py's SOH_STEP_INTERCEPT=408 documents `soh_step ~= az_step
// + 408` as a SEARCH SEED, not an exact law -- its own calibrate() always
// does a fine content search around that seed. This controller reuses 408
// only as the MINIMUM soh-frame floor before the first calibration search
// (i.e. "wait long enough for SoH's title cs to be genuinely live"), never
// as an assumed-exact offset. That caution is empirically justified: this
// session's loop_period_check (scratch/loop_period_check.py, see the
// journal) ran the oracle SOLELY via repeated `run` calls -- no SoH
// involved at all -- for exactly 4800 calls twice in a row from a common
// point, expecting matching content if "N retro_run() calls" mapped onto a
// fixed number of internal cs-ticks. It did NOT match (moonlit sky vs a
// black loop-transition frame) -- confirming the existing az_run_until
// REPL command's own documented caveat ("each retro_run advances a
// variable slice depending on host wall-clock scheduling") accumulates
// real drift over a full ~4800-frame title-cs loop, even within one
// continuous process. So: 1:1 stepping is accurate SHORT-term (title_ab.py
// verified sub-2000-frame spans to the exact frame) but drifts LONG-term,
// which is exactly why every full SoH title-cs wrap (2400 -> 0, see
// title_presentation.cpp) triggers RELOAD-title_settled.state +
// RECALIBRATE (same content search) rather than trusting the loop periods
// to line up for free.
#pragma once

#include <cstdint>

class TitleSyncController {
public:
    enum class State { UNARMED, HOLD, LOCKED, DISABLED };

    // Minimum SoH raw engine-frame count (RunFrame() calls since soh_boot)
    // before the FIRST content-search calibration is attempted -- a safe
    // floor (title_ab.py's own affine seed), not an exact-alignment claim;
    // see the file header for why an exact claim isn't trustworthy long-term.
    static constexpr uint64_t kSyncSohFrame = 408;

    // az-step search half-width for CalibrateAndLock()'s content search
    // (main.cpp), both for the initial calibration and every post-wrap
    // recalibration. title_ab.py's own calibrate() needed >=150 for a
    // <2000-frame span; searching the full loop span here uses a wider
    // margin for headroom.
    static constexpr int kCalibrateMargin = 400;

    // A SoH title-cs frame (Zelda3D_TitleCsFrame(), wraps 0..2399) drop of
    // at least this much between consecutive LOCKED iterations is treated
    // as a loop-wrap event (real wraps drop ~2400; this threshold is far
    // below that so it can't misfire on ordinary forward motion).
    static constexpr int kWrapDropThreshold = 1500;

    // Call once, before the first combined `step`. `armed=true` means the
    // caller (ArmTitleSync() in main.cpp) successfully auto-loaded
    // title_settled.state + soh_boot for this purpose -- engage HOLD.
    // `armed=false` means don't engage at all (arm failed, or a manual
    // loadstate/soh_boot already happened before the first `step` -- a
    // script driving its own scene, not the fresh-boot title default) --
    // permanently DISABLED, `step` behaves exactly like the old
    // unconditional lockstep passthrough forever after.
    void Arm(bool armed) { state_ = armed ? State::HOLD : State::DISABLED; }

    bool IsUnarmed() const { return state_ == State::UNARMED; }
    bool IsActive()  const { return state_ == State::HOLD || state_ == State::LOCKED; }
    State state() const { return state_; }

    uint64_t sohFrameCount() const { return sohFrameCount_; }
    uint64_t azFrameCount()  const { return azFrameCount_; }
    int lastCsFrame()  const { return lastCsFrame_; }
    int calibrations() const { return calibrations_; }

    // Call once per HOLD/LOCKED combined-step iteration, AFTER RunFrame()
    // has already advanced SoH by one frame. Returns true exactly once,
    // the first time sohFrameCount_ crosses kSyncSohFrame while still in
    // HOLD -- the caller should then run CalibrateAndLock().
    bool NoteSohFrame() {
        ++sohFrameCount_;
        return state_ == State::HOLD && sohFrameCount_ > kSyncSohFrame;
    }

    // Call once per LOCKED iteration with SoH's current title-cs frame.
    // Returns true exactly when a loop wrap is detected (sharp decrease) --
    // the caller should reload title_settled.state and run
    // CalibrateAndLock() again.
    bool NoteCsFrameAndDetectWrap(int csFrame) {
        bool wrapped = lastCsFrame_ >= 0 &&
                       (lastCsFrame_ - csFrame) >= kWrapDropThreshold;
        lastCsFrame_ = csFrame;
        return wrapped;
    }

    // Called by CalibrateAndLock() (main.cpp) once its content search has
    // found the az-step that best matches SoH's current frame and has
    // replayed the oracle exactly there from a fresh title_settled.state
    // load. Switches to LOCKED and resets wrap-detection state (a stale
    // pre-wrap csFrame must not falsely trigger a wrap check right after
    // recalibrating).
    void SetLocked(uint64_t azFrame) {
        azFrameCount_ = azFrame;
        state_ = State::LOCKED;
        lastCsFrame_ = -1;
        ++calibrations_;
    }

private:
    State state_ = State::UNARMED;
    uint64_t sohFrameCount_ = 0;
    uint64_t azFrameCount_  = 0;
    int lastCsFrame_  = -1;
    int calibrations_ = 0;
};

extern TitleSyncController g_titleSync;

// Path to the settled title save-state the controller (re)loads on arm and
// on every loop-wrap recalibration (auto-generated via tools/title_settle.py
// if missing -- see ArmTitleSync() in main.cpp).
extern const char* const kTitleSettledStatePath;
