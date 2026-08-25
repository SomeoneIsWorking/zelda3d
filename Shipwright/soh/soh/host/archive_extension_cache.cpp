#include "archive_extension_cache.h"

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/utils/StringHelper.h>

#include <algorithm>
#include <vector>

std::unordered_map<std::string, ExtensionEntry> ExtensionCache;

extern "C" void OTRExtScanner() {
    auto files = *Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles();
    for (const std::string& resourcePath : files) {
        const std::vector<std::string> components = StringHelper::Split(resourcePath, ".");
        const std::string& extension = components.back();
        std::string normalizedPath = resourcePath.substr(0, resourcePath.size() - (extension.size() + 1));
        std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
        ExtensionCache[normalizedPath] = { resourcePath, extension };
    }
}
