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
#include "../core/zelda3d_log.h"

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

// op-0x03 misc triggers (one entry per 48-byte record): the decomp
// (`oot3d-decomp/docs/title_gamestate_driver.md` §3) pins sub-op 0x1e =
// Flags_SetEnv(play,3) (logo fade-in trigger) and sub-op 0x1f = Flags_SetEnv
// (play,4) (logo fade-out trigger) — byte-confirmed in spot99's BDQ stream
// at +0x36a0/+0x36d8. Other sub-ops in this cs (e.g. 0x01 at frames 0..90,
// 0x04 in op-0x7c at 2310..2460) are stored too; consumers select by type.
struct MiscCue { uint16_t sub; uint16_t start, end; };
std::vector<MiscCue> sMiscCues;

// op-0x7c transition/fade window: title cs uses sub-op 0x04 over frames
// 2310..2460 — the screen-level fade straddling the loop boundary.
struct FadeCue { uint16_t sub; uint16_t start, end; };
std::vector<FadeCue> sFadeCues;

// op-0x3e8 destination marker — purely a "this is the cs end" sentinel in N64
// (z_demo.c case 0x3E8 just sets state = CS_STATE_UNSKIPPABLE_INIT). The loop
// point itself comes from the " BDQ" header's end_frame (2400 for title), not
// from this command's payload. Tracked here only so the loader can log it.
bool sHasDest = false;

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

// The title cs interpreter's own cursor tick vs the engine's per-frame call cadence:
// OoT3D's real title cutscene runs its cs cursor at HALF the rate SoH's port previously
// used. Verified empirically (2026-07-09) against the live Az oracle: reading Az's own
// title camera eye (fixed VA, byte-exact against the ported OP97 spline evaluator —
// see 2026-07-07-op97-camera-decode-verified.md) at a dozen retro_run()-step checkpoints
// and inverse-matching it against Zelda3D_TitleCsCamera's frame table gives an EXACT
// az_cs_frame = 88 + 0.5*az_step relationship (residual < 0.1 world units, i.e. float
// noise) across the full sampled range az_step in [0,600]. Meanwhile SoH's own ported
// cursor (this file, pre-fix) advanced 1 cs-frame per engine tick — an exact 1:1,
// confirmed by probing Zelda3D_TitleCsFrame() at soh_step checkpoints (constant 231-tick
// boot offset, slope exactly 1.0). So SoH's title demo was running the cutscene at TWICE
// the real 3DS speed — a genuine RATE bug, not a fixed phase offset (this is why the
// previously-recorded content-matched anchors, debug_journal/2026-07-08-title-*-schedule-
// re.md / tools/title_ab.py's ANCHORS table, showed a SHRINKING soh-az gap with elapsed
// time: that shrinkage is the signature of two clocks running at different RATES, not a
// one-time startup lag). See debug_journal/2026-07-09-title-cs-phase-sync.md for the full
// derivation. sTickParity halves the effective advance rate; toggled every call so the
// cursor only actually increments on every OTHER call, matching the oracle's real cadence.
int sTickParity = 0;

// Residual 1 (debug_journal/2026-07-10-title-arc-closing-measurement-v4.md): even after the
// half-rate fix above, SoH's dayTime read a CONSTANT +6 units (== +1 cs-frame, at the
// established +6/frame rate) BEHIND the oracle's at three content-matched points spanning
// cs 338/588/849 (measured via `az_daytime` vs `soh_env`, scratch/task2_daytime_check.py,
// 2026-07-10-title-arc-closing-measurement-v3.md Task 2) — exact and non-drifting across a
// wide span, the signature of a fixed BOOT-PHASE deficit, not a per-tick rate/parity bug (a
// parity bug would show N-call-count-parity-dependent jitter, which the clean constant rules
// out). Root cause: `sTickParity`'s init (0) makes this cursor's very FIRST Advance() call a
// HOLD (see below) — SoH's N64-splash/boot pre-roll before TitlePresentation first activates
// consumes exactly one fewer effective cs-tick than the real 3DS's boot sequence needs before
// its own title-cs interpreter's curFrame begins advancing, so SoH's cursor starts one
// "increment slot" behind and never catches up. Fixed at the ONE shared cursor site (this
// flag + the first-call branch in Zelda3D_TitleCsAdvance below), not per-consumer: every
// reader of Zelda3D_TitleCsFrame() (camera, rider, dayTime, dome, lighting) inherits the same
// +1 correction, so they stay mutually consistent — no separate +1 offsets scattered across
// title_presentation.cpp's callers.
bool sFirstAdvance = true;

