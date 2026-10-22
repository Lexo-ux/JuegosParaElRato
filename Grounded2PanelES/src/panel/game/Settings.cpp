// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#include "common/Paths.hpp"
#include <filesystem>
#include <fstream>
#endif

namespace Settings {
namespace {

std::string ToUtf8(const std::wstring& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = static_cast<uint16_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            uint32_t low = static_cast<uint16_t>(w[i + 1]);
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            ++i;
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 30) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (extra && i + extra >= s.size()) break;  // truncated sequence
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += 1 + extra;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<wchar_t>(cp);
        }
    }
    return out;
}

// Waypoint names may contain anything except line breaks and the field separator.
std::string Clean(std::string text) {
    for (auto& c : text)
        if (c == '\n' || c == '\r' || c == '|') c = ' ';
    return text;
}

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

float Clamp(float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

} // namespace

std::string Serialize(const Data& d) {
    std::ostringstream out;
    out << "# Grounded 2 · Panel en español — ajustes de la interfaz\n";
    out << "hotkey=" << d.hotkey << "\n";
    out << "alwaysOnTop=" << (d.alwaysOnTop ? 1 : 0) << "\n";
    out << "opacity=" << d.opacity << "\n";
    out << "uiScale=" << d.uiScale << "\n";
    out << "page=" << d.page << "\n";
    out << "pickupRadius=" << d.pickupRadius << "\n";
    out << "clearRadius=" << d.clearRadius << "\n";
    out << "showUntranslated=" << (d.showUntranslated ? 1 : 0) << "\n";
    out << "confirmPermanent=" << (d.confirmPermanent ? 1 : 0) << "\n";
    for (const auto& id : d.favorites) out << "favorite=" << id << "\n";
    for (const auto& id : d.recents) out << "recent=" << id << "\n";
    char coords[128];
    for (const auto& w : d.waypoints) {
        std::snprintf(coords, sizeof(coords), "%.2f|%.2f|%.2f", w.x, w.y, w.z);
        out << "waypoint=" << Clean(ToUtf8(w.name)) << "|" << coords << "\n";
    }
    return out.str();
}

Data Parse(const std::string& text) {
    Data d;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);  // BOM
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        const float number = static_cast<float>(std::atof(value.c_str()));
        if (key == "hotkey") {
            int vk = std::atoi(value.c_str());
            if (vk > 0 && vk < 256) d.hotkey = vk;
        } else if (key == "alwaysOnTop") d.alwaysOnTop = value == "1";
        else if (key == "opacity") d.opacity = Clamp(number, 0.5f, 1.0f, 1.0f);
        else if (key == "uiScale") d.uiScale = Clamp(number, 0.8f, 1.6f, 1.0f);
        else if (key == "page") d.page = std::clamp(std::atoi(value.c_str()), 0, 64);
        else if (key == "pickupRadius") d.pickupRadius = Clamp(number, 25, 2000, 300);
        else if (key == "clearRadius") d.clearRadius = Clamp(number, 25, 2000, 300);
        else if (key == "showUntranslated") d.showUntranslated = value == "1";
        else if (key == "confirmPermanent") d.confirmPermanent = value != "0";
        else if (key == "favorite" && ValidId(value) && !IsFavorite(d, value) && d.favorites.size() < 500) d.favorites.push_back(value);
        else if (key == "recent" && ValidId(value) && d.recents.size() < 12) d.recents.push_back(value);
        else if (key == "waypoint" && d.waypoints.size() < 200) {
            auto parts = Split(value, '|');
            if (parts.size() != 4) continue;
            Waypoint w;
            w.name = FromUtf8(parts[0]);
            w.x = std::atof(parts[1].c_str());
            w.y = std::atof(parts[2].c_str());
            w.z = std::atof(parts[3].c_str());
            if (w.name.empty() || !std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) continue;
            d.waypoints.push_back(std::move(w));
        }
    }
    return d;
}

#ifdef _WIN32
Data Load() {
    std::ifstream file(std::filesystem::path(Paths::DataDir() + L"\\ajustes.ini"), std::ios::binary);
    if (!file) return {};
    std::ostringstream content;
    content << file.rdbuf();
    return Parse(content.str());
}

void Save(const Data& data) {
    const std::wstring path = Paths::DataDir() + L"\\ajustes.ini";
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) return;
        file << Serialize(data);
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
#endif

void AddRecent(Data& data, const std::string& id, size_t max) {
    data.recents.erase(std::remove(data.recents.begin(), data.recents.end(), id), data.recents.end());
    data.recents.insert(data.recents.begin(), id);
    if (data.recents.size() > max) data.recents.resize(max);
}

bool ToggleFavorite(Data& data, const std::string& id) {
    auto it = std::find(data.favorites.begin(), data.favorites.end(), id);
    if (it != data.favorites.end()) {
        data.favorites.erase(it);
        return false;
    }
    data.favorites.push_back(id);
    return true;
}

bool IsFavorite(const Data& data, const std::string& id) {
    return std::find(data.favorites.begin(), data.favorites.end(), id) != data.favorites.end();
}

} // namespace Settings
