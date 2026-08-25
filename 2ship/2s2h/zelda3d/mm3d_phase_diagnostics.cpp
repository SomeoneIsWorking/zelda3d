// MM3D animation phase diagnostics: measures whether sampled clips actually advance.
#include "mm3d_phase_diagnostics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace Zelda3D::MM3D {
namespace {

struct PhaseStat {
    float low = 1e30f;
    float high = -1e30f;
    long samples = 0;
    float duration = 0.0f;
    bool phaseLocked = false;
    long morphSamples = 0;
    float maxMorph = 0.0f;
    std::set<const void*> actors;
};

std::map<std::pair<int, std::string>, PhaseStat> g_phaseStats;

bool PhaseReportEnabled() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("ZELDA3D_MM_PHASE_REPORT");
        enabled = value != nullptr && value[0] != '\0' && value[0] != '0';
        if (enabled) {
            atexit(DumpAndClearAnimationPhases);
        }
    }
    return enabled != 0;
}

} // namespace

void RecordAnimationPhase(int modelId, const char* animation, const void* actorKey, float frame, float duration,
                          bool phaseLocked, float morphWeight) {
    if (!PhaseReportEnabled() || animation == nullptr) {
        return;
    }
    PhaseStat& stat = g_phaseStats[{ modelId, std::string(animation) }];
    stat.low = std::min(stat.low, frame);
    stat.high = std::max(stat.high, frame);
    stat.duration = duration;
    stat.phaseLocked = phaseLocked;
    ++stat.samples;
    if (morphWeight > 0.0f) {
        ++stat.morphSamples;
        stat.maxMorph = std::max(stat.maxMorph, morphWeight);
    }
    stat.actors.insert(actorKey);
}

void DumpAndClearAnimationPhases() {
    if (!PhaseReportEnabled()) {
        return;
    }
    fprintf(stderr, "[MM3D-PHASE] %zu (model,clip) pair(s) sampled\n", g_phaseStats.size());
    int stuck = 0;
    long morphTotal = 0;
    for (const auto& [key, stat] : g_phaseStats) {
        const bool moved = stat.high - stat.low > 1e-3f;
        const size_t actorCount = stat.actors.empty() ? 1 : stat.actors.size();
        const bool enough = stat.samples >= 2 && static_cast<size_t>(stat.samples) >= actorCount * 2;
        const char* verdict = !enough ? "THIN" : (moved ? "MOVED" : "STUCK");
        if (!moved && enough) {
            ++stuck;
        }
        morphTotal += stat.morphSamples;
        fprintf(stderr,
                "[MM3D-PHASE] %-6s model=%d %-28s f %.2f..%.2f dur=%.0f n=%ld %s "
                "morph=%ld/%.2f actors=%lu\n",
                verdict, key.first, key.second.c_str(), stat.low, stat.high, stat.duration, stat.samples,
                stat.phaseLocked ? "phase-locked" : "free-run", stat.morphSamples, stat.maxMorph,
                static_cast<unsigned long>(actorCount));
    }
    fprintf(stderr, "[MM3D-PHASE] morph path fired on %ld sample(s) across all pairs.%s\n", morphTotal,
            morphTotal == 0 ? "  Either no transition happened while this was watching, or MM never"
                              " reports a nonzero morphWeight -- do NOT read 0 as 'morph works'."
                            : "");
    fprintf(stderr,
            "[MM3D-PHASE] %d of %zu pair(s) with >=2 samples PER ACTOR never advanced."
            "  (THIN = too few samples per actor to say either way.)%s\n",
            stuck, g_phaseStats.size(),
            g_phaseStats.empty() ? "  NOTE: zero pairs sampled -- this run measured NOTHING, which is"
                                   " NOT the same as 'nothing was stuck'."
                                 : "");
    g_phaseStats.clear();
    fflush(stderr);
}

} // namespace Zelda3D::MM3D
