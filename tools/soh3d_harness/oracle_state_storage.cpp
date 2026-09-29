#include "oracle_state_storage.h"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>

#include "binary_file.h"
#include "core/core.h"
#include "repl_protocol.h"

namespace HarnessOracleStorage {

// `boost::archive::archive_exception` derives from `std::exception`, so catching the standard
// type keeps the harness free of a boost include and still catches the real failure. The catch is
// deliberately at this owner: `LoadStateBuffer` is reached from the REPL, from the cache
// importer, and from the gameplay boot route, and a throw that terminates the process is worse
// than a false return in every one of them.
StateLoadResult LoadStateFile(const std::string& path) {
    auto buffer = HarnessBinaryFile::Read(path);
    if (buffer.empty()) {
        return {false, "unreadable"};
    }
    try {
        if (!Core::System::GetInstance().LoadStateBuffer(std::move(buffer))) {
            return {false, "incompatible"};
        }
    } catch (const std::exception& exception) {
        return {false, std::string("error:") + exception.what()};
    } catch (...) {
        return {false, "error:unknown"};
    }
    return {true, ""};
}

bool HandleLoad(std::istringstream& arguments) {
    std::string path;
    if (!(arguments >> path)) {
        HarnessRepl::PrintErr("loadstate: usage: loadstate <path>");
        return false;
    }
    const StateLoadResult result = LoadStateFile(path);
    if (!result.loaded) {
        // `PrintErr` takes a `const char*`, not a `std::string`, so the reason is formatted into a
        // named local rather than concatenated at the call.
        const std::string message = "loadstate: failed reason=" + result.reason;
        HarnessRepl::PrintErr(message.c_str());
        return false;
    }
    std::printf("ok\n");
    return true;
}

void HandleSave(std::istringstream& arguments) {
    std::string path;
    if (!(arguments >> path)) {
        HarnessRepl::PrintErr("savestate: usage: savestate <path>");
        return;
    }
    if (!HarnessBinaryFile::Write(path, Core::System::GetInstance().SaveStateBuffer())) {
        HarnessRepl::PrintErr("savestate: write failed");
        return;
    }
    std::printf("ok\n");
}

} // namespace HarnessOracleStorage
