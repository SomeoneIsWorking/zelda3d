#include "ship/config/Config.h"

#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <any>
#include <spdlog/spdlog.h>
#include "ship/utils/StringHelper.h"
#include "ship/Context.h"

namespace fs = std::filesystem;

namespace Ship {
Config::Config(std::string path) : mPath(std::move(path)), mIsNewInstance(false) {
    Reload();
}

Config::~Config() {
    SPDLOG_TRACE("destruct config");
}

std::string Config::FormatNestedKey(const std::string& key) {
    std::vector<std::string> dots = StringHelper::Split(key, ".");

    std::string tmp;
    if (dots.size() > 1) {
        for (const auto& dot : dots) {
            tmp += "/" + dot;
        }
    } else {
        tmp = "/" + dots[0];
    }

    return tmp;
}

nlohmann::json Config::Nested(const std::string& key) {
    std::vector<std::string> dots = StringHelper::Split(key, ".");
    if (!mFlattenedJson.is_object()) {
        return mFlattenedJson;
    }

    nlohmann::json gjson = mFlattenedJson.unflatten();

    if (dots.size() > 1) {
        for (auto& dot : dots) {
            if (dot == "*" || gjson.contains(dot)) {
                gjson = gjson[dot];
            }
        }
        return gjson;
    }

    return gjson[key];
}

std::string Config::GetString(const std::string& key, const std::string& defaultValue) {
    nlohmann::json n = Nested(key);
    if (n.is_string() && !n.get<std::string>().empty()) {
        return n;
    }

    return defaultValue;
}

float Config::GetFloat(const std::string& key, float defaultValue) {
    nlohmann::json n = Nested(key);
    if (n.is_number_float()) {
        return n;
    }

    return defaultValue;
}

bool Config::GetBool(const std::string& key, bool defaultValue) {
    nlohmann::json n = Nested(key);
    if (n.is_boolean()) {
        return n;
    }

    return defaultValue;
}

int32_t Config::GetInt(const std::string& key, int32_t defaultValue) {
    nlohmann::json n = Nested(key);
    if (n.is_number_integer()) {
        return n;
    }

    return defaultValue;
}

uint32_t Config::GetUInt(const std::string& key, uint32_t defaultValue) {
    nlohmann::json n = Nested(key);
    if (n.is_number_unsigned()) {
        return n;
    }

    return defaultValue;
}

bool Config::Contains(const std::string& key) {
    return !Nested(key).is_null();
}

void Config::SetString(const std::string& key, const std::string& value) {
    mFlattenedJson[FormatNestedKey(key)] = value;
}

void Config::SetFloat(const std::string& key, float value) {
    mFlattenedJson[FormatNestedKey(key)] = value;
}

void Config::SetBool(const std::string& key, bool value) {
    mFlattenedJson[FormatNestedKey(key)] = value;
}

void Config::SetInt(const std::string& key, int32_t value) {
    mFlattenedJson[FormatNestedKey(key)] = value;
}

void Config::SetUInt(const std::string& key, uint32_t value) {
    mFlattenedJson[FormatNestedKey(key)] = value;
}

void Config::Erase(const std::string& key) {
    mFlattenedJson.erase(FormatNestedKey(key));
}

void Config::SetBlock(const std::string& key, nlohmann::json block) {
    nlohmann::json gjson = mFlattenedJson.unflatten();
    if (key.find(".") != std::string::npos) {
        nlohmann::json* gjson2 = &gjson;
        std::vector<std::string> dots = StringHelper::Split(key, ".");
        if (dots.size() > 1) {
            size_t curDot = 0;
            for (auto& dot : dots) {
                if (curDot == dots.size() - 1) {
                    if (gjson2->contains(dot)) {
                        gjson2->at(dot) = block;
                        break;
                    } else {
                        gjson2->emplace(dot, block);
                        break;
                    }
                } else if (gjson2->contains(dot)) {
                    gjson2 = &gjson2->at(dot);
                    curDot++;
                }
            }
        }
    } else {
        gjson[key] = block;
    }
    mFlattenedJson = gjson.flatten();
    Save();
}

void Config::EraseBlock(const std::string& key) {
    nlohmann::json gjson = mFlattenedJson.unflatten();
    if (key.find(".") != std::string::npos) {
        nlohmann::json* gjson2 = &gjson;
        std::vector<std::string> dots = StringHelper::Split(key, ".");
        if (dots.size() > 1) {
            size_t curDot = 0;
            for (auto& dot : dots) {
                if (gjson2->contains(dot)) {
                    if (curDot == dots.size() - 1) {
                        gjson2->at(dot).clear();
                        gjson2->erase(dot);
                    } else {
                        gjson2 = &gjson2->at(dot);
                        curDot++;
                    }
                }
            }
        }
    } else {
        if (gjson.contains(key)) {
            gjson.erase(key);
        }
    }
    mFlattenedJson = gjson.flatten();
    Save();
}

void Config::Copy(const std::string& fromKey, const std::string& toKey) {
    auto nestedFromKey = FormatNestedKey(fromKey);
    auto nestedToKey = FormatNestedKey(toKey);
    if (mFlattenedJson.contains(nestedFromKey)) {
        mFlattenedJson[nestedToKey] = mFlattenedJson[nestedFromKey];
    }
}

void Config::Reload() {
    if (mPath == "None" || !fs::exists(mPath) || !fs::is_regular_file(mPath)) {
        mIsNewInstance = true;
        mFlattenedJson = nlohmann::json::object();
        return;
    }
    std::ifstream ifs(mPath);

    mNestedJson = nlohmann::json::object();
    mFlattenedJson = nlohmann::json::object();
    try {
        mNestedJson = nlohmann::json::parse(ifs);
        mFlattenedJson = mNestedJson.flatten();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse config file {}: {}", mPath, e.what());
    } catch (const std::exception& e) { SPDLOG_ERROR("Unexpected error loading config file {}: {}", mPath, e.what()); }
}

void Config::Save() {
    // mFlattenedJson holds JSON-pointer keys ("/A/B") -> primitive leaves. unflatten() rebuilds the
    // nested form for the on-disk file, but throws type_error.313 if two keys collide such that one is
    // a scalar leaf AND a prefix path of another (e.g. "/Foo" and "/Foo/Bar") — i.e. a CVar written
    // both as a scalar and as the parent of a nested key. Such a pair is impossible to represent in
    // the nested on-disk form. Build the nested form into a LOCAL first and only open (truncate) the
    // file once it succeeds — otherwise a bad key both wipes the config and aborts the process.
    nlohmann::json nested;
    try {
        nested = mFlattenedJson.unflatten();
    } catch (const nlohmann::json::exception&) {
        // Identify and drop the colliding scalar prefixes so the config still saves. Sort keys: a key
        // that is a strict "/"-delimited path-prefix of the next is the un-nestable scalar.
        std::vector<std::string> keys;
        for (auto it = mFlattenedJson.begin(); it != mFlattenedJson.end(); ++it) {
            keys.push_back(it.key());
        }
        std::sort(keys.begin(), keys.end());
        for (size_t i = 0; i + 1 < keys.size(); ++i) {
            const std::string& a = keys[i];
            const std::string& b = keys[i + 1];
            if (b.size() > a.size() && b.compare(0, a.size(), a) == 0 && b[a.size()] == '/') {
                SPDLOG_ERROR("Config::Save: dropping mis-registered scalar CVar '{}' — it is also the "
                             "parent of '{}', which cannot be represented in the nested config",
                             a, b);
                mFlattenedJson.erase(a);
            }
        }
        nested = mFlattenedJson.unflatten();
    }
    std::ofstream file(mPath);
    mNestedJson = nested;
    file << mNestedJson.dump(4);
}

template <typename T> std::vector<T> Config::GetArray(const std::string& key) {
    if (nlohmann::json tmp = Nested(key); tmp.is_array()) {
        return tmp.get<std::vector<T>>();
    }
    return std::vector<T>();
};

template <typename T> void Config::SetArray(const std::string& key, std::vector<T> array) {
    mFlattenedJson[FormatNestedKey(key)] = nlohmann::json(array);
}

nlohmann::json Config::GetNestedJson() {
    return mNestedJson;
}

bool Config::RegisterVersionUpdater(std::shared_ptr<ConfigVersionUpdater> versionUpdater) {
    auto [_, emplaced] = mVersionUpdaters.emplace(versionUpdater->GetVersion(), versionUpdater);
    return emplaced;
}

void Config::RunVersionUpdates() {
    for (auto [_, versionUpdater] : mVersionUpdaters) {
        uint32_t version = GetUInt("ConfigVersion", 0);
        if (version < versionUpdater->GetVersion()) {
            versionUpdater->Update(this);
            SetUInt("ConfigVersion", versionUpdater->GetVersion());
        }
    }
    Save();
}

ConfigVersionUpdater::ConfigVersionUpdater(uint32_t toVersion) : mVersion(toVersion) {
}

uint32_t ConfigVersionUpdater::GetVersion() {
    return mVersion;
}

} // namespace Ship
