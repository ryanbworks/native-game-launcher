#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Where everything lives inside the data folder (%APPDATA%\.eternal-launcher):
//
//   settings.json              Settings screen
//   state.json                 what the launcher remembers (see StateStore)
//   account\account.dat        Conta Eternal session, DPAPI-encrypted
//   games\eternal\             the group's modpack instance
//   games\vanilla-1.8.9\       unmodified 1.8.9
//   java\17\  java\8\          portable Java runtimes
//   skins\                     the player's own skins
//   cache\                     safe to delete: downloads, skin cache, authlib-injector,
//                              the verified-files list
//
// Launchers up to 3.2.1 kept all of this loose at the top of the folder. migrate() moves an
// old folder to this layout once. The big folders are only renamed, which can fail while a
// file inside is open (a game still running); until a later start manages it, the accessors
// below keep answering with the old place, so nothing is ever looked for where it is not.
namespace DataLayout
{
    std::filesystem::path eternalGame(const std::filesystem::path& data);
    std::filesystem::path vanillaGame(const std::filesystem::path& data);
    std::filesystem::path java17(const std::filesystem::path& data);
    std::filesystem::path java8(const std::filesystem::path& data);
    std::filesystem::path account(const std::filesystem::path& data);
    std::filesystem::path skins(const std::filesystem::path& data);
    std::filesystem::path cache(const std::filesystem::path& data);
    std::filesystem::path downloads(const std::filesystem::path& data);
    std::filesystem::path skinCache(const std::filesystem::path& data);
    std::filesystem::path authlibInjector(const std::filesystem::path& data);
    std::filesystem::path verifiedFiles(const std::filesystem::path& data);

    // The modpack folder's name before 3.3.0, still accepted in settings.json.
    std::filesystem::path legacyEternalGame(const std::filesystem::path& data);

    struct MigrationReport
    {
        // Something was moved or converted.
        bool changed = false;
        // What could not be moved yet (kept where it was; retried on the next start).
        std::vector<std::wstring> pending;
    };

    // Brings an old data folder to the current layout. Safe to run on every start: it only
    // acts on what is still in an old place, and never overwrites something already in the
    // new one. Text files become keys of state.json and are deleted only after that file
    // was written.
    MigrationReport migrate(const std::filesystem::path& data);
}
