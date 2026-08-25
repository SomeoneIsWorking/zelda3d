#include "process_environment.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace HarnessProcess {

int g_instance_lock_fd = -1;

std::string ShellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

bool LoadRepoEnvironment() {
    if (const char* loaded = std::getenv("ZELDA3D_HARNESS_ENV_LOADED"); loaded && *loaded) {
        return true;
    }

    const char* overridePath = std::getenv("ZELDA3D_HARNESS_ENV_FILE");
    const std::filesystem::path envPath = overridePath && *overridePath
                                              ? std::filesystem::path(overridePath)
                                              : std::filesystem::path(ZELDA3D_HARNESS_REPO_ROOT) / ".env";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(envPath, ec)) {
        std::fprintf(stderr,
                     "harness: environment scanned 1 candidate (%s), matched 0; continuing with caller environment\n",
                     envPath.c_str());
        return true;
    }

    // `.env` is a shell file throughout Zelda3D, not a reduced dotenv dialect.
    // Ask bash to evaluate it, then merge only names that were absent from the
    // incoming process. This preserves the launcher contract: explicit caller
    // values outrank `.env`, while plain (non-exported) NAME=value entries are
    // still exported into both embedded programs.
    std::unordered_set<std::string> callerNames;
    for (char** entry = environ; entry && *entry; ++entry) {
        const char* equals = std::strchr(*entry, '=');
        if (equals)
            callerNames.emplace(*entry, static_cast<size_t>(equals - *entry));
    }

    const std::string shell = "set -a; source " + ShellQuote(envPath.string()) + "; env -0";
    const std::string command = "bash -c " + ShellQuote(shell);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::fprintf(stderr, "harness: could not read environment %s: %s\n", envPath.c_str(), std::strerror(errno));
        return false;
    }

    std::vector<char> output;
    char chunk[4096];
    while (size_t count = std::fread(chunk, 1, sizeof(chunk), pipe)) {
        output.insert(output.end(), chunk, chunk + count);
    }
    const bool readFailed = std::ferror(pipe) != 0;
    const int status = pclose(pipe);
    if (readFailed || status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "harness: environment %s failed to evaluate (status=%d, bytes=%zu)\n", envPath.c_str(),
                     status, output.size());
        return false;
    }

    size_t loadedCount = 0;
    size_t offset = 0;
    while (offset < output.size()) {
        const auto end = std::find(output.begin() + offset, output.end(), '\0');
        const size_t length = static_cast<size_t>(end - (output.begin() + offset));
        const char* entry = output.data() + offset;
        const char* equals = static_cast<const char*>(std::memchr(entry, '=', length));
        if (equals) {
            const std::string name(entry, static_cast<size_t>(equals - entry));
            const std::string value(equals + 1, length - static_cast<size_t>(equals - entry) - 1);
            if (!callerNames.contains(name)) {
                if (setenv(name.c_str(), value.c_str(), 0) != 0) {
                    std::fprintf(stderr, "harness: could not import .env key %s: %s\n", name.c_str(),
                                 std::strerror(errno));
                    return false;
                }
                ++loadedCount;
            }
        }
        offset += length + 1;
    }
    setenv("ZELDA3D_HARNESS_ENV_LOADED", "1", 1);
    std::fprintf(stderr,
                 "harness: environment scanned 1 candidate (%s), loaded %zu new keys; caller values preserved\n",
                 envPath.c_str(), loadedCount);
    return true;
}

bool AcquireSingletonLock() {
    const char* rundir = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = rundir && *rundir ? std::string(rundir) : "scratch/harness/runtime";
    // Ensure the directory exists; mkdir failure is fine if already there.
    (void)mkdir(dir.c_str(), 0700);
    const std::string path = dir + "/soh3d_harness.lock";
    g_instance_lock_fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (g_instance_lock_fd < 0) {
        std::fprintf(stderr, "soh3d_harness: could not open lock %s: %s\n", path.c_str(), std::strerror(errno));
        return false;
    }
    if (flock(g_instance_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        // Read the holder PID from the file for a useful error.
        char buf[64] = {};
        ssize_t n = pread(g_instance_lock_fd, buf, sizeof(buf) - 1, 0);
        if (n < 0)
            n = 0;
        buf[n] = 0;
        std::fprintf(stderr,
                     "soh3d_harness: another instance is already running (pid %s).\n"
                     "  Kill it first: <safekill> soh3d_harness\n"
                     "  Lock file: %s\n",
                     buf[0] ? buf : "?", path.c_str());
        close(g_instance_lock_fd);
        g_instance_lock_fd = -1;
        return false;
    }
    // Write our pid for the next attempt's error message.
    if (ftruncate(g_instance_lock_fd, 0) == 0) {
        char pidbuf[32];
        int n = std::snprintf(pidbuf, sizeof(pidbuf), "%d\n", getpid());
        (void)write(g_instance_lock_fd, pidbuf, n);
    }
    return true;
}

} // namespace HarnessProcess
