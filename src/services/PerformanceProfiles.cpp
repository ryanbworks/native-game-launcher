#include "services/PerformanceProfiles.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    // Minecraft 1.20.1's data version: options.txt files without it are "upgraded" on load.
    constexpr const char* ModernDataVersion = "3465";

    bool writeAtomically(const std::filesystem::path& file, const std::string& content)
    {
        std::error_code ignored;
        std::filesystem::create_directories(file.parent_path(), ignored);
        auto partial = file;
        partial += L".part";
        {
            std::ofstream output(partial, std::ios::binary | std::ios::trunc);
            output << content;
            if (!output) return false;
        }
        return MoveFileExW(partial.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
}

const wchar_t* PerformanceProfiles::name(Profile profile)
{
    switch (profile) {
    case Profile::Light: return L"Leve";
    case Profile::Balanced: return L"Equilibrado";
    default: return L"Máximo";
    }
}

std::string PerformanceProfiles::id(Profile profile)
{
    switch (profile) {
    case Profile::Light: return "leve";
    case Profile::Balanced: return "equilibrado";
    default: return "maximo";
    }
}

std::optional<PerformanceProfiles::Profile> PerformanceProfiles::fromId(std::string_view text)
{
    for (const auto profile : All)
        if (id(profile) == text) return profile;
    return std::nullopt;
}

PerformanceProfiles::Memory PerformanceProfiles::memoryFor(Profile profile, unsigned long long physicalMegabytes)
{
    const int wanted = profile == Profile::Light ? 3072 : profile == Profile::Balanced ? 6144 : 8192;
    // Leave the rest of the computer room to breathe: 60% of its memory at most.
    const auto spare = physicalMegabytes > 0 ? static_cast<int>(std::min<unsigned long long>(physicalMegabytes * 6 / 10, 65536)) : wanted;
    const int maximum = std::max(2048, std::min(wanted, spare) / 512 * 512);
    const int minimum = std::min(maximum, profile == Profile::Light ? 1024 : 2048);
    return {minimum, maximum};
}

std::map<std::string, std::string> PerformanceProfiles::videoOptions(Profile profile, bool modern)
{
    const bool light = profile == Profile::Light, maximum = profile == Profile::Maximum;
    const std::string distance = light ? "6" : maximum ? "16" : "10";
    if (modern) {
        return {
            {"renderDistance", distance},
            {"simulationDistance", light ? "5" : maximum ? "12" : "8"},
            {"graphicsMode", light ? "0" : "1"},
            {"ao", light ? "false" : "true"},
            {"particles", light ? "2" : maximum ? "0" : "1"},
            {"biomeBlendRadius", light ? "0" : maximum ? "5" : "2"},
            {"renderClouds", light ? "\"false\"" : maximum ? "\"true\"" : "\"fast\""},
            {"entityShadows", light ? "false" : "true"},
            {"entityDistanceScaling", light ? "0.75" : maximum ? "1.25" : "1.0"},
            {"mipmapLevels", light ? "0" : "4"},
        };
    }
    return {
        {"renderDistance", distance},
        {"fancyGraphics", light ? "false" : "true"},
        {"ao", light ? "0" : "2"},
        {"particles", light ? "2" : maximum ? "0" : "1"},
        {"renderClouds", light ? "false" : maximum ? "true" : "fast"},
        {"entityShadows", light ? "false" : "true"},
        {"mipmapLevels", light ? "0" : "4"},
    };
}

std::string PerformanceProfiles::mergeOptions(std::string_view current, const std::map<std::string, std::string>& values, bool modern)
{
    std::vector<std::string> lines;
    {
        std::istringstream input{std::string(current)};
        for (std::string line; std::getline(input, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    std::set<std::string> written;
    for (auto& line : lines) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        if (const auto found = values.find(key); found != values.end()) {
            line = key + ":" + found->second;
            written.insert(key);
        }
    }
    if (lines.empty() && modern) lines.push_back(std::string("version:") + ModernDataVersion);
    for (const auto& [key, value] : values)
        if (!written.count(key)) lines.push_back(key + ":" + value);
    std::string text;
    for (const auto& line : lines) text += line + "\n";
    return text;
}

bool PerformanceProfiles::applyVideoOptions(const std::filesystem::path& gameDirectory, Profile profile, bool modern)
{
    const auto file = gameDirectory / L"options.txt";
    std::string current;
    {
        std::ifstream input(file, std::ios::binary);
        current.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    return writeAtomically(file, mergeOptions(current, videoOptions(profile, modern), modern));
}

std::optional<PerformanceProfiles::Profile> PerformanceProfiles::load(const std::filesystem::path& dataDirectory)
{
    std::ifstream input(dataDirectory / L"performance-profile.txt", std::ios::binary);
    std::string text;
    std::getline(input, text);
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return fromId(text);
}

bool PerformanceProfiles::save(const std::filesystem::path& dataDirectory, Profile profile)
{
    return writeAtomically(dataDirectory / L"performance-profile.txt", id(profile) + "\n");
}
