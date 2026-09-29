#pragma once

#include <sstream>
#include <string>

namespace HarnessOracleStorage {

/// Why a savestate load did not happen.
///
/// A load failure is a MEASUREMENT, not a crash. `Core::System::LoadStateBuffer` throws
/// `boost::archive::archive_exception` (a `std::exception`) on a savestate it cannot deserialize,
/// and the first version of this owner let that escape: the process hit `std::terminate` and the
/// whole oracle session died without printing the one fact the operator needed. Every other
/// measurement taken up to that point was lost with it, and the failure surfaced as a dead harness
/// rather than as a bad file.
///
/// `LoadStateFile` is the single owner of that boundary, so it is also where the reason belongs.
/// The reason is returned rather than printed so the caller decides the wording, and so a unit
/// test can assert on it without spawning an emulator.
struct StateLoadResult {
    bool loaded = false;
    /// Empty when `loaded`. Otherwise a stable, greppable token: `unreadable`, `empty`,
    /// `incompatible`, or `error:<what()>`. `incompatible` is separated from `unreadable` because
    /// they demand different responses -- a truncated or absent file is a provisioning problem,
    /// while a file the current build refuses is a serialization problem.
    std::string reason;
};

StateLoadResult LoadStateFile(const std::string& path);

/// REPL entry point. Returns false and prints one `err` line on failure, leaving the emulator
/// running so the operator can keep measuring.
bool HandleLoad(std::istringstream& arguments);
void HandleSave(std::istringstream& arguments);

} // namespace HarnessOracleStorage
