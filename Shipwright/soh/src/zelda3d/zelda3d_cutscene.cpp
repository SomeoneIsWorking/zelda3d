// OoT3D " BDQ" cutscene playback — title-demo camera (OP97 spline block).
//
// This is a literal port of the RE'd OoT3D functions:
//   FUN_002c5ba0 case 0x97 — segment select by frame range
//   FUN_0033cb90            — per-frame camera eval (defaults + tracks)
//   FUN_003087a4            — Grezzo keyframe curve (linear/hermite/step)
// Reference implementation + verification: tools/oot3d_cs_camera.py
// (byte-exact vs live Az: |d_eye|=0.00, |d_dir|<=0.0002 over 300 frames).
// Derivation trail: debug_journal/2026-07-07-title-cs-spot99-format-solved.md
// and 2026-07-07-op97-camera-decode-verified.md.

#include "zelda3d_cutscene.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// zelda3d_model.cpp — reads a romfs file from the OoT3D ROM.
extern "C" uint8_t* Zelda3D_RomReadAlloc(const char* path, size_t* outSize);

namespace {

constexpr float kPosScale = 40.0f;       // fRam0033ce70: curve/default pos units are 1/40 world
constexpr float kRadToDeg = 57.29578f;   // fRam0033ce6c

struct Curve {
    uint8_t interp = 0;                  // 1 linear, 2 hermite, 3 step
    struct Key { int32_t frame; float value, tanIn, tanOut; };
    std::vector<Key> keys;

