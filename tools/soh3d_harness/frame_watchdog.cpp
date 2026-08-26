#include "frame_watchdog.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

namespace HarnessWatchdog {
namespace {

constexpr int kTimeoutSeconds = 5;
std::atomic<const char*> g_operation{ nullptr };
std::atomic<uint64_t> g_frame{ 0 };
std::atomic<bool> g_active{ false };

void HandleTimeout(int) {
    const char* operation = g_operation.load();
    std::fprintf(stderr,
                 "\n===== harness watchdog: frame stalled >%ds =====\n"
                 "  where     : %s\n"
                 "  frame idx : %llu\n"
                 "  backtrace :\n",
                 kTimeoutSeconds, operation ? operation : "(unknown)", static_cast<unsigned long long>(g_frame.load()));
    void* entries[64];
    const int count = backtrace(entries, 64);
    backtrace_symbols_fd(entries, count, fileno(stderr));
    std::fprintf(stderr, "===== forcing _exit =====\n");
    std::fflush(stderr);
    _exit(124);
}

} // namespace

void Install() {
    struct sigaction action{};
    action.sa_handler = HandleTimeout;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGALRM, &action, nullptr);
}

void Pulse() {
    if (g_active.load()) {
        alarm(kTimeoutSeconds);
    }
}

Frame::Frame(const char* operation) {
    g_operation.store(operation);
    g_frame.fetch_add(1);
    g_active.store(true);
    alarm(kTimeoutSeconds);
}

Frame::~Frame() {
    g_active.store(false);
    alarm(0);
}

} // namespace HarnessWatchdog
