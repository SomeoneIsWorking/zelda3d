// The C seam over Lucent's channel control, for the in-game `log` REPL command. See zelda3d_log.h:
// the logger is Lucent's, and this is only the part a C caller and a REPL need on top of it.
//
// WHAT LUCENT ALREADY DOES, AND IS NOT REPEATED: reading the channel set from the environment, the
// gate itself, the sink, the levels, and enable_channels(). What is left is the per-game channel WALK,
// which Lucent deliberately does not provide -- its gate is name-keyed with no registry, so "which
// channels exist?" is a question only the game can answer, through its own table.
#include "diagnostics/zelda3d_log.h"

#include "lucent/config.h"
#include "lucent/log.h"

#include <cstdio>
#include <cstring>

namespace {

/// Once per process, not once per call: this walks the channel list, and the whole point of calling
/// it from run-begin rather than from the gate is that it never runs on a hot path.
bool sWarnedUnknownChannels = false;

} // namespace

void Zelda3D_LogWarnUnknownChannels(void) {
    if (sWarnedUnknownChannels) {
        return;
    }
    sWarnedUnknownChannels = true;

    // Read through lucent::config rather than getenv, so this stays a consumer of the one
    // configuration owner instead of becoming a second reader of the environment.
    const std::string& list = lucent::config::channel_list();
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        std::string name = list.substr(start, end - start);
        // Trim the spaces a hand-written list tends to carry; the comparison below is exact.
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.erase(name.begin());
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        if (!name.empty() && name != "all" && Zelda3D_LogNameExists(name.c_str()) == 0) {
            lucent::warn("zelda3d", "unknown channel in {}: '{}'", lucent::config::channel_env(), name);
        }
        if (end == list.size()) {
            break;
        }
        start = end + 1;
    }
}

int Zelda3D_LogEnabled(int channel) {
    return lucent::channel_on(Zelda3D_LogName(channel)) ? 1 : 0;
}

int Zelda3D_LogNameExists(const char* name) {
    if (name == nullptr) {
        return 0;
    }
    for (int channel = 0; channel < Zelda3D_LogChannelCount(); channel++) {
        if (std::strcmp(Zelda3D_LogName(channel), name) == 0) {
            return 1;
        }
    }
    return 0;
}

int Zelda3D_LogSet(const char* name, int on) {
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
    if (std::strcmp(name, "all") == 0) {
        // An empty list is how Lucent spells "nothing enabled": enable_channels REPLACES the set, so
        // "all 0" has to hand it something that selects nothing rather than leaving the set alone.
        lucent::enable_channels(on != 0 ? "all" : "");
        return 1;
    }
    // Checked first, because enable_channel has no "did you mean" answer: a REPL that accepted a typo
    // would report success and then log nothing, which is worse than saying no.
    if (Zelda3D_LogNameExists(name) == 0) {
        return 0;
    }
    lucent::enable_channel(name, on != 0);
    return 1;
}

void Zelda3D_LogList(char* out, int outCap) {
    if (out == nullptr || outCap <= 0) {
        return;
    }
    out[0] = '\0';
    int used = 0;
    for (int channel = 0; channel < Zelda3D_LogChannelCount(); channel++) {
        int n = std::snprintf(out + used, static_cast<std::size_t>(outCap - used), "%s%s=%s", (channel > 0) ? " " : "",
                              Zelda3D_LogName(channel), Zelda3D_LogEnabled(channel) != 0 ? "on" : "off");
        if (n < 0 || n >= outCap - used) {
            break;
        }
        used += n;
    }
}