#include "binary_file.h"

#include <cstdio>

namespace HarnessBinaryFile {

std::vector<uint8_t> Read(const std::string& path) {
    std::vector<uint8_t> data;
    std::FILE* input = std::fopen(path.c_str(), "rb");
    if (!input) {
        return data;
    }
    std::fseek(input, 0, SEEK_END);
    const long size = std::ftell(input);
    std::fseek(input, 0, SEEK_SET);
    if (size > 0) {
        data.resize(static_cast<size_t>(size));
        if (std::fread(data.data(), 1, data.size(), input) != data.size()) {
            data.clear();
        }
    }
    std::fclose(input);
    return data;
}

bool Write(const std::string& path, const std::vector<uint8_t>& data) {
    std::FILE* output = std::fopen(path.c_str(), "wb");
    if (!output) {
        return false;
    }
    const bool wrote = std::fwrite(data.data(), 1, data.size(), output) == data.size();
    std::fclose(output);
    return wrote;
}

} // namespace HarnessBinaryFile
