#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// The audio thread's control block -- ONE object for the process.
//
// It used to be a `static` object named `audio` declared here, which gives every translation unit
// that includes this header its own private copy. Two do: OTRGlobals.cpp, which owns the thread,
// and savestates.cpp, which takes the mutex around a save/load precisely to exclude that thread.
// They were locking two different mutexes, so the exclusion never existed -- the audio thread was
// free to run through a savestate restore. Naming the type and declaring the object `extern` is
// what makes "the audio thread's state" a single thing rather than a per-includer coincidence.
//
// The rename to `gAudioControl` is not cosmetic: `audio` at namespace scope collides at link time
// with a global of the same name in ZAPD's Main.cpp, which the `static` had been hiding.
struct Zelda3DAudioControl {
    std::thread thread;
    std::condition_variable cv_to_thread;
    std::mutex mutex;
    std::atomic_bool running;
    std::atomic_bool processing;
};

extern Zelda3DAudioControl gAudioControl;
