#include "services/Achievements.h"

#include "config/StateStore.h"
#include "services/HttpDownloadClient.h"
#include "utils/JsonValue.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace
{
    std::string utf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring wide(const std::string& value)
    {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(std::max(size, 0)), L'\0');
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    int number(const JsonValue* document, const char* key)
    {
        const auto* field = document ? document->find(key) : nullptr;
        return field ? std::clamp(static_cast<int>(field->numberValue(0)), 0, 1000000) : 0;
    }

    bool flag(const JsonValue* document, const char* key)
    {
        const auto* field = document ? document->find(key) : nullptr;
        return field && field->boolValue(false);
    }

    bool validNickname(std::wstring_view name)
    {
        return name.size() >= 3 && name.size() <= 16 && std::all_of(name.begin(), name.end(), [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
        });
    }

    std::wstring monthName(const std::string& month)
    {
        static constexpr const wchar_t* Names[] = {L"janeiro", L"fevereiro", L"março", L"abril", L"maio", L"junho",
                                                   L"julho", L"agosto", L"setembro", L"outubro", L"novembro", L"dezembro"};
        if (month.size() != 7) return L"este mês";
        const int index = std::atoi(month.substr(5, 2).c_str());
        return index >= 1 && index <= 12 ? Names[index - 1] : L"este mês";
    }
}

AchievementStats Achievements::parse(std::string_view playtimeJson, std::string_view galleryJson)
{
    AchievementStats stats;
    const auto playtime = JsonValue::parse(playtimeJson);
    if (!playtime || !playtime->objectValue()) return stats;
    stats.known = true;
    stats.daysPlayed = number(&*playtime, "daysPlayed");
    stats.currentStreak = number(&*playtime, "currentStreak");
    stats.bestStreak = std::max(number(&*playtime, "bestStreak"), stats.currentStreak);
    stats.monthsPlayed = number(&*playtime, "monthsPlayed");
    stats.playedThisMonth = flag(&*playtime, "playedThisMonth");
    stats.nightOwl = flag(&*playtime, "nightOwl");
    stats.rank = number(&*playtime, "rank");
    if (const auto* month = playtime->find("month"); month && month->stringValue() && month->stringValue()->size() == 7)
        stats.month = *month->stringValue();
    // The gallery is optional: without it the sharing achievements just wait.
    if (const auto gallery = JsonValue::parse(galleryJson); gallery && gallery->objectValue()) {
        stats.photosShared = number(&*gallery, "photos");
        stats.skinsShared = number(&*gallery, "skins");
    }
    return stats;
}

AchievementStats Achievements::fetch(std::wstring_view baseUrl, std::wstring_view nickname, std::stop_token stop)
{
    if (!validNickname(nickname)) return {};
    std::wstring base(baseUrl);
    while (!base.empty() && base.back() == L'/') base.pop_back();
    const auto playtime = HttpDownloadClient{}.getText(base + L"/api/playtime/achievements?name=" + std::wstring(nickname), stop);
    if (!playtime.success || stop.stop_requested()) return {};
    const auto gallery = HttpDownloadClient{}.getText(base + L"/api/gallery/stats?name=" + std::wstring(nickname), stop);
    return parse(playtime.content, gallery.success ? gallery.content : std::string{});
}

std::vector<Achievement> Achievements::evaluate(const AchievementStats& s, const std::set<std::string>& earned)
{
    using Icons::Icon;
    const std::string monthly = "mes-" + (s.month.empty() ? std::string("atual") : s.month);
    std::vector<Achievement> list = {
        {"primeiros-passos", L"Primeiros passos", L"Jogue no servidor do grupo", Icon::Play, std::min(s.daysPlayed, 1), 1, false, L"dia"},
        {"sequencia-3", L"Três seguidos", L"Jogue 3 dias seguidos", Icon::Clock, s.bestStreak, 3, false, L"dias"},
        {"sequencia-7", L"Semana cheia", L"Jogue 7 dias seguidos", Icon::Calendar, s.bestStreak, 7, false, L"dias"},
        {"sequencia-30", L"Mês sem parar", L"Jogue 30 dias seguidos", Icon::Crown, s.bestStreak, 30, false, L"dias"},
        {monthly, L"Presença do mês", L"Jogue pelo menos um dia em " + monthName(s.month), Icon::Check, s.playedThisMonth ? 1 : 0, 1, false, L"dia"},
        {"frequentador", L"Frequentador", L"Jogue em 30 dias diferentes", Icon::Calendar, s.daysPlayed, 30, false, L"dias"},
        {"veterano", L"Veterano", L"Jogue em 3 meses diferentes", Icon::Shield, s.monthsPlayed, 3, false, L"meses"},
        {"coruja", L"Coruja", L"Jogue entre meia-noite e 5h", Icon::Sparkle, s.nightOwl ? 1 : 0, 1, false, L""},
        {"primeira-captura", L"Primeira captura", L"Compartilhe uma captura com o grupo", Icon::Image, std::min(s.photosShared, 1), 1, false, L""},
        {"fotografo", L"Fotógrafo", L"Compartilhe 10 capturas", Icon::Image, s.photosShared, 10, false, L"capturas"},
        {"estilista", L"Estilista", L"Compartilhe uma skin em Explorar", Icon::Shirt, std::min(s.skinsShared, 1), 1, false, L""},
        {"podio", L"No pódio", L"Fique entre os 3 primeiros do ranking", Icon::Crown, s.rank >= 1 && s.rank <= 3 ? 1 : 0, 1, false, L""},
    };
    for (auto& achievement : list) {
        achievement.progress = std::clamp(achievement.progress, 0, achievement.target);
        achievement.unlocked = achievement.progress >= achievement.target || earned.count(achievement.id) > 0;
        if (achievement.unlocked) achievement.progress = achievement.target;
    }
    return list;
}

// ---------------------------------------------------------------------------------------------

std::wstring AchievementStore::key(std::wstring account)
{
    std::transform(account.begin(), account.end(), account.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return account;
}

void AchievementStore::load(const std::filesystem::path& directory)
{
    directory_ = directory;
    earned_.clear();
    const auto state = StateStore::read(directory, Key);
    const auto* object = state ? state->objectValue() : nullptr;
    if (!object) return;
    for (const auto& [account, list] : *object) {
        const auto* ids = list.arrayValue();
        if (account.empty() || !ids) continue;
        auto& earned = earned_[key(wide(account))];
        for (const auto& id : *ids)
            if (const auto* text = id.stringValue(); text && !text->empty()) earned.insert(*text);
    }
}

bool AchievementStore::save() const
{
    if (directory_.empty()) return false;
    JsonValue::Object object;
    for (const auto& [account, ids] : earned_) {
        JsonValue::Array list;
        for (const auto& id : ids) list.push_back(JsonValue(id));
        object.insert_or_assign(utf8(account), JsonValue(std::move(list)));
    }
    return StateStore::write(directory_, Key, JsonValue(std::move(object)));
}

bool AchievementStore::knows(const std::wstring& account) const
{
    return earned_.count(key(account)) > 0;
}

const std::set<std::string>& AchievementStore::earned(const std::wstring& account) const
{
    static const std::set<std::string> none;
    const auto found = earned_.find(key(account));
    return found == earned_.end() ? none : found->second;
}

void AchievementStore::add(const std::wstring& account, const std::string& id)
{
    earned_[key(account)].insert(id);
}
