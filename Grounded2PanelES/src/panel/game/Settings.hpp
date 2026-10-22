// SPDX-License-Identifier: GPL-3.0-or-later
// Panel preferences saved to %LOCALAPPDATA%\Grounded2PanelES\ajustes.ini (UTF-8 key=value).
// Only interface preferences, favourites and saved positions are stored; nothing is applied
// to the game automatically when a new session starts.
#pragma once
#include <string>
#include <vector>

namespace Settings {

struct Waypoint {
    std::wstring name;
    double x = 0, y = 0, z = 0;
};

struct Data {
    int hotkey = 0x77;            // VK_F8
    bool alwaysOnTop = false;
    float opacity = 1.0f;         // 0.5 .. 1
    float uiScale = 1.0f;         // 0.8 .. 1.6 (on top of Windows scaling)
    int page = 0;
    float pickupRadius = 300;
    float clearRadius = 300;
    bool showUntranslated = false;
    bool confirmPermanent = true; // ask before permanent unlocks
    std::vector<std::string> favorites;
    std::vector<std::string> recents;
    std::vector<Waypoint> waypoints;
};

std::string Serialize(const Data& data);
Data Parse(const std::string& text);

Data Load();
void Save(const Data& data);

void AddRecent(Data& data, const std::string& id, size_t max = 12);
bool ToggleFavorite(Data& data, const std::string& id);
bool IsFavorite(const Data& data, const std::string& id);

} // namespace Settings
