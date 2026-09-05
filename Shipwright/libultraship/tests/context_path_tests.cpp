#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "ship/Context.h"

namespace Fs = std::filesystem;

#if defined(__linux__)
TEST(ContextAppDirectory, UsesXdgConfigInsteadOfWorkingDirectory) {
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    const std::optional<std::string> saved = previous != nullptr ? std::optional<std::string>(previous) : std::nullopt;
    const Fs::path root = Fs::absolute("scratch/lus-tests/xdg-data");
    ASSERT_EQ(setenv("XDG_CONFIG_HOME", root.c_str(), 1), 0);

    const Fs::path resolved = Ship::Context::GetAppDirectoryPath("zelda3d-path-test");

    if (saved.has_value()) {
        ASSERT_EQ(setenv("XDG_CONFIG_HOME", saved->c_str(), 1), 0);
    } else {
        ASSERT_EQ(unsetenv("XDG_CONFIG_HOME"), 0);
    }
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_NE(resolved, Fs::path("."));
    const Fs::path relative = resolved.lexically_relative(root);
    ASSERT_FALSE(relative.empty());
    EXPECT_EQ(relative.begin()->string(), "zelda3d-path-test");
}

TEST(ContextAppDirectory, RejectsRelativeXdgConfigHomeInsteadOfWritingIntoWorkingDirectory) {
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    const std::optional<std::string> saved = previous != nullptr ? std::optional<std::string>(previous) : std::nullopt;
    ASSERT_EQ(setenv("XDG_CONFIG_HOME", "relative-config", 1), 0);

    EXPECT_THROW(Ship::Context::GetAppDirectoryPath("zelda3d-path-test"), std::runtime_error);

    if (saved.has_value()) {
        ASSERT_EQ(setenv("XDG_CONFIG_HOME", saved->c_str(), 1), 0);
    } else {
        ASSERT_EQ(unsetenv("XDG_CONFIG_HOME"), 0);
    }
}
#endif