// Set by Zelda3D_TitleCsAdvance() every call: true iff sFrame actually incremented (or wrapped)
// this call, false on a sTickParity hold tick. Read by Zelda3D_TitleCsDidAdvance() — see that
// function's declaration comment in zelda3d_cutscene.h for why stateful per-tick consumers
// (the rider) need it and pure-function consumers (camera/dayTime/dome) don't.
bool sLastAdvanced = false;

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
        if (op == 0x03) {               // misc triggers (48-byte recs, sub-op @ ro)
            const int32_t cnt = S32(d, p + 4);
            for (int r = 0; r < cnt && p + 8 + (r + 1) * 48 <= len; r++) {
                const size_t ro = p + 8 + (size_t)r * 48;
                MiscCue mc;
                memcpy(&mc.sub,   d + ro,     2);
                memcpy(&mc.start, d + ro + 2, 2);
                memcpy(&mc.end,   d + ro + 4, 2);
                sMiscCues.push_back(mc);
            }
            p += 8 + (cnt > 0 ? (size_t)cnt * 48 : 0);
            continue;
        }
        if (op == 0x7c) {               // transition/fade window
            const int32_t cnt = S32(d, p + 4);
            for (int r = 0; r < cnt && p + 8 + (r + 1) * 48 <= len; r++) {
                const size_t ro = p + 8 + (size_t)r * 48;
                FadeCue fc;
                memcpy(&fc.sub,   d + ro,     2);
                memcpy(&fc.start, d + ro + 2, 2);
                memcpy(&fc.end,   d + ro + 4, 2);
                sFadeCues.push_back(fc);
            }
            p += 8 + (cnt > 0 ? (size_t)cnt * 48 : 0);
            continue;
        }
        if (op == 0x3e8) {              // loop destination marker (end of demo)
            // N64 z_demo.c case 0x3E8 is a state-flip sentinel — no payload frame
            // value to read; the loop point is the BDQ header's end_frame (sEndFrame).
            sHasDest = true;
            p += 16;
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
        } else {
            const int32_t cnt = S32(d, p + 4);
            p += 8 + (cnt > 0 ? (size_t)cnt * 48 : 0);
        }
    }
    free(d);
    fprintf(stderr, "[Zelda3D] title cs: %zu rider cues, %zu misc cues, %zu fade cues, hasDest=%d\n",
            sRiderCues.size(), sMiscCues.size(), sFadeCues.size(), (int)sHasDest);
    if (!found) {
        fprintf(stderr, "[Zelda3D] title cs: OP97 spline block not found\n");
        return 0;
    }
    fprintf(stderr, "[Zelda3D] title cs loaded: %zu camera segments, end_frame=%d\n",
            sSpline.segments.size(), sEndFrame);
    // Per-segment frame coverage — diagnoses camera-spline GAPS (frames covered by no segment fall
    // back to the static default; see debug_journal/2026-07-15-epona-title-animation.md).
    for (size_t si = 0; si < sSpline.segments.size(); ++si)
        fprintf(stderr, "[Zelda3D]   cam seg %zu: frames (%d, %d)\n",
                si, sSpline.segments[si].start, sSpline.segments[si].end);
    // Rider cue windows + actions — diagnoses which EnHorse cs func drives each frame range
    // (0x24 Move / 0x26 Rearing / 0x40 WarpMove / 0x41 WarpRearing; title_rider.cpp RiderCsFuncIdx).
    for (size_t ci = 0; ci < sRiderCues.size(); ++ci)
        fprintf(stderr, "[Zelda3D]   rider cue %zu: action=0x%02x frames (%u, %u] yaw=%d\n",
                ci, sRiderCues[ci].action, sRiderCues[ci].start, sRiderCues[ci].end,
                (int)sRiderCues[ci].yaw);
    sLoadState = 1;
    return 1;
}

