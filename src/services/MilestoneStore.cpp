#include "services/MilestoneStore.h"

#include "config/StateStore.h"

#include <string>

std::wstring MilestoneStore::key(std::wstring name)
{
    // Nicknames are ASCII and case-insensitive.
    for (auto& c : name) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return name;
}

void MilestoneStore::load(const std::filesystem::path& directory)
{
    directory_ = directory;
    levels_.clear();
    const auto state = StateStore::read(directory, Key);
    const auto* object = state ? state->objectValue() : nullptr;
    if (!object) return;
    for (const auto& [name, value] : *object) {
        const int level = static_cast<int>(value.numberValue(0));
        if (name.empty() || level < 1 || level > 9) continue;
        // Nicknames are ASCII, so a byte-wise widening is exact.
        levels_[key(std::wstring(name.begin(), name.end()))] = level;
    }
}

bool MilestoneStore::save() const
{
    if (directory_.empty()) return false;
    JsonValue::Object object;
    for (const auto& [name, level] : levels_) {
        std::string ascii;
        for (const wchar_t c : name) ascii.push_back(c < 128 ? static_cast<char>(c) : '?');
        object.insert_or_assign(std::move(ascii), JsonValue(level));
    }
    return StateStore::write(directory_, Key, JsonValue(std::move(object)));
}

int MilestoneStore::celebrated(const std::wstring& name) const
{
    const auto found = levels_.find(key(name));
    return found == levels_.end() ? -1 : found->second;
}

void MilestoneStore::setCelebrated(const std::wstring& name, int level)
{
    if (name.empty()) return;
    levels_[key(name)] = level;
}
