#pragma once

#include <string>
#include <unordered_map>

struct ExtensionEntry {
    std::string path;
    std::string ext;
};

extern std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

extern "C" void OTRExtScanner();
