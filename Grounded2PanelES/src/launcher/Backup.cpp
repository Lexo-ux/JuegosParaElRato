// SPDX-License-Identifier: GPL-3.0-or-later
// Copies the Steam saves (%USERPROFILE%\Saved Games\Grounded2) to a dated folder next to them.
#include "Launcher.hpp"

#include "common/Paths.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace Backup {

std::wstring Folder() {
    const std::wstring saves = Paths::SavesDir();
    return saves.empty() ? std::wstring() : saves + L" - Copias del panel";
}

int Count() {
    std::error_code ec;
    const std::wstring folder = Folder();
    if (folder.empty() || !fs::is_directory(folder, ec)) return 0;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec))
        if (entry.is_directory(ec)) ++count;
    return count;
}

std::wstring Run(std::wstring& message) {
    std::error_code ec;
    const std::wstring saves = Paths::SavesDir();
    if (saves.empty() || !fs::is_directory(saves, ec)) {
        message = L"No se encontró la carpeta de partidas de Steam (Partidas guardadas\\Grounded2).";
        return {};
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"%04u-%02u-%02u_%02u-%02u-%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const fs::path destination = fs::path(Folder()) / stamp;
    fs::create_directories(destination, ec);
    fs::copy(saves, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        message = L"La copia falló: " + Paths::Wide(ec.message());
        return {};
    }
    // Keep the 15 most recent backups.
    std::vector<fs::path> all;
    for (const auto& entry : fs::directory_iterator(Folder(), ec))
        if (entry.is_directory(ec)) all.push_back(entry.path());
    std::sort(all.begin(), all.end());
    while (all.size() > 15) {
        fs::remove_all(all.front(), ec);
        all.erase(all.begin());
    }
    message = L"Copia guardada en " + destination.wstring();
    return destination.wstring();
}

} // namespace Backup
