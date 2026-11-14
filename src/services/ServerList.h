#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Keeps the group's server in Minecraft's multiplayer list (servers.dat, uncompressed NBT),
// so the game opens with it ready to join. The player's other servers stay as they are.
namespace ServerList
{
    // The new servers.dat content: entries whose address is on `retiredDomain` (the group's
    // old address) move to `address`, and when no entry has `address` one named `name` is
    // added at the top. Empty when `current` is not a servers.dat this code understands.
    std::optional<std::string> merge(std::string_view current, std::string_view name, std::string_view address,
                                     std::string_view retiredDomain);
    // Applies merge to <gameDirectory>/servers.dat (created when missing), atomically.
    bool ensure(const std::filesystem::path& gameDirectory, std::string_view name, std::string_view address,
                std::string_view retiredDomain);
    // The addresses listed in a servers.dat, in order (for tests and diagnostics).
    std::optional<std::string> addresses(std::string_view content);
}