    float Eval(float t) const {
        const int n = (int)keys.size();
        if (n == 0) return 0.0f;
        if (n == 1) return keys[0].value;
        int idx = 0;
        while (idx < n && !(keys[idx].frame >= t)) idx++;
        if (idx == 0) return keys[0].value;
        if (idx == n) return keys[n - 1].value;
        const Key& k0 = keys[idx - 1];
        const Key& k1 = keys[idx];
        if (interp == 3) return k0.value;
        if (interp == 1)
            return k0.value + (k1.value - k0.value) *
                   (t - k0.frame) / (float)(k1.frame - k0.frame);
        // hermite — exact FUN_003087a4 form
        const float d = (float)(k1.frame - k0.frame);
        const float u = (t - k0.frame) / d;
        return k0.value + (k0.value - k1.value) * (u * 2.0f - 3.0f) * u * u +
               (t - k0.frame) * (u - 1.0f) *
                   ((u - 1.0f) * k0.tanOut + u * k1.tanIn);
    }
};

struct Track {
    uint8_t type = 0;                    // 1 eye.xyz, 2 at.xyz, 3 roll, 7 fov, 8 misc
    Curve chan[3];
    bool hasChan[3] = { false, false, false };
};

struct Segment {
    int32_t start = 0, end = 0;
    float eyeDef[3] = {}, atDef[3] = {}; // seg+0x18 = EYE, seg+0x24 = AT (verified vs Az)
    float rollRad = 0.0f, fovRad = 0.0f;
    std::vector<Track> tracks;
};

struct CamSpline {
    std::vector<Segment> segments;
    bool ok = false;
};

CamSpline sSpline;

// op-0x0a player/rider cue records — the N64 CsCmdActorAction 48-byte
// shape: {u16 action, u16 start, u16 end, u16 rot[3], s32 p0[3], s32 p1[3],
// f32 extra[3]}. Verified against Az: the live path_node pointers pinned in
// docs/title_writer_chains.md point AT these records inside the loaded ZSI.
struct RiderCue {
    uint16_t action;
    uint16_t start, end;
    int16_t yaw;                        // rot[1] binang
    float p0[3], p1[3];
};
std::vector<RiderCue> sRiderCues;

// op-0x8c time-of-day cues: fires when csFrame == frame; value derived per
// FUN_002c5ba0 case 0x8c: s16 = (int)(hours*60*45.511) + (int)((min+1)*45.511)
// (45.511 = 0x10000/1440 = daytime units per minute; 60.0 = DAT_002c5ff4,
// 45.511 = DAT_002c5ffc). Title: 4:01 AM at f=0 and f=301.
struct TimeCue { int frame; uint16_t daytime; };
std::vector<TimeCue> sTimeCues;

// spot99 scene light settings (ZSI cmd 0x0F), raw 28-byte entries — kept
// for reference/actors; NOT what the title blends (see sTitlePal below).
std::vector<uint8_t> sLightSlotsRaw;
int sLightSlotCount = 0;

// THE title palette: 4 x 28-byte entries immediately BEFORE the " BDQ" cs
// (zsi+0x34B8; the live runtime ptr [play+0x3230] points here — verified,
// and blended output value-matched over 5 dayTime samples). Runtime layout
// (pinned by regression, debug_journal/2026-07-07-title-lighting-solved.md):
//   +0x00 f32 fogEnd   +0x04 f32 drawDist   +0x08 u16 fogNear-ish
//   +0x0A u8 ambient[3]   +0x0D s8 light1Dir[3]  +0x10 u8 light1Color[3]
//   +0x13 s8 light2Dir[3] +0x16 u8 light2Color[3] +0x19 u8 fogColor[3]
// Schedule slots 0..3 index this table DIRECTLY (no metadata bias).
struct TitleLightEntry {
    float fogEnd, drawDist;
    uint16_t fogNear;
    uint8_t amb[3]; int8_t l1dir[3]; uint8_t l1col[3];
    int8_t l2dir[3]; uint8_t l2col[3]; uint8_t fogCol[3];
};
TitleLightEntry sTitlePal[4];
bool sTitlePalOk = false;

int sEndFrame = 0;
int sFrame = 0;
int sLoadState = 0;                      // 0 not tried, 1 ok, -1 failed

uint32_t U32(const uint8_t* d, size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; }
int32_t  S32(const uint8_t* d, size_t o) { int32_t v; memcpy(&v, d + o, 4); return v; }
int16_t  S16(const uint8_t* d, size_t o) { int16_t v; memcpy(&v, d + o, 2); return v; }
uint32_t U32BE(const uint8_t* d, size_t o) {
    return ((uint32_t)d[o] << 24) | ((uint32_t)d[o+1] << 16) |
           ((uint32_t)d[o+2] << 8) | d[o+3];
}
float F32(const uint8_t* d, size_t o) { float v; memcpy(&v, d + o, 4); return v; }

bool ParseCurve(const uint8_t* d, size_t len, size_t off, Curve* out) {
    if (off + 0x10 > len) return false;
    out->interp = d[off];
    const int32_t count = S32(d, off + 4);
    if (count <= 0 || count > 4096) return false;
    const size_t ksz = (out->interp == 2) ? 16 : 8;
    if (off + 0x10 + count * ksz > len) return false;
    out->keys.resize(count);
    size_t p = off + 0x10;
    for (int i = 0; i < count; i++, p += ksz) {
        Curve::Key& k = out->keys[i];
        k.frame = S32(d, p);
        k.value = F32(d, p + 4);
        k.tanIn = (out->interp == 2) ? F32(d, p + 8) : 0.0f;
        k.tanOut = (out->interp == 2) ? F32(d, p + 12) : 0.0f;
    }
    return true;
}

bool ParseSpline(const uint8_t* d, size_t len, size_t payload, CamSpline* out) {
    if (payload + 0x18 > len) return false;
    if (memcmp(d + payload, "ccb", 3) != 0 || U32(d, payload + 4) != 3) return false;
    const int32_t segCount = S32(d, payload + 0x10);
    if (segCount <= 0 || segCount > 256) return false;
    for (int i = 0; i < segCount; i++) {
        const size_t so = payload + S32(d, payload + 0x18 + i * 4);
        if (so + 0x4C > len) return false;
        Segment seg;
        seg.start = S32(d, so + 8);
        seg.end = S32(d, so + 0xC);
        for (int j = 0; j < 3; j++) {
            seg.eyeDef[j] = F32(d, so + 0x18 + j * 4);
            seg.atDef[j] = F32(d, so + 0x24 + j * 4);
        }
        seg.rollRad = F32(d, so + 0x30);
        seg.fovRad = F32(d, so + 0x3C);
        const int32_t tb = S32(d, so + 0x48);
        if (tb) {
            const size_t tbase = so + tb;
            if (tbase + 8 > len) return false;
            const int32_t tcount = S32(d, tbase + 4);
            if (tcount < 0 || tcount > 64) return false;
            for (int t = 0; t < tcount; t++) {
                const size_t to = tbase + S32(d, tbase + 8 + t * 4);
                if (to + 0x0E > len) return false;
                Track tr;
                tr.type = d[to + 4];
                const int nch = (tr.type == 1 || tr.type == 2) ? 3 : 1;
                for (int c = 0; c < nch; c++) {
                    const int16_t rel = S16(d, to + 8 + c * 2);
                    if (rel && ParseCurve(d, len, to + rel, &tr.chan[c]))
                        tr.hasChan[c] = true;
                }
                seg.tracks.push_back(std::move(tr));
            }
        }
        out->segments.push_back(std::move(seg));
    }
    out->ok = !out->segments.empty();
    return out->ok;
}

// Parse the scene's cmd-0x0F environment light settings into sLightSlotsRaw.
void ParseLightSettings(const uint8_t* d, size_t len) {
    size_t off = 16;
    while (off + 8 <= len) {
        const uint32_t cmd = U32BE(d, off);
        const uint8_t type = (cmd >> 24) & 0xFF;
        const uint8_t count = (cmd >> 16) & 0xFF;
        const uint32_t ptr = U32(d, off + 4);
        if (type == 0x0F && ptr + count * 28u <= len) {
            sLightSlotsRaw.assign(d + ptr, d + ptr + count * 28u);
            sLightSlotCount = count;
        }
        off += 8;
        if (type == 0x14) break;
    }
}

// Locate the " BDQ" cs inside a scene _info.zsi: scene cmd 0x18 (alt
// headers) -> entry[0] must be inline (0x17, ptr) -> ptr + 0x10 = " BDQ".
// (The 16 bytes at ptr are the container prefix, e.g. "OHHH…".)
bool LocateTitleCs(const uint8_t* d, size_t len, size_t* bdqOff) {
    if (len < 0x20 || memcmp(d, "ZSI", 3) != 0) return false;
    size_t off = 16;
    uint32_t altPtr = 0;
    while (off + 8 <= len) {
        const uint32_t cmd = U32BE(d, off);
        const uint32_t ptr = U32(d, off + 4);
        const uint8_t type = (cmd >> 24) & 0xFF;
        if (type == 0x18) altPtr = ptr;
        off += 8;
        if (type == 0x14) break;
    }
    if (!altPtr || altPtr + 8 > len) return false;
    const uint32_t a = U32(d, altPtr);
    const uint32_t b = U32(d, altPtr + 4);
    if (a != 0x17 || b + 0x14 > len) return false;
    const size_t bdq = b + 0x10;
    if (memcmp(d + bdq, " BDQ", 4) != 0) return false;
    *bdqOff = bdq;
    return true;
}

} // namespace

