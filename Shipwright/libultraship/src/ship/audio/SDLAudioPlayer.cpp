#include "ship/audio/SDLAudioPlayer.h"
#include <spdlog/spdlog.h>

namespace Ship {

SDLAudioPlayer::~SDLAudioPlayer() {
    SPDLOG_TRACE("destruct SDL audio player");
    DoClose();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLAudioPlayer::DoClose() {
    if (mStream != nullptr) {
        // SDL3-MIGRATION: SDL_DestroyAudioStream tears down the stream AND closes the
        // logical device that SDL_OpenAudioDeviceStream opened, discarding any queued
        // audio in the process — so it replaces the SDL2 pause/clear/close sequence.
        SDL_DestroyAudioStream(mStream);
        mStream = nullptr;
    }
}

bool SDLAudioPlayer::DoInit() {
    // Headless mode (env SOH_HEADLESS=1): use SDL's dummy audio driver so no sound is
    // played out of the user's speakers. Set before SDL inits its audio subsystem.
    const char* headlessEnv = getenv("SOH_HEADLESS");
    if (headlessEnv != nullptr && headlessEnv[0] == '1') {
        // SDL3-MIGRATION: SDL_setenv -> SDL_setenv_unsafe (SDL3 renamed it; the env must be set
        // before SDL_Init brings up the audio subsystem, so the non-thread-safe variant is fine here).
        SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
    }
    // SDL3-MIGRATION: SDL_Init now returns bool (true on success) instead of 0/non-zero.
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        SPDLOG_ERROR("SDL init error: {}", SDL_GetError());
        return false;
    }

    // Always open with the correct number of output channels
    mNumChannels = this->GetNumOutputChannels();

    // SDL3-MIGRATION: SDL_AudioSpec lost the `samples` and `callback` fields (buffer sizing
    // is now managed internally by the stream). Only format/channels/freq remain, and the
    // format enum AUDIO_S16SYS -> SDL_AUDIO_S16 (native-endian signed 16-bit).
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = this->GetSampleRate();
    want.format = SDL_AUDIO_S16;
    want.channels = mNumChannels;

    // SDL3-MIGRATION: SDL_OpenAudioDevice + SDL_QueueAudio is replaced by a single
    // SDL_OpenAudioDeviceStream that opens the default playback device and binds a stream
    // to it. We pass no callback (NULL) and pull-free push samples with SDL_PutAudioStreamData.
    mStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
    if (mStream == nullptr) {
        SPDLOG_ERROR("SDL_OpenAudioDeviceStream error: {}", SDL_GetError());
        return false;
    }

    SPDLOG_INFO("SDL Audio initialized: {} channels, {} Hz, driver={}", mNumChannels, this->GetSampleRate(),
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");

    // SDL3-MIGRATION: devices opened via SDL_OpenAudioDeviceStream start PAUSED; we must
    // resume the bound device or no audio is ever consumed from the stream.
    if (!SDL_ResumeAudioStreamDevice(mStream)) {
        SPDLOG_ERROR("SDL_ResumeAudioStreamDevice error: {}", SDL_GetError());
        return false;
    }
    return true;
}

int SDLAudioPlayer::Buffered() {
    // SDL3-MIGRATION: SDL_GetQueuedAudioSize -> SDL_GetAudioStreamQueued (bytes still pending
    // in the stream). Convert bytes -> frames exactly as before.
    return SDL_GetAudioStreamQueued(mStream) / (sizeof(int16_t) * mNumChannels);
}

void SDLAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    if (Buffered() < 6000) {
        // Don't fill the audio buffer too much in case this happens
        // SDL3-MIGRATION: SDL_QueueAudio -> SDL_PutAudioStreamData. `len` is the byte count
        // (int param); the throttling threshold above is unchanged.
        SDL_PutAudioStreamData(mStream, buf, static_cast<int>(len));
    }
}
} // namespace Ship
