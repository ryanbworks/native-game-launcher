#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Persists only the local nickname ("nickname" in state.json); it is not authentication or
// a Minecraft account.
class NicknameStore
{
public:
    static constexpr const char* Key = "nickname";
    [[nodiscard]] static bool isValid(std::wstring_view nickname);
    // Empty when none was saved.
    [[nodiscard]] static std::wstring load(const std::filesystem::path& dataDirectory);
    [[nodiscard]] static bool save(
        const std::filesystem::path& dataDirectory,
        std::wstring_view nickname);
};