extern "C" int Zelda3D_TitleCsLoad(void) {
    if (sLoadState) return sLoadState > 0;
    sLoadState = -1;
    size_t len = 0;
    uint8_t* d = Zelda3D_RomReadAlloc("/scene/spot99_info.zsi", &len);
    if (!d) {
        fprintf(stderr, "[Zelda3D] title cs: spot99_info.zsi not readable\n");
        return 0;
    }
    ParseLightSettings(d, len);
    size_t bdq = 0;
    if (!LocateTitleCs(d, len, &bdq)) {
        fprintf(stderr, "[Zelda3D] title cs: no ' BDQ' stream in spot99_info.zsi\n");
        free(d);
        return 0;
    }
    sEndFrame = S32(d, bdq + 0xC);
    // Title palette: the 4 x 28B entries directly before the " BDQ".
    if (bdq >= 4 * 28) {
        for (int i = 0; i < 4; i++) {
            const uint8_t* e = d + bdq - 4 * 28 + i * 28;
            TitleLightEntry* o = &sTitlePal[i];
            memcpy(&o->fogEnd, e, 4);
            memcpy(&o->drawDist, e + 4, 4);
            memcpy(&o->fogNear, e + 8, 2);
            for (int j = 0; j < 3; j++) {
                o->amb[j] = e[0x0A + j];
                o->l1dir[j] = (int8_t)e[0x0D + j];
                o->l1col[j] = e[0x10 + j];
                o->l2dir[j] = (int8_t)e[0x13 + j];
                o->l2col[j] = e[0x16 + j];
                o->fogCol[j] = e[0x19 + j];
            }
        }
        sTitlePalOk = true;
    }
    // walk the command stream for OP97 (the camera spline block)
    const int32_t cmdCount = S32(d, bdq + 8);
    size_t p = bdq + 0x10;
    bool found = false;
    for (int i = 0; i < cmdCount && p + 8 <= len; i++) {
        const int32_t op = S32(d, p);
        if (op == -1) break;
        if (op == 0x97) {
            found = ParseSpline(d, len, p + 8, &sSpline);
            break;
        }
        if (op == 0x8c) {               // time-of-day cues (12-byte records)
            const int32_t cnt = S32(d, p + 4);
            for (int r = 0; r < cnt && p + 8 + (r + 1) * 12 <= len; r++) {
                const size_t ro = p + 8 + (size_t)r * 12;
                uint16_t f;
                memcpy(&f, d + ro + 2, 2);
                const float kPerMin = 45.511f;   // 0x10000/1440
                const int hours = d[ro + 6], mins = d[ro + 7];
                const uint16_t t = (uint16_t)((int16_t)(int)(hours * 60.0f * kPerMin) +
                                              (int16_t)(int)((mins + 1) * kPerMin));
                sTimeCues.push_back({ (int)f, t });
            }
            p += 8 + (size_t)S32(d, p + 4) * 12;
            continue;
        }
        if (op == 0x0a) {               // player (rider) cue track
            const int32_t cnt = S32(d, p + 4);
            for (int r = 0; r < cnt && p + 8 + (r + 1) * 48 <= len; r++) {
                const size_t ro = p + 8 + (size_t)r * 48;
                RiderCue cue;
                memcpy(&cue.action, d + ro, 2);
                memcpy(&cue.start, d + ro + 2, 2);
                memcpy(&cue.end, d + ro + 4, 2);
                cue.yaw = S16(d, ro + 8);
                for (int j = 0; j < 3; j++) {
                    cue.p0[j] = (float)S32(d, ro + 12 + j * 4);
                    cue.p1[j] = (float)S32(d, ro + 24 + j * 4);
                }
                sRiderCues.push_back(cue);
            }
            p += 8 + (cnt > 0 ? (size_t)cnt * 48 : 0);
            continue;
        }
        // stride rules from FUN_002c5ba0 (subset needed for spot99's stream;
        // full table in tools/walk_oot3d_cs.py)
        if (op == 1 || op == 2 || op == 5 || op == 6) {
            size_t q = p + 12;
            while (q + 16 <= len && d[q] != 0xFF) q += 16;
            p = q + 16;
        } else if (op == 7 || op == 8) {
            p += 28;
        } else if (op == 0x8c) {
            p += 8 + (size_t)S32(d, p + 4) * 12;
        } else if (op == 0x96) {
            p += 12 + (size_t)S16(d, p + 10) * 32;
        } else if (op == 1000) {
            p += 16;
        } else {
            const int32_t cnt = S32(d, p + 4);
            p += 8 + (cnt > 0 ? (size_t)cnt * 48 : 0);
        }
    }
    free(d);
    fprintf(stderr, "[Zelda3D] title cs: %zu rider cues\n", sRiderCues.size());
    if (!found) {
        fprintf(stderr, "[Zelda3D] title cs: OP97 spline block not found\n");
        return 0;
    }
    fprintf(stderr, "[Zelda3D] title cs loaded: %zu camera segments, end_frame=%d\n",
            sSpline.segments.size(), sEndFrame);
    sLoadState = 1;
    return 1;
}

