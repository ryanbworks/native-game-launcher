#include "config/DataLayout.h"

#include "config/StateStore.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

namespace
{
    namespace fs = std::filesystem;

    bool present(const fs::path& path)
    {
        std::error_code ignored;
        return fs::exists(path, ignored);
    }

    // The new place, unless only the old one exists (a move that has not happened yet).
    fs::path settled(const fs::path& current, const fs::path& legacy)
    {
        return !present(current) && present(legacy) ? legacy : current;
    }

    std::optional<std::string> readText(const fs::path& file)
    {
        std::error_code error;
        const auto size = fs::file_size(file, error);
        if (error || size > 16 * 1024 * 1024) return std::nullopt;
        std::ifstream input(file, std::ios::binary);
        if (!input) return std::nullopt;
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::vector<std::string> lines(const std::string& text)
    {
        std::vector<std::string> result;
        std::istringstream input(text);
        for (std::string line; std::getline(input, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            result.push_back(std::move(line));
        }
        return result;
    }

    std::string trimmed(std::string text)
    {
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
        return text;
    }

    bool writeAtomically(const fs::path& file, const std::string& content)
    {
        auto temporary = file;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << content;
            output.flush();
            if (!output) return false;
        }
        if (MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }

    // ---- Paths that moved, and the files that point into them ------------------------------

    struct Moved { std::wstring from, to; };

    // `path` with a moved folder swapped for its new place, compared without case like Windows does.
    std::optional<std::wstring> rebase(const std::wstring& path, const std::vector<Moved>& moved)
    {
        for (const auto& move : moved) {
            const auto length = move.from.size();
            if (path.size() < length) continue;
            if (CompareStringOrdinal(path.data(), static_cast<int>(length), move.from.data(), static_cast<int>(length), TRUE) != CSTR_EQUAL)
                continue;
            if (path.size() > length && path[length] != L'\\' && path[length] != L'/') continue;
            return move.to + path.substr(length);
        }
        return std::nullopt;
    }

    // eternal-classpath.txt lists absolute paths: the 1.8.9 launches from it without
    // asking Mojang again, so it must follow the folder.
    void rebaseClasspath(const fs::path& game, const std::vector<Moved>& moved)
    {
        const auto file = game / L"eternal-classpath.txt";
        const auto text = readText(file);
        if (!text) return;
        std::string output;
        bool changed = false;
        for (const auto& line : lines(*text)) {
            if (line.empty()) continue;
            if (const auto rebased = rebase(StateStore::wide(line), moved)) {
                output += StateStore::utf8(*rebased) + "\n";
                changed = true;
            } else {
                output += line + "\n";
            }
        }
        if (changed) (void)writeAtomically(file, output);
    }

    // The verified-files list is keyed by lower-case absolute path (VerifiedFileCache::key).
    // Rewriting it saves hashing every game file again after the move.
    void moveVerifiedFiles(const fs::path& legacy, const fs::path& current, const std::vector<Moved>& moved)
    {
        std::error_code ignored;
        if (!present(legacy)) return;
        if (present(current)) { fs::remove(legacy, ignored); return; }
        std::vector<Moved> lowered;
        for (auto move : moved) {
            for (auto* text : {&move.from, &move.to})
                std::transform(text->begin(), text->end(), text->begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            lowered.push_back(std::move(move));
        }
        const auto text = readText(legacy);
        if (!text) return;
        std::string output;
        for (const auto& line : lines(*text)) {
            // algorithm, size, write time, hash, path
            std::size_t tab = std::string::npos;
            for (int field = 0; field < 4; ++field) {
                tab = line.find('\t', tab == std::string::npos ? 0 : tab + 1);
                if (tab == std::string::npos) break;
            }
            if (tab == std::string::npos) continue;
            const auto rebased = rebase(StateStore::wide(line.substr(tab + 1)), lowered);
            output += rebased ? line.substr(0, tab + 1) + StateStore::utf8(*rebased) + "\n" : line + "\n";
        }
        fs::create_directories(current.parent_path(), ignored);
        if (writeAtomically(current, output)) fs::remove(legacy, ignored);
    }

    // ---- Old text files -> state.json keys -------------------------------------------------

    bool validNickname(const std::string& name)
    {
        return name.size() >= 3 && name.size() <= 16 && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        });
    }

    std::optional<JsonValue> nickname(const std::string& text)
    {
        auto name = trimmed(text.substr(0, text.find('\n')));
        return validNickname(name) ? std::optional<JsonValue>(JsonValue(std::move(name))) : std::nullopt;
    }

    std::optional<JsonValue> singleLine(const std::string& text)
    {
        auto value = trimmed(text.substr(0, text.find('\n')));
        return value.empty() ? std::nullopt : std::optional<JsonValue>(JsonValue(std::move(value)));
    }

    std::optional<JsonValue> playtime(const std::string& text)
    {
        const auto parsed = JsonValue::parse(text);
        if (!parsed || !parsed->objectValue()) return std::nullopt;
        JsonValue::Object record;
        for (const char* key : {"totalSeconds", "lastSessionSeconds", "lastPlayed"}) {
            const auto* value = parsed->find(key);
            record.insert_or_assign(key, JsonValue(std::max(0.0, value ? value->numberValue(0) : 0.0)));
        }
        return JsonValue(std::move(record));
    }

    // "account<TAB>id" lines -> {"account": ["id", ...]}
    std::optional<JsonValue> achievements(const std::string& text)
    {
        std::map<std::string, JsonValue::Array, std::less<>> earned;
        for (const auto& line : lines(text)) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
            auto account = StateStore::utf8([&] {
                auto name = StateStore::wide(line.substr(0, tab));
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                return name;
            }());
            earned[account].push_back(JsonValue(line.substr(tab + 1)));
        }
        JsonValue::Object object;
        for (auto& [account, ids] : earned) object.insert_or_assign(account, JsonValue(std::move(ids)));
        return JsonValue(std::move(object));
    }

