#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// Leve, Equilibrado and Máximo: one click sets the game's memory and its video options,
// instead of tuning both in Configurações and inside Minecraft.
namespace PerformanceProfiles
{
    enum class Profile { Light, Balanced, Maximum };
    inline constexpr Profile All[] = {Profile::Light, Profile::Balanced, Profile::Maximum};

    const wchar_t* name(Profile profile);
    std::string id(Profile profile);
    std::optional<Profile> fromId(std::string_view id);

    struct Memory { int minimumMegabytes; int maximumMegabytes; };
    // The profile's memory, never more than this computer can spare (60% of its RAM,
    // in steps of 512 MB, at least 2 GB).
    Memory memoryFor(Profile profile, unsigned long long physicalMegabytes);

    // The options.txt keys a profile sets: `modern` for 1.20.1, otherwise 1.8.9's names.
    std::map<std::string, std::string> videoOptions(Profile profile, bool modern);
    // Rewrites only those keys in options.txt, keeping every other line and their order.
    // A missing file is created with just them (1.20.1 also gets its data version).
    bool applyVideoOptions(const std::filesystem::path& gameDirectory, Profile profile, bool modern);
    // Exposed for tests: the new text of options.txt.
    std::string mergeOptions(std::string_view current, const std::map<std::string, std::string>& values, bool modern);

    // The profile last chosen, kept in performance-profile.txt in the data folder.
    std::optional<Profile> load(const std::filesystem::path& dataDirectory);
    bool save(const std::filesystem::path& dataDirectory, Profile profile);
}