extern "C" int Zelda3D_TitleCsEndFrame(void) { return sEndFrame; }

extern "C" int Zelda3D_TitleCsCamera(int frame, float eye[3], float at[3],
                                     float up[3], float* fovDeg) {
    if (sLoadState <= 0 || !sSpline.ok) return 0;
    const Segment* seg = nullptr;
    for (const Segment& s : sSpline.segments) {
        if (s.start < frame && frame < s.end) { seg = &s; break; }
    }
    if (!seg) return 0;
    float e[3], a[3];
    memcpy(e, seg->eyeDef, sizeof(e));
    memcpy(a, seg->atDef, sizeof(a));
    float rollRad = seg->rollRad;
    float fovRad = seg->fovRad;
    const float t = (float)frame;
    for (const Track& tr : seg->tracks) {
        switch (tr.type) {
            case 1:
                for (int j = 0; j < 3; j++)
                    if (tr.hasChan[j]) e[j] = tr.chan[j].Eval(t);
                break;
            case 2:
                for (int j = 0; j < 3; j++)
                    if (tr.hasChan[j]) a[j] = tr.chan[j].Eval(t);
                break;
            case 3:
                if (tr.hasChan[0]) rollRad = tr.chan[0].Eval(t);
                break;
            case 7:
                if (tr.hasChan[0]) fovRad = tr.chan[0].Eval(t);
                break;
            default:
                break;                   // type 8 (misc dist) unused by the camera
        }
    }
    for (int j = 0; j < 3; j++) {
        eye[j] = e[j] * kPosScale;
        at[j] = a[j] * kPosScale;
    }
    // up from roll about the view dir; sign verified against Az's live up
    // (roll=0.0873 rad -> up=(0.212,0.977,-0.013) vs Az (0.212,0.977,-0.014)).
    float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    float m = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (m < 1e-6f) return 0;
    for (float& v : f) v /= m;
    float r[3] = { f[1]*0 - f[2]*1, f[2]*0 - f[0]*0, f[0]*1 - f[1]*0 }; // cross(f, worldUp)
    m = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (m < 1e-6f) { r[0] = 1; r[1] = 0; r[2] = 0; m = 1; }
    for (float& v : r) v /= m;
    const float u0[3] = { r[1]*f[2] - r[2]*f[1],                       // cross(r, f)
                          r[2]*f[0] - r[0]*f[2],
                          r[0]*f[1] - r[1]*f[0] };
    const float c = cosf(rollRad), s = sinf(rollRad);
    for (int j = 0; j < 3; j++) up[j] = u0[j] * c - r[j] * s;
    *fovDeg = fovRad * kRadToDeg;
    return 1;
}

