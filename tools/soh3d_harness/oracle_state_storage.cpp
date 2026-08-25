#include "oracle_state_storage.h"

#include <cstdio>
#include <utility>

#include "binary_file.h"
#include "core/core.h"
#include "repl_protocol.h"

namespace HarnessOracleStorage {

bool LoadStateFile(const std::string& path) {
    auto buffer = HarnessBinaryFile::Read(path);
    return !buffer.empty() && Core::System::GetInstance().LoadStateBuffer(std::move(buffer));
}

bool HandleLoad(std::istringstream& arguments) {
    std::string path;
    if (!(arguments >> path)) {
        HarnessRepl::PrintErr("loadstate: usage: loadstate <path>");
        return false;
    }
    if (!LoadStateFile(path)) {
        HarnessRepl::PrintErr("loadstate: read/deserialize failed");
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
