#include "ship/controller/scripted/ScriptedInputFifo.h"
#include "ship/controller/scripted/ScriptedInput.h"

#include <libultraship/bridge/fifo_rpc.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// POSIX named-pipe transport. The poller is a headless-development tool for the Wayland/Xvfb
// runs; on Windows (no mkfifo) it compiles to a no-op so the shared library still links.
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

std::atomic<bool> gRunning{ false };
std::thread gThread;
std::string gOutPath;

// Clamp a parsed integer into the N64 analog range without pulling in <algorithm>.
int8_t clampAxis(long v) {
    if (v < -128) {
        v = -128;
    } else if (v > 127) {
        v = 127;
    }
    return (int8_t)v;
}

bool writeReply(const char* line) {
    if (gOutPath.empty()) {
        return false;
    }
    FILE* f = std::fopen(gOutPath.c_str(), "a");
    if (f != nullptr) {
        const bool wroteLine = std::fputs(line, f) >= 0 && std::fputc('\n', f) != EOF;
        const bool closed = std::fclose(f) == 0;
        return wroteLine && closed;
    }
    return false;
}

struct ReplyContext {
    bool tagged = false;
    bool failed = false;
    char id[ZELDA3D_FIFO_RPC_ID_LENGTH + 1] = {};
    size_t lineCount = 0;
};

void reply(ReplyContext& context, const char* line) {
    if (!context.tagged) {
        writeReply(line);
        return;
    }
    char framed[160];
    const int length = Zelda3DFifoRpc_FormatData(framed, sizeof(framed), context.id, line);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(framed) || !writeReply(framed)) {
        context.failed = true;
        return;
    }
    context.lineCount++;
}

void finishReply(ReplyContext& context) {
    if (!context.tagged || context.failed) {
        return;
    }
    char framed[64];
    const int length = Zelda3DFifoRpc_FormatEnd(framed, sizeof(framed), context.id, context.lineCount);
    if (length >= 0 && static_cast<size_t>(length) < sizeof(framed)) {
        writeReply(framed);
    }
}

// Execute one command line. Kept tiny and dependency-free so it is trivially game-agnostic.
void execLine(const char* line) {
    ReplyContext replyContext;
    const char* command = line;
    const int framing = Zelda3DFifoRpc_ParseRequest(line, replyContext.id, &command);
    replyContext.tagged = framing == 1;
    if (framing < 0) {
        ReplyContext legacy;
        reply(legacy, "err malformed-request-id");
        return;
    }
    line = command;
    // Skip leading whitespace / ignore blank lines.
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        finishReply(replyContext);
        return;
    }

    if (std::strncmp(line, "enable", 6) == 0) {
        int on = std::atoi(line + 6);
        Ship_ScriptedInput_SetEnabled(on);
        reply(replyContext, on ? "ok enable 1" : "ok enable 0");
    } else if (std::strncmp(line, "btn", 3) == 0) {
        uint16_t mask = (uint16_t)std::strtol(line + 3, nullptr, 0);
        Ship_ScriptedInput_SetButtons(mask);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "ok btn 0x%04X", mask);
        reply(replyContext, buf);
    } else if (std::strncmp(line, "stick", 5) == 0) {
        char* end = nullptr;
        long x = std::strtol(line + 5, &end, 10);
        long y = std::strtol(end != nullptr ? end : line + 5, nullptr, 10);
        Ship_ScriptedInput_SetStick(clampAxis(x), clampAxis(y));
        char buf[48];
        std::snprintf(buf, sizeof(buf), "ok stick %d %d", (int)clampAxis(x), (int)clampAxis(y));
        reply(replyContext, buf);
    } else if (std::strncmp(line, "reset", 5) == 0) {
        Ship_ScriptedInput_SetEnabled(0);
        Ship_ScriptedInput_SetButtons(0);
        Ship_ScriptedInput_SetStick(0, 0);
        reply(replyContext, "ok reset");
    } else if (std::strncmp(line, "ping", 4) == 0) {
        reply(replyContext, "pong");
    } else {
        reply(replyContext, "err unknown-command");
    }
    finishReply(replyContext);
}

#ifndef _WIN32
void pollerThread(std::string fifoPath) {
    ::mkfifo(fifoPath.c_str(), 0666); // ignore EEXIST
    // O_RDWR keeps a writer open on our side so reads never hit EOF when a client disconnects.
    int fd = ::open(fifoPath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        gRunning.store(false, std::memory_order_relaxed);
        return;
    }
    writeReply("SHIP scripted-input FIFO ready");

    std::string pending;
    char buf[512];
    while (gRunning.load(std::memory_order_relaxed)) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        // 200ms timeout keeps Stop() responsive (checked each loop) without busy-spinning.
        int pr = ::poll(&pfd, 1, 200);
        if (pr <= 0) {
            continue;
        }
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            continue;
        }
        pending.append(buf, (size_t)n);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string cmd = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            execLine(cmd.c_str());
        }
        // Bound the buffer so a writer that never sends a newline can't grow it unbounded.
        if (pending.size() > 4096) {
            pending.clear();
        }
    }
    ::close(fd);
}
#endif

} // namespace

extern "C" {

void Ship_ScriptedInputFifo_StartFromEnv(void) {
#ifndef _WIN32
    if (gRunning.load(std::memory_order_relaxed)) {
        return; // already running; idempotent
    }
    const char* path = std::getenv("SHIP_SCRIPTED_FIFO");
    if (path == nullptr || path[0] == '\0') {
        return; // off by default
    }
    gOutPath = std::string(path) + ".out";
    // Truncate any stale reply file so a fresh run starts clean.
    if (FILE* f = std::fopen(gOutPath.c_str(), "w")) {
        std::fclose(f);
    }
    gRunning.store(true, std::memory_order_relaxed);
    gThread = std::thread(pollerThread, std::string(path));
#endif
}

void Ship_ScriptedInputFifo_Stop(void) {
#ifndef _WIN32
    if (!gRunning.exchange(false, std::memory_order_relaxed)) {
        return;
    }
    if (gThread.joinable()) {
        gThread.join();
    }
#endif
}

} // extern "C"
