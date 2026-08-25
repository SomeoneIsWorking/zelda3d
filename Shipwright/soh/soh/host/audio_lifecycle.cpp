#include "audio_lifecycle.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

#include "soh/OTRAudio.h"
#include "soh/ResourceManagerHelpers.h"
#include "regs.h"
#include "variables.h"

#include <libultraship/bridge/audiobridge.h>

extern "C" void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 numSamples);
int AudioPlayer_Buffered(void);

Zelda3DAudioControl gAudioControl;

void OTRAudio_Thread() {
#define SAMPLES_HIGH 560
#define SAMPLES_LOW 528
#define AUDIO_FRAMES_PER_UPDATE (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 1)
#define NUM_AUDIO_CHANNELS 2

    // Single producer routine used by both the wake-driven and pre-buffer
    // loops. Captures the per-iteration sample count from the caller.
    auto produce_and_play = [&](u32 num_audio_samples) {
        const u32 total_frames = num_audio_samples * AUDIO_FRAMES_PER_UPDATE;
        const u32 total_samples = total_frames * NUM_AUDIO_CHANNELS;

        // 3 is the maximum authentic frame divisor.
        static thread_local s16 audio_buffer[SAMPLES_HIGH * NUM_AUDIO_CHANNELS * 3];

        for (int i = 0; i < AUDIO_FRAMES_PER_UPDATE; i++) {
            AudioMgr_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * NUM_AUDIO_CHANNELS),
                                           num_audio_samples);
        }

        AudioPlayer_Play(reinterpret_cast<u8*>(audio_buffer), total_samples * sizeof(int16_t));
    };

    // Self-pump cadence. The gfx thread wakes us once per rendered frame
    // (Graph_ProcessGfxCommands sets gAudioControl.processing), but a single long
    // frame leave us asleep while the backend's queue drains to silence.
    // So we also wake on a short timeout, independent of the gfx frame rate.
    // Doing so is in fact closer to the console, where the audio task ran
    // off the scheduler rather than gated on rendering..
    constexpr auto kSelfPumpInterval = std::chrono::milliseconds(5);

    // The self-pump timeout must wait that the game has reached its render
    // loop, to avoid accessing uninitialized variables.
    bool primed = false;

    while (gAudioControl.running) {
        {
            std::unique_lock<std::mutex> Lock(gAudioControl.mutex);
            if (!primed) {
                // Pre-init: block until the gfx thread drives the first buffer
                // (engine guaranteed ready by then), exactly as before.
                while (!gAudioControl.processing && gAudioControl.running) {
                    gAudioControl.cv_to_thread.wait(Lock);
                }
                primed = true;
            } else if (!gAudioControl.processing && gAudioControl.running) {
                // Primed: wait for the next gfx wake, but no longer than
                // kSelfPumpInterval so a stalled gfx thread can't starve the
                // backend queue. A pending wake falls straight through.
                gAudioControl.cv_to_thread.wait_for(Lock, kSelfPumpInterval);
            }

            if (!gAudioControl.running) {
                break;
            }
        }

        {
            std::unique_lock<std::mutex> Lock(gAudioControl.mutex);
            int samples_left = AudioPlayer_Buffered();
            u32 num_audio_samples = samples_left < AudioPlayer_GetDesiredBuffered() ? SAMPLES_HIGH : SAMPLES_LOW;

            // Producer guard (banteg/Shipwright#6594): skip advancing the audio
            // engine if the backend ring cannot accept the smallest next burst.
            // Generating PCM that DoPlay() would refuse creates a discontinuity
            // audible as a click. The pre-buffer loop below will catch up once
            // the backend drains enough.
            if (AudioPlayer_Buffered() + SAMPLES_LOW * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                gAudioControl.processing = false;
            } else {
                produce_and_play(num_audio_samples);
                gAudioControl.processing = false;
            }
        }

        // Pre-buffer: fill the reservoir while the backend can accept more,
        // without waiting for the next frame signal. This absorbs load spikes.
        // Safe for BGM — the N64 sequencer advances independently of gameplay.
        // The producer guard (same as above) prevents advancing the audio engine
        // when the backend ring is already at capacity.
        while (gAudioControl.running && AudioPlayer_Buffered() < AudioPlayer_GetDesiredBuffered()) {
            if (AudioPlayer_Buffered() + SAMPLES_LOW * AUDIO_FRAMES_PER_UPDATE > AudioPlayer_GetDesiredBuffered()) {
                break;
            }
            int samples_left = AudioPlayer_Buffered();
            u32 num_audio_samples = samples_left < AudioPlayer_GetDesiredBuffered() ? SAMPLES_HIGH : SAMPLES_LOW;
            produce_and_play(num_audio_samples);
        }
    }
}

extern "C" void Zelda3D_AudioResetRunState(void) {
    const bool inheritedRunning = gAudioControl.running;
    const bool inheritedProcessing = gAudioControl.processing;
    const bool inheritedThread = gAudioControl.thread.joinable();

    // A live thread here means the previous run never called OTRAudio_Exit -- a teardown that did
    // not run, not merely a flag left set. Stopping it is this function's job precisely because the
    // run that owned it is already over and nothing else is going to.
    if (inheritedThread) {
        {
            std::unique_lock<std::mutex> Lock(gAudioControl.mutex);
            gAudioControl.running = false;
        }
        gAudioControl.cv_to_thread.notify_all();
        gAudioControl.thread.join();
    }

    gAudioControl.running = false;
    gAudioControl.processing = false;

    // Always printed, with what was actually looked at: "nothing inherited" is only meaningful next
    // to the list of things that could have been.
    fprintf(stderr, "ZELDA3D CORE: audio run state reset -- inherited running=%d processing=%d thread=%s.\n",
            (int)inheritedRunning, (int)inheritedProcessing, inheritedThread ? "ALIVE (joined here)" : "none");
    if (inheritedThread) {
        fprintf(stderr, "  A live audio thread means OTRAudio_Exit did NOT run for the previous run.\n");
    }
    fflush(stderr);
}

void OTRAudio_Init() {
    // Precache all our samples, sequences, etc...
    ResourceMgr_LoadDirectory("audio");

    if (!gAudioControl.running) {
        gAudioControl.running = true;
        gAudioControl.thread = std::thread(OTRAudio_Thread);
    }
}

extern "C" void OTRAudio_Exit() {
    // Tell the audio thread to stop. Guard with joinable() so this is idempotent
    // — called early (right after Graph_ThreadEntry returns on window-close) AND
    // again from DeinitOTR; the second call is a no-op.
    if (!gAudioControl.thread.joinable()) {
        return;
    }
    {
        std::unique_lock<std::mutex> Lock(gAudioControl.mutex);
        gAudioControl.running = false;
    }
    gAudioControl.cv_to_thread.notify_all();

    // Wait until the audio thread quit
    gAudioControl.thread.join();
}

int AudioPlayer_Buffered(void) {
    return AudioPlayerBuffered();
}

extern "C" int AudioPlayer_GetDesiredBuffered(void) {
    return AudioPlayerGetDesiredBuffered();
}

extern "C" void AudioPlayer_Play(const uint8_t* buf, uint32_t len) {
    AudioPlayerPlayFrame(buf, len);
}
