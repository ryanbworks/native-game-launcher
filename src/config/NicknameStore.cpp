#include "config/NicknameStore.h"

#include "config/StateStore.h"

#include <algorithm>
#include <string>

bool NicknameStore::isValid(std::wstring_view nickname)
{
    // Same rules accepted by the field and by the game's offline launch.
    return nickname.size() >= 3 && nickname.size() <= 16 &&
        std::all_of(nickname.begin(), nickname.end(), [](wchar_t character)
        {
            return (character >= L'a' && character <= L'z') ||
                   (character >= L'A' && character <= L'Z') ||
                   (character >= L'0' && character <= L'9') || character == L'_';
        });
}

std::wstring NicknameStore::load(const std::filesystem::path& dataDirectory)
{
    const auto value = StateStore::read(dataDirectory, Key);
    const auto* text = value ? value->stringValue() : nullptr;
    if (!text || text->size() > 16) return {};
    // Valid nicknames are ASCII, so widening byte by byte is exact.
    const std::wstring nickname(text->begin(), text->end());
    return isValid(nickname) ? nickname : std::wstring{};
}

bool NicknameStore::save(
    const std::filesystem::path& dataDirectory,
    std::wstring_view nickname)
{
    if (!isValid(nickname)) return false;
    std::string ascii;
    for (const auto character : nickname) ascii.push_back(static_cast<char>(character));
    return StateStore::write(dataDirectory, Key, JsonValue(std::move(ascii)));
}
