// The scripted-input FIFO poller stores its std::thread in a static, so the thread's own destructor
// runs from libultraship's _dl_fini pass and calls std::terminate if it is still joinable. A game core
// can exit() long before any Context exists to stop it -- MM's OTRGlobals::RunExtract does, from the
// boot-time extraction prompt -- so the poller has to be stopped on every exit path, not just that one.
//
// exit() cannot be called inside a test process without killing the runner, and forking a process
// that ALREADY owns the thread would leave the child holding a handle to a thread that did not survive
// the fork. So the child is forked first, while the runner is still single-threaded, and the child is
// the one that starts the poller and then exits. That makes the child a faithful miniature of the real
// failure: a process that starts the poller and leaves via exit().

#include <gtest/gtest.h>
#include <ship/controller/scripted/ScriptedInputFifo.h>

#include <filesystem>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// Runs in the forked child: start the poller on a real path, then leave through exit() -- the exact
// sequence that used to abort. Returns nothing; the parent's waitpid() is the observation.
[[noreturn]] void StartPollerThenExit(const char* fifoPath) {
    setenv("SHIP_SCRIPTED_FIFO", fifoPath, 1);
    Ship_ScriptedInputFifo_StartFromEnv();
    // exit(), not _exit(): _exit() skips every handler, so it would never reach the static
    // destruction this test exists to cover.
    exit(0);
}

} // namespace

TEST(ScriptedInputFifoLifetime, ExitWithPollerRunningExitsCleanly) {
    const auto fifoPath = std::filesystem::temp_directory_path() / "lus_scripted_fifo_lifetime_test";
    std::filesystem::remove(fifoPath);

    const pid_t child = fork();
    ASSERT_NE(child, -1) << "fork() failed; cannot observe the child's exit path";

    if (child == 0) {
        StartPollerThenExit(fifoPath.c_str());
    }

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child) << "waitpid() failed";

    // Before the fix the child died here with SIGABRT ("terminate called without an active
    // exception") because ~gThread ran still joinable, so this assertion is the falsifier.
    ASSERT_TRUE(WIFEXITED(status)) << "child was killed by signal " << WTERMSIG(status)
                                   << " instead of exiting cleanly";
    EXPECT_EQ(WEXITSTATUS(status), 0);

    std::filesystem::remove(fifoPath);
}
