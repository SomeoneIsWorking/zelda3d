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
    size_t bdq = 0;
    if (!LocateTitleCs(d, len, &bdq)) {
        fprintf(stderr, "[Zelda3D] title cs: no ' BDQ' stream in spot99_info.zsi\n");
        free(d);
        return 0;
    }
    sEndFrame = S32(d, bdq + 0xC);
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
