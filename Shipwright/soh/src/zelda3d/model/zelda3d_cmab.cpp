// Zelda3D CMAB player implementation — see zelda3d_cmab.h for the design note and ground truth.
// Byte layout + sampling math are a direct port of tools/cmab.py (verified against
// oot3d-decomp/docs/title_logo_fireglow_cmab.md §1); keep the two in sync if either changes.
#include "zelda3d_cmab.h"

#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

enum AnimType : uint32_t {
    kAnimTranslation = 1,
    kAnimTexturePalette = 2,
    kAnimDiffuse = 3,
    kAnimConstColor = 4,
    kAnimRotation = 5,
    kAnimScale = 6,
    // 7..10 = Ambient/Spec0/Spec1/Emission — not needed yet, parsed as generic 4-track colors.
};

enum TrackType : uint32_t {
    kTrackLinear = 1,
    kTrackHermite = 2,
    kTrackInteger = 3,
};

struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
    float tangentIn = 0.0f;
    float tangentOut = 0.0f;
};

struct Track {
    uint32_t type = kTrackLinear;
    std::vector<Keyframe> frames;

    // Mirrors tools/cmab.py Track.sample() exactly, including the Hermite reset-tangent
    // (Δt==1 -> hold k0.value) special case.
    float sample(float frame) const {
        if (frames.empty()) return 0.0f;
        int idx1 = -1;
        for (size_t i = 0; i < frames.size(); i++) {
            if (frame < frames[i].time) { idx1 = (int)i; break; }
        }
        if (idx1 == 0) return frames[0].value;
        if (idx1 < 0) return frames.back().value;
        const Keyframe& k0 = frames[idx1 - 1];
        const Keyframe& k1 = frames[idx1];
        if (type == kTrackInteger) return k0.value;
        float dt = k1.time - k0.time;
        float t = (dt != 0.0f) ? (frame - k0.time) / dt : 0.0f;
        if (type == kTrackHermite && dt != 1.0f) {
            float length = dt;
            float s0 = k0.tangentOut * length;
            float s1 = k1.tangentIn * length;
            float t2 = t * t, t3 = t2 * t;
            float h00 = 2 * t3 - 3 * t2 + 1;
            float h10 = t3 - 2 * t2 + t;
            float h01 = -2 * t3 + 3 * t2;
            float h11 = t3 - t2;
            return h00 * k0.value + h10 * s0 + h01 * k1.value + h11 * s1;
        }
        return k0.value + (k1.value - k0.value) * t;
    }
};

struct AnimEntry {
    uint32_t animType = 0;
    uint32_t materialIndex = 0;
    uint32_t channelIndex = 0;
    Track tracks[4]; // 0..1 used by Translation/Scale (U,V); 0..3 by the color types (R,G,B,A)
    bool trackPresent[4] = { false, false, false, false };
};

struct Cmab {
    uint32_t duration = 0;
    uint32_t loopMode = 0;
    std::vector<AnimEntry> entries;
};

uint32_t rd32(const uint8_t* d, size_t off) {
    return (uint32_t)d[off] | ((uint32_t)d[off + 1] << 8) | ((uint32_t)d[off + 2] << 16) |
           ((uint32_t)d[off + 3] << 24);
}
uint16_t rd16(const uint8_t* d, size_t off) {
    return (uint16_t)d[off] | ((uint16_t)d[off + 1] << 8);
}
float rdF32(const uint8_t* d, size_t off) {
    float f;
    uint32_t bits = rd32(d, off);
    std::memcpy(&f, &bits, 4);
    return f;
}

// Parse a track header at `off` (track+0x00 type, +0x04 keyframe count, +0x08/0x0C
// timeStart/timeEnd, +0x10.. keyframes). Returns false (leaves *out untouched) if malformed.
bool parseTrack(const uint8_t* d, size_t size, size_t off, Track* out) {
    if (off + 0x10 > size) return false;
    uint32_t type = rd32(d, off + 0x00);
    uint32_t nkf = rd32(d, off + 0x04);
    if (nkf == 0) return false;
    size_t stride = (type == kTrackHermite) ? 0x10 : 0x08;
    size_t idx = off + 0x10;
    if (idx + (size_t)nkf * stride > size) return false;
    out->type = type;
    out->frames.resize(nkf);
    for (uint32_t i = 0; i < nkf; i++) {
        Keyframe& k = out->frames[i];
        k.time = (float)rd32(d, idx);
        k.value = rdF32(d, idx + 4);
        if (type == kTrackHermite) {
            k.tangentIn = rdF32(d, idx + 8);
            k.tangentOut = rdF32(d, idx + 12);
        }
        idx += stride;
    }
    return true;
}

bool parseMmad(const uint8_t* d, size_t size, size_t off, AnimEntry* out) {
    if (off + 0x0C > size || std::memcmp(d + off, "mmad", 4) != 0) return false;
    out->animType = rd32(d, off + 0x04);
    out->materialIndex = rd32(d, off + 0x08);
    size_t trackTable = off + 0x0C;
    bool hasChannel = (out->animType == kAnimTranslation || out->animType == kAnimRotation ||
                        out->animType == kAnimScale || out->animType == kAnimTexturePalette ||
                        out->animType == kAnimConstColor);
    if (hasChannel) {
        if (trackTable + 4 > size) return false;
        out->channelIndex = rd32(d, trackTable);
        trackTable += 4;
    }
    int ntracks;
    switch (out->animType) {
        case kAnimTranslation:
        case kAnimScale: ntracks = 2; break;
        case kAnimRotation:
        case kAnimTexturePalette: ntracks = 1; break;
        default: ntracks = 4; break; // color types: R,G,B,A
    }
    for (int i = 0; i < ntracks && i < 4; i++) {
        size_t tOff = trackTable + (size_t)i * 2;
        if (tOff + 2 > size) continue;
        uint16_t rel = rd16(d, tOff);
        if (rel == 0) continue;
        out->trackPresent[i] = parseTrack(d, size, off + rel, &out->tracks[i]);
    }
    return true;
}

