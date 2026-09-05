#pragma once
#include "extractor/RomVersionTable.h"
#include "platform/rom_identity.h"
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Zelda3D::Extractor {
bool IsSupportedN64RomSize(uint64_t size);
bool NormalizeN64Rom(std::span<unsigned char> bytes);
uint32_t N64HeaderCrc(std::span<const unsigned char> bytes);
// Normalizes and applies supported header repairs in-place. The caller owns these working bytes.
bool ValidateN64Rom(std::span<unsigned char> bytes, const RomVersionTable& table, std::string& error);
// Publishes normalized bytes only after the entire file validates; failure preserves prior bytes.
bool LoadValidatedN64Rom(const std::filesystem::path& path, const RomVersionTable& table,
                         std::vector<unsigned char>& bytes, std::string& error);
bool ValidateN64RomFile(Platform::RomKind kind, const std::filesystem::path& path, std::string& error);
} // namespace Zelda3D::Extractor