extern "C" int Zelda3D_TitleCsFrame(void) { return sFrame; }
extern "C" void Zelda3D_TitleCsSetFrame(int frame) {
    sFrame = (sEndFrame > 0) ? frame % sEndFrame : frame;
    if (sFrame < 0) sFrame = 0;
}
extern "C" int Zelda3D_TitleCsAdvance(void) {
    sFrame++;
    if (sEndFrame > 0 && sFrame >= sEndFrame) sFrame = 0;
    return sFrame;
}

// Active rider cue for a cs frame (start <= f < end). Returns 1 and fills
// outputs; 0 when no cue covers the frame. cueIndex identifies the cue so
// callers can detect cue changes / teleport discontinuities.
extern "C" int Zelda3D_TitleCsRiderCue(int frame, int* cueIndex,
                                       float p0[3], float p1[3],
                                       int* startF, int* endF,
                                       int16_t* yawBinang) {
    if (sLoadState <= 0) return 0;
    for (size_t i = 0; i < sRiderCues.size(); i++) {
        const RiderCue& c = sRiderCues[i];
        if (c.start <= frame && frame < c.end) {
            *cueIndex = (int)i;
            memcpy(p0, c.p0, sizeof(c.p0));
            memcpy(p1, c.p1, sizeof(c.p1));
            *startF = c.start;
            *endF = c.end;
            *yawBinang = c.yaw;
            return 1;
        }
    }
    return 0;
}

// Time-of-day for a cs frame: the last op-0x8c cue sets the anchor, then
// time FLOWS at 6 dayTime units per cs frame (measured live: d(t)/d(f) =
// 6.000 across the whole demo; scratch/time_slope.py). The title's dawn
// progression (4:01 AM -> ~9 AM over the 2400-frame loop) comes from this.
extern "C" int Zelda3D_TitleCsTimeOfDay(int frame, uint16_t* outDayTime) {
    if (sLoadState <= 0) return 0;
    int bestF = -1;
    uint16_t bestT = 0;
    for (const auto& c : sTimeCues) {
        if (c.frame <= frame && c.frame >= bestF) {
            bestF = c.frame;
            bestT = c.daytime;
        }
    }
    if (bestF < 0) return 0;
    *outDayTime = (uint16_t)(bestT + 6 * (frame - bestF));
    return 1;
}

