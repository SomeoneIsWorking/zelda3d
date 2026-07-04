// Az OoT3D title-demo lighting timeline — transcribed from Az live envCtx
// via the harness (see scratch/az_title_lighting_timeline.py). Each entry
// covers a cursor range where Az's envCtx.unk_BF is a specific spot00
// slot value. Slots 0xFF / 0xFE / 0xFD are OoT-native sentinel values
// (0xFF = use dayTime table, 0xFE = disable lightSetting, 0xFD = ?).
//
// Cursor = csCtx.frames on both engines. `force titletime N` on the
// harness pins BOTH engines' cursors to N and Az's Env_Update-equivalent
// (FUN_0045dd30) commits the CS-driven slot within a few frames of the
// pin. Ranges gapped by 20 cursors are the harness sample step; those
// gap frames are transient lerp windows where Az has a between-slot
// blend.
//
// See debug_journal/2026-07-04-title-slot-divergence.md for context on
// why this is used instead of the N64 CS-derived slot picks.

static const struct {
    uint16_t start;
    uint16_t end;
    uint8_t  slot;
} kAzTitleLightingTimeline[] = {
    {    0,  460, 8   },
    {  480,  640, 7   },
    {  660,  680, 6   },
    {  700,  860, 9   },
    {  880, 1060, 8   },
    { 1080, 1240, 7   },
    { 1260, 1420, 6   },
    { 1440, 1620, 5   },
    { 1640, 1780, 4   },
    { 1800, 1960, 3   },
    { 1980, 2140, 2   },
    { 2160, 2320, 1   },
    { 2340, 2660, 0   },
    { 2680, 2840, 0xFF },
    { 2860, 3080, 0xFE },
    { 3100, 3200, 0xFD },
};
static const int kAzTitleLightingTimelineLen =
    (int)(sizeof(kAzTitleLightingTimeline) / sizeof(kAzTitleLightingTimeline[0]));

// Returns the Az-derived slot for cursor `frames`, or 0xFF if the cursor
// falls in a gap between sampled ranges (harness step size = 20). Gaps
// are transient lerp windows; caller may fall back to Env_Update-driven
// blending or hold the previous slot.
static inline uint8_t Zelda3d_TitleSlotForCursor(int frames) {
    if (frames < 0) frames = 0;
    for (int i = 0; i < kAzTitleLightingTimelineLen; ++i) {
        if ((uint16_t)frames >= kAzTitleLightingTimeline[i].start &&
            (uint16_t)frames <= kAzTitleLightingTimeline[i].end) {
            return kAzTitleLightingTimeline[i].slot;
        }
    }
    // Cursor past the sampled range or in a gap. For gap-holding, walk
    // back to the last range whose end is < frames.
    uint8_t last = 8;  // shot-0 default (matches Az cursor=0)
    for (int i = 0; i < kAzTitleLightingTimelineLen; ++i) {
        if (kAzTitleLightingTimeline[i].end < frames) {
            last = kAzTitleLightingTimeline[i].slot;
        }
    }
    return last;
}
