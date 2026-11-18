#include "config/StateStore.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace
{
    // One name for every launcher on this Windows session: they all share the data folder.
    constexpr wchar_t LockName[] = L"Local\\EternalLauncher-DataFolder";
    constexpr DWORD LockTimeoutMs = 10000;
    constexpr std::uintmax_t MaximumBytes = 4 * 1024 * 1024;

    enum class Contents { Missing, Valid, Damaged };

    Contents load(const std::filesystem::path& file, JsonValue::Object& object)
    {
        object.clear();
        std::error_code error;
        if (!std::filesystem::exists(file, error)) return error ? Contents::Damaged : Contents::Missing;
        const auto size = std::filesystem::file_size(file, error);
        if (error || size > MaximumBytes) return Contents::Damaged;
        std::ifstream input(file, std::ios::binary);
        if (!input) return Contents::Damaged;
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        auto parsed = JsonValue::parse(text);
        if (!parsed || !parsed->objectValue()) return Contents::Damaged;
        object = *parsed->objectValue();
        return Contents::Valid;
    }

    bool replace(const std::filesystem::path& temporary, const std::filesystem::path& file)
    {
        // A reader that has the file open for a moment makes the rename fail; try again shortly.
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
            Sleep(25);
        }
        return false;
    }

    // Applies `change` to the file on disk. Must be called with the lock held.
    template <typename Change>
    bool update(const std::filesystem::path& dataDirectory, Change change)
    {
        const auto file = StateStore::filePath(dataDirectory);
        JsonValue::Object object;
        if (load(file, object) == Contents::Damaged) {
            // Never overwrite something unreadable without keeping a copy of it.
            std::error_code ignored;
            auto backup = file;
            backup += L".damaged";
            std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, ignored);
            object.clear();
        }
        change(object);
        object.insert_or_assign("schemaVersion", JsonValue(1));

        std::error_code error;
        std::filesystem::create_directories(dataDirectory, error);
        if (error) return false;
        auto temporary = file;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << JsonValue(std::move(object)).serialize(2) << '\n';
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        if (replace(temporary, file)) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }
}

namespace StateStore
{
    std::filesystem::path filePath(const std::filesystem::path& dataDirectory)
    {
        return dataDirectory / L"state.json";
    }

    std::optional<JsonValue> read(const std::filesystem::path& dataDirectory, std::string_view key)
    {
        // Writes replace the file in one rename, so a read never sees half of one.
        JsonValue::Object object;
        if (load(filePath(dataDirectory), object) != Contents::Valid) return std::nullopt;
        const auto found = object.find(key);
        if (found == object.end()) return std::nullopt;
        return found->second;
    }

    bool write(const std::filesystem::path& dataDirectory, std::string_view key, JsonValue value)
    {
        std::vector<std::pair<std::string, JsonValue>> values;
        values.emplace_back(std::string(key), std::move(value));
        return write(dataDirectory, std::move(values));
    }

    bool write(const std::filesystem::path& dataDirectory, std::vector<std::pair<std::string, JsonValue>> values)
    {
        const Lock lock;
        if (!lock.held()) return false;
        return update(dataDirectory, [&](JsonValue::Object& object) {
            for (auto& [key, value] : values) object.insert_or_assign(key, std::move(value));
        });
    }

    bool erase(const std::filesystem::path& dataDirectory, std::string_view key)
    {
        const Lock lock;
        if (!lock.held()) return false;
        return update(dataDirectory, [&](JsonValue::Object& object) {
            if (const auto found = object.find(key); found != object.end()) object.erase(found);
        });
    }

    Lock::Lock()
    {
        handle_ = CreateMutexW(nullptr, FALSE, LockName);
        if (!handle_) return;
        // A Windows mutex is per thread and reentrant: other threads of this process wait
        // like other processes do, and the owner can take it again. An abandoned lock
        // (a launcher that crashed) is still ours to take.
        const auto wait = WaitForSingleObject(handle_, LockTimeoutMs);
        held_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }

    Lock::~Lock()
    {
        if (held_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }

    std::string utf8(std::wstring_view value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring wide(std::string_view value)
    {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(std::max(size, 0)), L'\0');
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
}