// spot99's raw ZSI cmd-0x0F light-settings entries (28 bytes each, entry 0 =
// metadata like every scene; caller applies the same +1 slot bias as the
// generated kZelda3dSceneLighting rows).
extern "C" int Zelda3D_TitleCsLightSlotsRaw(const uint8_t** outSlots, int* outCount) {
    if (sLoadState <= 0 || sLightSlotsRaw.empty()) return 0;
    *outSlots = sLightSlotsRaw.data();
    *outCount = sLightSlotCount;
    return 1;
}

// OoT3D time-based light schedule, config 0 — static engine data at
// [pool 0x0045e168] = 0x00531EFC in code.bin (rows of 9 x 6-byte spans
// {u16 startTime, u16 endTime, u8 slotFrom, u8 slotTo}; row = env[0x21],
// which is 0 at title). Same mechanism as N64 z_kankyo's
// sTimeBasedLightConfigs; consumer decomp: FUN_0045dd30 @0x0045e4a8
// (blend weight = (time - start) / (end - start)).
namespace {
struct LightSpan { uint16_t start, end; uint8_t from, to; };
const LightSpan kTitleLightSchedule[9] = {
    { 0x0000, 0x2AAC, 3, 3 }, { 0x2AAC, 0x4000, 3, 0 },
    { 0x4000, 0x4AAB, 0, 0 }, { 0x4AAB, 0x6000, 0, 1 },
    { 0x6000, 0xA000, 1, 1 }, { 0xA000, 0xB556, 1, 2 },
    { 0xB556, 0xC001, 2, 2 }, { 0xC001, 0xD556, 2, 3 },
    { 0xD556, 0xFFFF, 3, 3 },
};
} // namespace

// Resolve the title light-schedule span for a daytime value. Slots are
// RUNTIME slots (palette entry = slot + 1, entry 0 being metadata).
extern "C" int Zelda3D_TitleCsLightBlend(uint16_t daytime, int* slotFrom,
                                         int* slotTo, float* weight) {
    for (const LightSpan& sp : kTitleLightSchedule) {
        if (sp.start <= daytime && (daytime < sp.end || sp.end == 0xFFFF)) {
            *slotFrom = sp.from;
            *slotTo = sp.to;
            const float d = (float)(sp.end - sp.start);
            *weight = (d > 0.0f) ? (float)(daytime - sp.start) / d : 0.0f;
            return 1;
        }
    }
    return 0;
}

// Blend the title palette per the 3DS schedule at a dayTime. Fills the
// EnvLightSettings-shaped fields the z_kankyo override consumes. Slots
// index sTitlePal directly. Returns 0 when the palette isn't loaded.
extern "C" int Zelda3D_TitleCsBlendedLight(uint16_t daytime,
                                           uint8_t amb[3], int8_t l1dir[3], uint8_t l1col[3],
                                           int8_t l2dir[3], uint8_t l2col[3], uint8_t fogCol[3]) {
    if (!sTitlePalOk) return 0;
    int sf, st;
    float w;
    if (!Zelda3D_TitleCsLightBlend(daytime, &sf, &st, &w)) return 0;
    const TitleLightEntry& a = sTitlePal[sf & 3];
    const TitleLightEntry& b = sTitlePal[st & 3];
    for (int j = 0; j < 3; j++) {
        amb[j]    = (uint8_t)(a.amb[j]    + (b.amb[j]    - a.amb[j])    * w + 0.5f);
        l1col[j]  = (uint8_t)(a.l1col[j]  + (b.l1col[j]  - a.l1col[j])  * w + 0.5f);
        l2col[j]  = (uint8_t)(a.l2col[j]  + (b.l2col[j]  - a.l2col[j])  * w + 0.5f);
        fogCol[j] = (uint8_t)(a.fogCol[j] + (b.fogCol[j] - a.fogCol[j]) * w + 0.5f);
        float d1 = a.l1dir[j] + (b.l1dir[j] - a.l1dir[j]) * w;
        float d2 = a.l2dir[j] + (b.l2dir[j] - a.l2dir[j]) * w;
        l1dir[j] = (int8_t)(d1 >= 0 ? d1 + 0.5f : d1 - 0.5f);
        l2dir[j] = (int8_t)(d2 >= 0 ? d2 + 0.5f : d2 - 0.5f);
    }
    return 1;
}
