// SPDX-License-Identifier: GPL-3.0-or-later
#include "Launcher.hpp"

#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace Steam {
namespace {

std::wstring RegistryString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return buffer;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring FileVersion(const std::wstring& path) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return {};
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return {};
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
           std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionLS));
}

std::wstring FindShipping(const fs::path& gameDir) {
    std::error_code ec;
    for (const char* sub : {"Augusta/Binaries/Win64", "Augusta/Binaries/WinGRTS"}) {
        fs::path dir = gameDir / sub;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const std::wstring name = entry.path().filename().wstring();
            if (name.starts_with(L"Grounded2") && name.ends_with(L"-Shipping.exe")) return entry.path().wstring();
        }
    }
    return {};
}

} // namespace

Install Detect(const std::wstring& launcherDir) {
    Install install;
    std::error_code ec;
    // Deployed by Vortex into <game>\Augusta\Binaries\Win64\Grounded2PanelES\ -> the game is 4 levels up.
    fs::path here(launcherDir);
    fs::path candidate = here.parent_path().parent_path().parent_path().parent_path();
    if (fs::exists(candidate / "Augusta", ec)) {
        install.gameDir = candidate.wstring();
        install.exe = FindShipping(candidate);
    }
    install.steamPath = RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (install.steamPath.empty())
        install.steamPath = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (install.gameDir.empty() && !install.steamPath.empty()) {
        std::vector<fs::path> libraries = {fs::path(install.steamPath)};
        const std::string vdf = ReadFile(fs::path(install.steamPath) / "steamapps" / "libraryfolders.vdf");
        static const std::regex pathEntry("\"path\"\\s+\"([^\"]+)\"");
        for (std::sregex_iterator it(vdf.begin(), vdf.end(), pathEntry), end; it != end; ++it) {
            std::string path = (*it)[1].str();
            std::string unescaped;
            for (size_t i = 0; i < path.size(); ++i) {
                if (path[i] == '\\' && i + 1 < path.size() && path[i + 1] == '\\') ++i;
                unescaped += path[i];
            }
            libraries.emplace_back(Widen(unescaped));
        }
        for (const auto& library : libraries) {
            const fs::path manifest = library / "steamapps" / "appmanifest_2661300.acf";
            if (!fs::exists(manifest, ec)) continue;
            const std::string acf = ReadFile(manifest);
            static const std::regex dirEntry("\"installdir\"\\s+\"([^\"]+)\"");
            std::smatch match;
            const std::string folder = std::regex_search(acf, match, dirEntry) ? match[1].str() : "Grounded2";
            const fs::path gameDir = library / "steamapps" / "common" / Widen(folder);
            if (!fs::exists(gameDir, ec)) continue;
            install.gameDir = gameDir.wstring();
            install.exe = FindShipping(gameDir);
            break;
        }
    }
    if (!install.exe.empty()) install.version = FileVersion(install.exe);
    return install;
}

bool Launch() {
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"steam://rungameid/2661300", nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

} // namespace Steam
