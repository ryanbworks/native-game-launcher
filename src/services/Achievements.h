#pragma once

#include "utils/Icons.h"

#include <filesystem>
#include <map>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// What the achievements need about one player: days, streaks and nights from the play-time
// server (/api/playtime/achievements) and what they shared from the gallery (/api/gallery/stats).
struct AchievementStats
{
    bool known = false;
    int daysPlayed = 0;
    int currentStreak = 0;
    int bestStreak = 0;
    int monthsPlayed = 0;
    bool playedThisMonth = false;
    bool nightOwl = false;
    int rank = 0;              // 0 while not ranked
    int photosShared = 0;
    int skinsShared = 0;
    std::string month;         // YYYY-MM on the server, for the monthly achievement
};

struct Achievement
{
    std::string id;            // the monthly one carries its month: "mes-2026-11"
    std::wstring title;
    std::wstring description;
    Icons::Icon icon;
    int progress = 0;          // toward `target`
    int target = 1;
    bool unlocked = false;
    std::wstring unit;         // "dias", "capturas"... for the progress line
};

namespace Achievements
{
    AchievementStats parse(std::string_view playtimeJson, std::string_view galleryJson);
    // Blocking; `baseUrl` is the file server that publishes both APIs.
    AchievementStats fetch(std::wstring_view baseUrl, std::wstring_view nickname, std::stop_token stop = {});
    // Every achievement, unlocked by the stats or by `earned` (ids already won before:
    // the podium, for one, stays won after dropping out of the top 3).
    std::vector<Achievement> evaluate(const AchievementStats& stats, const std::set<std::string>& earned);
}

// Which achievements each account already won, under "achievements" in state.json,
// so each one is announced once.
class AchievementStore
{
public:
    static constexpr const char* Key = "achievements";
    void load(const std::filesystem::path& directory);
    bool save() const;
    bool knows(const std::wstring& account) const;
    const std::set<std::string>& earned(const std::wstring& account) const;
    void add(const std::wstring& account, const std::string& id);
private:
    static std::wstring key(std::wstring account);
    std::filesystem::path directory_;
    std::map<std::wstring, std::set<std::string>> earned_;
};