extern "C" int Zelda3D_TitleCsEndFrame(void) { return sEndFrame; }

extern "C" int Zelda3D_TitleCsCamera(float frame, float eye[3], float at[3],
                                     float up[3], float* fovDeg) {
    if (sLoadState <= 0 || !sSpline.ok) return 0;
    // FREEZE past end_frame (title_sequence_full_re.md §1): the 3DS interpreter stops evaluating
    // every command once curFrame exceeds end_frame(2400) — the spline HAS authored segments past
    // 2400, but hardware never plays them. Hold the 2400 pose. (dayTime is NOT clamped — the
    // continuous day flow is the ordinary Play tick, not a cs consumer.)
    if (sEndFrame > 0 && frame > (float)sEndFrame) frame = (float)sEndFrame;
    // Segment select on the INTEGER frame (curve keys are integer-frame ranges); the fractional
    // part flows into the track Eval below for 60fps sub-frame interpolation (kanban #149).
    const int fi = (int)frame;
    // INCLUSIVE bounds. Segments are contiguous inclusive ranges (0,299)(300,929)... . The old
    // strict `s.start < frame < s.end` dropped BOTH seam frames of every boundary (299 AND 300, ...)
    // to the caller's fixed static-default camera — a jarring per-seam camera jump (the title-demo
    // "everything looks broken, only bisectable frame by frame" report; 7 seams x 2 frames).
    //
    // VERIFIED correct against the oracle's REAL view direction (the at-eye vector @0x005BE6D4+0x24,
    // decompiled in oot3d-decomp/title_basis_writer_jit_solved.md — NOT the +0x0C RIGHT vector an
    // earlier pass mislabeled "dir", which made this look wrong and got a first attempt reverted):
    //   cs320 (interior): SoH eye/fwd/up/right all match the oracle within noise.
    //   cs300 (seam, now seg1 via `<=`): SoH eye=(3921,7460) dir=(0.182,-0.323,-0.920) matches the
    //     oracle eye=(3919.7,7454.0) fwd=(0.184,-0.322,-0.929) — the oracle is at seg1's opening
    //     too. Adjacent segments differ by 1 frame so no frame matches two.
    const Segment* seg = nullptr;
    for (const Segment& s : sSpline.segments) {
        if (s.start <= fi && fi <= s.end) { seg = &s; break; }
    }
    if (!seg) return 0;
    float e[3], a[3];
    memcpy(e, seg->eyeDef, sizeof(e));
    memcpy(a, seg->atDef, sizeof(a));
    float rollRad = seg->rollRad;
    float fovRad = seg->fovRad;
    const float t = frame; // fractional — sub-frame interpolated eval
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
    // `log titlecam 1`: raw eye/at defs + evaluated + which track types the segment carries — the
    // per-frame spline evaluation trail (debug_journal/2026-07-15-epona-title-animation.md).
    if (Zelda3D_LogEnabled(Z3D_LOG_TITLECAM)) {
        char tks[64] = {0}; size_t tl = 0;
        for (const Track& tr : seg->tracks) tl += (size_t)snprintf(tks+tl, sizeof(tks)-tl, "%d ", tr.type);
        Z3D_LOG(TITLECAM, "f=%.1f seg[%d,%d] eyeDef=(%.1f,%.1f,%.1f) atDef=(%.1f,%.1f,%.1f) "
                "eyeEval=(%.1f,%.1f,%.1f) atEval=(%.1f,%.1f,%.1f) tracks=[%s]\n",
                frame, seg->start, seg->end,
                seg->eyeDef[0]*kPosScale, seg->eyeDef[1]*kPosScale, seg->eyeDef[2]*kPosScale,
                seg->atDef[0]*kPosScale, seg->atDef[1]*kPosScale, seg->atDef[2]*kPosScale,
                eye[0], eye[1], eye[2], at[0], at[1], at[2], tks);
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

// Sub-frame fraction of the CURRENT engine tick within the current cs frame. The cs cursor ticks
// once per TWO engine frames (sTickParity): parity==0 right after an increment (first engine frame
// of cs frame sFrame -> 0.0), parity==1 on the hold tick (second engine frame -> 0.5). Lets 60fps
// consumers (camera spline eval, rider render position) interpolate between 30fps cs ticks instead
// of stepping — the user-visible "everything jitters at half rate" fix (kanban #149).
extern "C" float Zelda3D_TitleCsSubframe(void) { return sTickParity ? 0.5f : 0.0f; }
extern "C" void Zelda3D_TitleCsSetFrame(int frame) {
    sFrame = frame; // no wrap — the cs is one continuous script (see Zelda3D_TitleCsAdvance)
    if (sFrame < 0) sFrame = 0;
}
extern "C" int Zelda3D_TitleCsAdvance(void) {
    // One-time boot-phase seed — see sFirstAdvance's declaration comment (residual 1) for the
    // RE trail. Consumes this call's "increment slot" itself (sTickParity left at 1, i.e. the
    // NEXT call holds) instead of falling into the normal hold-first pattern below, which
    // shifts every subsequent sFrame value by exactly +1 versus the pre-fix sequence for any
    // call count — provably (by induction: old(t) = floor(t/2), new(t) = floor(t/2)+1 for all
    // t >= 1), not just at even call counts.
    if (sFirstAdvance) {
        sFirstAdvance = false;
        sFrame = 1;
        sTickParity = 1;
        sLastAdvanced = true;   // 0 -> 1 is a real step, not a hold
        return sFrame;
    }
    // Half-rate advance — see sTickParity's declaration comment above for the RE trail.
    sTickParity ^= 1;
    if (sTickParity) {
        sLastAdvanced = false;
        return sFrame;   // hold: this engine-tick has no corresponding cs-tick on the oracle
    }
    sFrame++;
    // NO wrap at end_frame (2400): the loop assumption is FALSIFIED against the live oracle
    // (2026-07-16, scratch/decomp_agent/wrap_discriminator.py + fade_curve_sweep*): the 3DS
    // title cs is one long continuous script — camera at tick 2705 is nowhere near tick 305's
    // pose, cues are authored to 3036, dayTime flows uninterrupted (dawn -> day -> night storm)
    // to at least tick 6000 with no cursor reset and no fade-to-black anywhere. The " BDQ"
    // header's end_frame does NOT restart playback on the 3DS.
    sLastAdvanced = true;
    return sFrame;
}

extern "C" int Zelda3D_TitleCsDidAdvance(void) { return sLastAdvanced ? 1 : 0; }

// First cs frame at which a given op-0x03 misc sub-op fires, or -1 if absent.
// Sub-ops of interest (per `oot3d-decomp/docs/title_gamestate_driver.md` §3):
//   0x1e = Flags_SetEnv(play, 3) — title-logo FADE_IN trigger
//   0x1f = Flags_SetEnv(play, 4) — title-logo FADE_OUT trigger
// (Other misc sub-ops in this cs: 0x01 at frames 0..90 — startup trigger.)
extern "C" int Zelda3D_TitleCsMiscTriggerFrame(uint16_t sub) {
    if (sLoadState <= 0) return -1;
    for (const MiscCue& m : sMiscCues) {
        if (m.sub == sub) return m.start;
    }
    return -1;
}

// First op-0x7c transition/fade window (sub-op 0x04 in title cs: frames 2310
// ..2460 — the screen-level fade straddling the loop boundary). Returns 1 if
// any fade cue is present; fills *start/*end with its [start,end) frame range.
extern "C" int Zelda3D_TitleCsScreenFade(int* start, int* end) {
    if (sLoadState <= 0 || sFadeCues.empty()) return 0;
    *start = sFadeCues.front().start;
    *end   = sFadeCues.front().end;
    return 1;
}

// The op-0x3e8 destination marker = the cs end. The loop-restart frame is the
// " BDQ" header's end_frame (2400 in title cs) — exposed via
// Zelda3D_TitleCsEndFrame(); this function just reports whether the cs had a
// destination marker at all (a malformed/missing one means no loop).
extern "C" int Zelda3D_TitleCsLoopFrame(void) {
    return sHasDest ? sEndFrame : -1;
}

// Active rider cue for a cs frame. Latch semantics are the 3DS interpreter's
// own (FUN_002c5ba0, oot3d-decomp docs/cutscene_format.md +
// docs/title_rider_cs_dispatch.md):
//   - predicate `startFrame < curFrame <= endFrame` — STRICT lower bound,
//     INCLUSIVE upper bound (NOT the N64-ish [start, end) this function used
//     before 2026-07-14, which made every window fire one cs frame early);
//   - the interpreter walks the script's commands IN ORDER and every op-0x0a
//     command stores its matching record into the SAME csCtx channel slot
//     (play+0x22D8), so when two channels' windows overlap on a boundary
//     frame the LAST match in script order wins. sRiderCues preserves script
//     order, so this scan keeps the last match. The title cs depends on this:
//     at f=925 both the plain-move window [750,925] (earlier command) and the
//     1-frame warp cue [924,925] (later command) match, and the WARP must win
//     or the rider never teleports across the shot cut (live-falsified: the
//     first-match version left the rider ~2500u off-course for the rest of
//     the loop — rider_traj_green FAIL table, 2026-07-14 journal).
// Returns 1 and fills outputs; 0 when no cue covers the frame. cueIndex
// identifies the cue so callers can detect cue changes.
extern "C" int Zelda3D_TitleCsRiderCue(int frame, int* cueIndex,
                                       float p0[3], float p1[3],
                                       int* startF, int* endF,
                                       int16_t* yawBinang,
                                       uint16_t* outAction) {
    if (sLoadState <= 0) return 0;
    // FREEZE past end_frame — same clamp as Zelda3D_TitleCsCamera (cues are authored to 3036 but
    // the 3DS interpreter never plays past 2400; see title_sequence_full_re.md §1).
    if (sEndFrame > 0 && frame > sEndFrame) frame = sEndFrame;
    int found = -1;
    for (size_t i = 0; i < sRiderCues.size(); i++) {
        const RiderCue& c = sRiderCues[i];
        if (c.start < frame && frame <= c.end) {
            found = (int)i; // keep scanning: last match in script order wins
        }
    }
    if (found < 0) return 0;
    const RiderCue& c = sRiderCues[found];
    *cueIndex = found;
    memcpy(p0, c.p0, sizeof(c.p0));
    memcpy(p1, c.p1, sizeof(c.p1));
    *startF = c.start;
    *endF = c.end;
    *yawBinang = c.yaw;
    if (outAction != nullptr) {
        *outAction = c.action;
    }
    return 1;
}

// Time-of-day for a cs frame: the last op-0x8c cue sets the anchor, then
// time FLOWS at 6 dayTime units per cs frame (measured live: d(t)/d(f) =
// 6.000 across the whole demo; scratch/time_slope.py). The title's dawn
// progression (4:01 AM -> ~9 AM over the 2400-frame loop) comes from this.
extern "C" int Zelda3D_TitleCsTimeOfDay(float frame, uint16_t* outDayTime) {
    if (sLoadState <= 0) return 0;
    // Cue latch on the INTEGER frame; the fractional part only advances the +6/cs-frame drift so
    // dayTime (and everything derived: sun/moon position, dome blend, light palette) moves every
    // ENGINE frame instead of stepping per cs tick — same fractional-frame idiom as the camera
    // spline (kanban #149; the drift becomes +3/engine frame, FrameInterpolation covers the rest).
    const int fi = (int)frame;
    int bestF = -1;
    uint16_t bestT = 0;
    for (const auto& c : sTimeCues) {
        if (c.frame <= fi && c.frame >= bestF) {
            bestF = c.frame;
            bestT = c.daytime;
        }
    }
    if (bestF < 0) return 0;
    *outDayTime = (uint16_t)(bestT + (int)(6.0f * (frame - (float)bestF)));
    return 1;
}

// 3DS dome selector/blend table, `FUN_002e47c8`'s row 0 ("fine", the non-rain
// case, which is what title uses) at VA 0x0053200a
// (oot3d-decomp/docs/title_sky_dome.md §9.2/§9.5). Entry shape on the 3DS is
// {u16 start, u16 end, u8 blendFlag, u8 idx1, u16 idx2(low byte used)}; the
// blendFlag is not carried separately here because LerpWeight's own ceiling
// clamp already reproduces the flag=0 rows' "no blend" behavior (span
// nonzero, weight saturates to 1.0 for the whole row).
namespace {
struct DomeSpan { uint16_t start, end; int idx1, idx2; };
const DomeSpan kTitleDomeSchedule[9] = {
    { 0x0000, 0x2AAC, 3, 3 }, { 0x2AAC, 0x4000, 3, 0 },
    { 0x4000, 0x4AAB, 0, 0 }, { 0x4AAB, 0x6000, 0, 1 },
    { 0x6000, 0xA000, 1, 1 }, { 0xA000, 0xB556, 1, 2 },
    { 0xB556, 0xC001, 2, 2 }, { 0xC001, 0xD556, 2, 3 },
    { 0xD556, 0xFFFF, 3, 3 },
};
} // namespace

// Sky-dome variant + cross-fade at a dayTime, per the 3DS's OWN dome
// consumer (FUN_002e47c8/§9.2 above) — a SEPARATE, purpose-built table from
// kTitleLightSchedule below even though the two are boundary/value-identical
// (contiguous tables in the ROM's data blob, title_sky_dome.md §9.2). Do not
// alias kTitleLightSchedule's index field for this per §9.5's explicit note.
extern "C" int Zelda3D_TitleCsDomeBlend(uint16_t daytime, int* skybox1Index,
                                        int* skybox2Index, float* blendWeight) {
    for (const DomeSpan& sp : kTitleDomeSchedule) {
        if (sp.start <= daytime && (daytime < sp.end || sp.end == 0xFFFF)) {
            *skybox1Index = sp.idx1;
            *skybox2Index = sp.idx2;
            const float d = (float)(sp.end - sp.start);
            // FUN_00361490 shape: w = (t-start)/(end-start), ceiling clamp only.
            float w = (d > 0.0f) ? (float)(daytime - sp.start) / d : 1.0f;
            *blendWeight = (w < 1.0f) ? w : 1.0f;
            return 1;
        }
    }
    return 0;
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

// Fog params blended with the SAME schedule/weights as BlendedLight — see the header comment
// for the RE'd units (oot3d-decomp title_env_lighting.md §13). The palette's packed u16's top
// bits are the N64-style blendRate; only the low 10 bits are the fog near distance.
extern "C" int Zelda3D_TitleCsBlendedFog(uint16_t daytime, float* fogNear, float* fogFar,
                                         float* fogEnd) {
    if (!sTitlePalOk) return 0;
    int sf, st;
    float w;
    if (!Zelda3D_TitleCsLightBlend(daytime, &sf, &st, &w)) return 0;
    const TitleLightEntry& a = sTitlePal[sf & 3];
    const TitleLightEntry& b = sTitlePal[st & 3];
    const float nearA = (float)(a.fogNear & 0x3ff);
    const float nearB = (float)(b.fogNear & 0x3ff);
    *fogNear = nearA + (nearB - nearA) * w;
    *fogFar = a.drawDist + (b.drawDist - a.drawDist) * w;
    *fogEnd = a.fogEnd + (b.fogEnd - a.fogEnd) * w;
    return 1;
}
