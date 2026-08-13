// Zelda3D CMAB (CTR Material Animation Binary) player — generic material-anim keyframe sampler.
//
// Ground truth: oot3d-decomp/docs/title_logo_fireglow_cmab.md §1 (byte-level format spec,
// verified against tools/cmab.py — itself ported from noclip.website's cmab.ts, which is a
// decompilation of this exact OoT3D binary's CMAB reader). This C++ port mirrors
// tools/cmab.py's Track.sample() exactly (Linear/Hermite/Integer, the Hermite "reset tangent"
// 1-frame special case included) so a native player gets the identical curve values the Python
// tool already validated.
//
// First consumer: the title-logo fire-glow (behaviors/title/title_fireglow.cpp), which needs
// g_title_fire.cmab's Translation (UV scroll) and ConstColor (RGB tint) entries. Per
// title_logo_fireglow_cmab.md §4, this is intentionally the FIRST general CMAB track sampler in
// the codebase (the two existing CMAB consumers — zelda3d.c's sky cloud-scroll and
// zelda3d_model.cpp's facial eye/mouth swap — each special-case one narrow slice of the format,
// neither touches the mads/mmad/track machinery) — reuse this for any future material-anim need
// rather than adding a third special case.
#ifndef ZELDA3D_CMAB_H
#define ZELDA3D_CMAB_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Parse a cmab file's raw bytes (as read from its ZAR, e.g. via Zelda3D_AutoModelReadZarFile).
// Returns an opaque handle owned by the caller (free with Zelda3D_CmabFree), or NULL if `data`
// isn't a valid cmab (bad magic/subversion/asserts — see title_logo_fireglow_cmab.md §1).
void* Zelda3D_CmabParse(const uint8_t* data, size_t size);
void  Zelda3D_CmabFree(void* handle);

int Zelda3D_CmabDuration(void* handle);  // animation length, frames
int Zelda3D_CmabLoopMode(void* handle);  // 0 = Once (hold last keyframe past duration), 1 = Repeat

// Sample the (materialIndex, channelIndex) Translation entry's V-track (track[1]) at `frame`.
// `frame` is clamped to [0, duration] internally when loopMode == Once (matches the fire-glow's
// observed one-shot-then-freeze behavior, title_logo_fireglow_cmab.md §2). Returns 1 and writes
// *outV if a matching Translation entry with a populated V track exists, else 0 (untouched).
int Zelda3D_CmabSampleTranslationV(void* handle, int materialIndex, int channelIndex, float frame,
                                    float* outV);
int Zelda3D_CmabSampleTranslationUV(void* handle, int materialIndex, int channelIndex, float frame,
                                    float* outU, float* outV);
int Zelda3D_CmabSampleTexturePalette(void* handle, int materialIndex, int channelIndex, float frame,
                                     int* outIndex);

// Sample the (materialIndex, channelIndex) ConstColor entry's R/G/B tracks at `frame` (same
// clamping as above). Absent tracks fall back per-channel to the CMAB format's own default (0 —
// matches every currently-known ConstColor entry's B track, which is a flat 0 constant, not
// modeled as "absent" in the title fire cmabs but handled here for completeness). Returns 1 and
// writes rgb3[0..2] if a matching ConstColor entry exists, else 0.
int Zelda3D_CmabSampleConstColorRGB(void* handle, int materialIndex, int channelIndex, float frame,
                                     float* rgb3);
int Zelda3D_CmabSampleConstColorRGBA(void* handle, int materialIndex, int channelIndex, float frame,
                                     float* rgba4);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_CMAB_H