bool parseCmab(const uint8_t* d, size_t size, Cmab* out) {
    if (size < 0x38 || std::memcmp(d, "cmab", 4) != 0) return false;
    if (rd32(d, 0x04) != 1) return false;             // subversion
    if (rd32(d, 0x10) != 1) return false;              // chunk count
    if (rd32(d, 0x14) != 0x20) return false;            // chunk location
    if (rd32(d, 0x20) != 0xFFFFFFFF) return false;      // chunk type marker
    out->duration = rd32(d, 0x24);
    out->loopMode = rd32(d, 0x28);
    size_t mads = 0x34;
    if (mads + 8 > size || std::memcmp(d + mads, "mads", 4) != 0) return false;
    uint32_t nanim = rd32(d, mads + 0x04);
    size_t tbl = mads + 0x08;
    out->entries.reserve(nanim);
    for (uint32_t i = 0; i < nanim; i++) {
        if (tbl + 4 > size) break;
        uint32_t rel = rd32(d, tbl);
        tbl += 4;
        AnimEntry e;
        if (parseMmad(d, size, mads + rel, &e)) out->entries.push_back(std::move(e));
    }
    return true;
}

// loopMode==Once holds the final keyframe's value past `duration` (title_logo_fireglow_cmab.md
// §2: the fire flicker plays once during the fade-in flourish then freezes). loopMode==Repeat
// wraps. `duration==0` (malformed) is treated as "never clamp" to avoid a div/mod by zero.
float clampFrame(const Cmab& c, float frame) {
    if (c.duration == 0) return frame;
    if (c.loopMode == 1) { // Repeat
        float d = (float)c.duration;
        float f = std::fmod(frame, d);
        return (f < 0.0f) ? f + d : f;
    }
    return std::clamp(frame, 0.0f, (float)c.duration);
}

const AnimEntry* findEntry(const Cmab& c, uint32_t animType, int materialIndex, int channelIndex) {
    for (const auto& e : c.entries) {
        if (e.animType == animType && (int)e.materialIndex == materialIndex &&
            (int)e.channelIndex == channelIndex) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

extern "C" void* Zelda3D_CmabParse(const uint8_t* data, size_t size) {
    if (!data || size == 0) return nullptr;
    auto* c = new Cmab();
    if (!parseCmab(data, size, c)) { delete c; return nullptr; }
    return c;
}

extern "C" void Zelda3D_CmabFree(void* handle) {
    delete reinterpret_cast<Cmab*>(handle);
}

extern "C" int Zelda3D_CmabDuration(void* handle) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    return c ? (int)c->duration : 0;
}

extern "C" int Zelda3D_CmabLoopMode(void* handle) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    return c ? (int)c->loopMode : 0;
}

extern "C" int Zelda3D_CmabSampleTranslationV(void* handle, int materialIndex, int channelIndex,
                                               float frame, float* outV) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    if (!c || !outV) return 0;
    const AnimEntry* e = findEntry(*c, kAnimTranslation, materialIndex, channelIndex);
    if (!e || !e->trackPresent[1]) return 0;
    *outV = e->tracks[1].sample(clampFrame(*c, frame));
    return 1;
}

extern "C" int Zelda3D_CmabSampleTranslationUV(void* handle, int materialIndex, int channelIndex,
                                                float frame, float* outU, float* outV) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    if (!c || (!outU && !outV)) return 0;
    const AnimEntry* e = findEntry(*c, kAnimTranslation, materialIndex, channelIndex);
    if (!e) return 0;
    float f = clampFrame(*c, frame);
    if (outU) *outU = e->trackPresent[0] ? e->tracks[0].sample(f) : 0.0f;
    if (outV) *outV = e->trackPresent[1] ? e->tracks[1].sample(f) : 0.0f;
    return 1;
}

extern "C" int Zelda3D_CmabSampleTexturePalette(void* handle, int materialIndex, int channelIndex,
                                                  float frame, int* outIndex) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    if (!c || !outIndex) return 0;
    const AnimEntry* e = findEntry(*c, kAnimTexturePalette, materialIndex, channelIndex);
    if (!e || !e->trackPresent[0]) return 0;
    *outIndex = static_cast<int>(e->tracks[0].sample(clampFrame(*c, frame)));
    return 1;
}

extern "C" int Zelda3D_CmabSampleConstColorRGB(void* handle, int materialIndex, int channelIndex,
                                                float frame, float* rgb3) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    if (!c || !rgb3) return 0;
    const AnimEntry* e = findEntry(*c, kAnimConstColor, materialIndex, channelIndex);
    if (!e) return 0;
    float f = clampFrame(*c, frame);
    for (int i = 0; i < 3; i++) rgb3[i] = e->trackPresent[i] ? e->tracks[i].sample(f) : 0.0f;
    return 1;
}

extern "C" int Zelda3D_CmabSampleConstColorRGBA(void* handle, int materialIndex, int channelIndex,
                                                 float frame, float* rgba4) {
    Cmab* c = reinterpret_cast<Cmab*>(handle);
    if (!c || !rgba4) return 0;
    const AnimEntry* e = findEntry(*c, kAnimConstColor, materialIndex, channelIndex);
    if (!e) return 0;
    float f = clampFrame(*c, frame);
    for (int i = 0; i < 4; i++) rgba4[i] = e->trackPresent[i] ? e->tracks[i].sample(f) : 0.0f;
    return 1;
}
