// MM's `log` REPL command: the runtime half of the channel registry, so a channel can be turned on in
// a running game without a rebuild. SoH has had this since its registry existed; MM had no channels at
// all until mm3d_log.h, so it had nothing to toggle and no way to see what existed.
//
// C++, not C, on purpose: the other MM REPL commands that predate mm3d_log.h are C and format their
// replies with snprintf, which this repo's lint rejects. mm3d_fog_repl.cpp set the precedent for a new
// command -- build the reply as a std::string and hand that to the reply callback.
#include "2s2h/zelda3d/repl/mm3d_log_repl.h"

#include "2s2h/zelda3d/mm3d_log.h"

#include "lucent/log.h"

#include <cstdint>
#include <string>

namespace {

/// The channel list as one "name=on|off" string, for a reply or a message.
std::string MmLogChannelsText() {
    char channels[512];
    Zelda3D_LogList(channels, static_cast<int>(sizeof(channels)));
    return std::string(channels);
}

} // namespace

int Zelda3D_MmLogReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    (void)play;
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "log", &args) == 0) {
        return 0;
    }

    // `log <channel> <0|1>` sets; `log` alone lists. Parsed with the shared token reader rather than
    // sscanf over the raw tail, so `log bone 1 junk` is rejected here instead of half-applied -- the
    // same contract every other MM REPL command has.
    char name[32] = { 0 };
    int32_t enabled = 0;
    if (Zelda3D_MmReplNextToken(&args, name, sizeof(name)) != 0) {
        if ((Zelda3D_MmReplParseI32(&args, 10, &enabled) == 0) || (Zelda3D_MmReplArgsEnd(&args) == 0)) {
            reply("usage: log <channel> <0|1>", user);
            return 1;
        }
        if (Zelda3D_LogSet(name, static_cast<int>(enabled)) != 0) {
            reply(lucent::format("log {}={}", name, enabled).c_str(), user);
            return 1;
        }
        // The channel list, because a typo with no list to check against is the one way this command
        // can be useless. Formatted as a string rather than into a fixed buffer sized by guesswork: an
        // undersized buffer truncates the list to its first few entries, which defeats the purpose.
        reply(lucent::format("log: no channel '{}'; have: {}", name, MmLogChannelsText()).c_str(), user);
        return 1;
    }

    reply(lucent::format("log channels: {} (env ZELDA3D_LOG=name,.. or all)", MmLogChannelsText()).c_str(), user);
    return 1;
}