    // "name<TAB>level" lines -> {"name": level}
    std::optional<JsonValue> milestones(const std::string& text)
    {
        JsonValue::Object object;
        for (const auto& line : lines(text)) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
            const int level = line[tab + 1] - '0';
            if (level < 1 || level > 9) continue;
            auto name = line.substr(0, tab);
            std::transform(name.begin(), name.end(), name.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
            object.insert_or_assign(std::move(name), JsonValue(level));
        }
        return JsonValue(std::move(object));
    }

    // "s<TAB>key" / "d<TAB>key" lines -> {"seen": [...], "dismissed": [...]}
    std::optional<JsonValue> news(const std::string& text)
    {
        JsonValue::Array seen, dismissed;
        for (const auto& line : lines(text)) {
            if (line.size() < 3 || line[1] != '\t') continue;
            if (line[0] == 's') seen.push_back(JsonValue(line.substr(2)));
            else if (line[0] == 'd') dismissed.push_back(JsonValue(line.substr(2)));
        }
        JsonValue::Object object;
        object.insert_or_assign("seen", JsonValue(std::move(seen)));
        object.insert_or_assign("dismissed", JsonValue(std::move(dismissed)));
        return JsonValue(std::move(object));
    }

    std::optional<JsonValue> modpackSnapshot(const std::string& text)
    {
        JsonValue::Array files;
        for (const auto& line : lines(text)) if (!line.empty()) files.push_back(JsonValue(line));
        return JsonValue(std::move(files));
    }

    // "id<TAB>token<TAB>source" lines -> [{"id","token","source"}]
    std::optional<JsonValue> shared(const std::string& text)
    {
        JsonValue::Array entries;
        for (const auto& line : lines(text)) {
            const auto first = line.find('\t');
            const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
            if (second == std::string::npos) continue;
            JsonValue::Object entry;
            entry.insert_or_assign("id", JsonValue(line.substr(0, first)));
            entry.insert_or_assign("token", JsonValue(line.substr(first + 1, second - first - 1)));
            entry.insert_or_assign("source", JsonValue(line.substr(second + 1)));
            entries.push_back(JsonValue(std::move(entry)));
        }
        return JsonValue(std::move(entries));
    }

    std::optional<JsonValue> marker(const std::string&) { return JsonValue(true); }

    struct LegacyFile
    {
        const wchar_t* file;
        const char* key;
        std::optional<JsonValue> (*convert)(const std::string&);
    };

    constexpr LegacyFile LegacyFiles[] = {
        {L"nickname.txt", "nickname", nickname},
        {L"performance-profile.txt", "performanceProfile", singleLine},
        {L"last-version.txt", "lastSeenVersion", singleLine},
        {L"playtime.json", "playtime", playtime},
        {L"playtime-imported.marker", "playtimeImported", marker},
        {L"achievements.txt", "achievements", achievements},
        {L"milestones.txt", "milestones", milestones},
        {L"news-state.txt", "news", news},
        {L"modpack-snapshot.txt", "modpackSnapshot", modpackSnapshot},
        {L"gallery-shared.txt", "galleryShared", shared},
        {L"skins-shared.txt", "skinsShared", shared},
    };

    void convertTextFiles(const fs::path& data, DataLayout::MigrationReport& report)
    {
        std::vector<std::pair<std::string, JsonValue>> values;
        std::vector<fs::path> converted;
        for (const auto& legacy : LegacyFiles) {
            const auto file = data / legacy.file;
            if (!present(file)) continue;
            // A key already in state.json is newer than a leftover file: keep the key.
            if (StateStore::read(data, legacy.key)) { converted.push_back(file); continue; }
            const auto text = readText(file);
            if (!text) { report.pending.push_back(legacy.file); continue; }
            if (auto value = legacy.convert(*text)) values.emplace_back(legacy.key, std::move(*value));
            // A file with nothing usable in it is dropped like the old reader ignored it.
            converted.push_back(file);
        }
        if (!values.empty() && !StateStore::write(data, std::move(values))) {
            report.pending.push_back(L"state.json");
            return;
        }
        std::error_code ignored;
        for (const auto& file : converted) {
            fs::remove(file, ignored);
            auto temporary = file;
            temporary += L".tmp";
            fs::remove(temporary, ignored);
        }
        if (!converted.empty()) report.changed = true;
    }

