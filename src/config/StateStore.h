#pragma once

#include "utils/JsonValue.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What the launcher remembers between runs (play time, news read, achievements, the
// nickname...), all in one state.json in the data folder, one top-level key per part.
//
// Each part only ever replaces its own key: a write takes a lock shared by every thread
// and every launcher process, reads the file again from disk, swaps that key and puts
// the whole file back atomically (temporary file + rename). So the playtime thread, the
// window and a second launcher (a development build next to the installed one) never
// undo each other's changes, and a crash mid-write leaves the previous file intact.
namespace StateStore
{
    std::filesystem::path filePath(const std::filesystem::path& dataDirectory);

    // The value under `key`, or nothing when the file or the key is missing or the file
    // cannot be read.
    std::optional<JsonValue> read(const std::filesystem::path& dataDirectory, std::string_view key);

    // Replaces `key` (all of them in one write for the list form). False when nothing was
    // written; the previous file is then untouched.
    bool write(const std::filesystem::path& dataDirectory, std::string_view key, JsonValue value);
    bool write(const std::filesystem::path& dataDirectory, std::vector<std::pair<std::string, JsonValue>> values);

    // Removes `key`. Missing keys are not an error.
    bool erase(const std::filesystem::path& dataDirectory, std::string_view key);

    // Holds the shared lock for a longer job (the layout migration). Writes made by the
    // same thread while it is held do not wait for it.
    class Lock
    {
    public:
        Lock();
        ~Lock();
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        [[nodiscard]] bool held() const { return held_; }
    private:
        void* handle_ = nullptr;
        bool held_ = false;
    };

    // Text <-> UTF-8 helpers shared by the parts that keep wide strings.
    std::string utf8(std::wstring_view value);
    std::wstring wide(std::string_view value);
}