    // ---- Folders ---------------------------------------------------------------------------

    // Renames `from` to `to` on the same disk. Nothing happens when `to` already exists.
    bool moveFolder(const fs::path& from, const fs::path& to, std::vector<Moved>& moved, DataLayout::MigrationReport& report)
    {
        if (!present(from)) return false;
        if (present(to)) {
            report.pending.push_back(from.filename().wstring());
            return false;
        }
        std::error_code error;
        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);
        if (error) {
            // Usually a game still running from there; the old place keeps working meanwhile.
            report.pending.push_back(from.filename().wstring());
            return false;
        }
        moved.push_back({from.wstring(), to.wstring()});
        report.changed = true;
        return true;
    }
}

namespace DataLayout
{
    std::filesystem::path eternalGame(const std::filesystem::path& data) { return settled(data / L"games" / L"eternal", legacyEternalGame(data)); }
    std::filesystem::path vanillaGame(const std::filesystem::path& data) { return settled(data / L"games" / L"vanilla-1.8.9", data / L"minecraft-1.8.9"); }
    std::filesystem::path java17(const std::filesystem::path& data) { return settled(data / L"java" / L"17", data / L"runtime"); }
    std::filesystem::path java8(const std::filesystem::path& data) { return settled(data / L"java" / L"8", data / L"runtime-8"); }
    std::filesystem::path account(const std::filesystem::path& data)
    {
        return !present(data / L"account" / L"account.dat") && present(data / L"account.dat") ? data : data / L"account";
    }
    std::filesystem::path skins(const std::filesystem::path& data) { return data / L"skins"; }
    std::filesystem::path cache(const std::filesystem::path& data) { return data / L"cache"; }
    std::filesystem::path downloads(const std::filesystem::path& data) { return cache(data) / L"downloads"; }
    std::filesystem::path skinCache(const std::filesystem::path& data) { return cache(data) / L"skins"; }
    std::filesystem::path authlibInjector(const std::filesystem::path& data) { return cache(data) / L"authlib-injector"; }
    std::filesystem::path verifiedFiles(const std::filesystem::path& data) { return cache(data) / L"verified-files.txt"; }
    std::filesystem::path legacyEternalGame(const std::filesystem::path& data) { return data / L"minecraft"; }

    MigrationReport migrate(const std::filesystem::path& data)
    {
        MigrationReport report;
        if (!present(data)) return report;
        // One launcher at a time: a second one (another build) waits instead of moving the
        // same folders under its feet.
        const StateStore::Lock lock;
        if (!lock.held()) { report.pending.push_back(L"lock"); return report; }

        std::vector<Moved> moved;
        std::error_code ignored;

        // authlib-injector lived inside the Java 17 folder; it is a download, so it goes to
        // the cache first (or is dropped: it is fetched again when needed).
        const auto oldAuthlib = data / L"runtime" / L"authlib-injector";
        if (present(oldAuthlib)) {
            if (present(authlibInjector(data)) || !moveFolder(oldAuthlib, authlibInjector(data), moved, report))
                fs::remove_all(oldAuthlib, ignored);
        }

        struct Folder { fs::path from, to; bool disposable; };
        const Folder folders[] = {
            {legacyEternalGame(data), data / L"games" / L"eternal", false},
            {data / L"minecraft-1.8.9", data / L"games" / L"vanilla-1.8.9", false},
            {data / L"runtime", data / L"java" / L"17", false},
            {data / L"runtime-8", data / L"java" / L"8", false},
            {data / L"downloads", downloads(data), true},
            {data / L"skin-cache", skinCache(data), true},
        };
        for (const auto& folder : folders) {
            if (!present(folder.from)) continue;
            // Never carry a leftover authlib-injector into java\17.
            if (folder.from == data / L"runtime" && present(oldAuthlib)) { report.pending.push_back(L"runtime"); continue; }
            // An old cache next to a new one has nothing the new one needs.
            if (folder.disposable && present(folder.to)) { fs::remove_all(folder.from, ignored); report.changed = true; continue; }
            moveFolder(folder.from, folder.to, moved, report);
        }
        for (const auto& game : {data / L"games" / L"eternal", data / L"games" / L"vanilla-1.8.9"})
            if (!moved.empty()) rebaseClasspath(game, moved);

        moveVerifiedFiles(data / L"verified-files.txt", verifiedFiles(data), moved);

        // The session file is renamed as is: it is encrypted for this Windows user.
        if (present(data / L"account.dat") && !present(data / L"account" / L"account.dat")) {
            fs::create_directories(data / L"account", ignored);
            if (MoveFileExW((data / L"account.dat").c_str(), (data / L"account" / L"account.dat").c_str(), MOVEFILE_WRITE_THROUGH))
                report.changed = true;
            else
                report.pending.push_back(L"account.dat");
        }

        convertTextFiles(data, report);
        return report;
    }
}
