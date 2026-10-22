// SPDX-License-Identifier: GPL-3.0-or-later
// Shared folder helpers for the panel DLL and the launcher.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>

namespace Paths {

// %LOCALAPPDATA%\Grounded2PanelES — logs, settings and waypoints live here so the
// mod folder deployed by Vortex stays untouched.
inline std::wstring DataDir() {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = base;
        dir += L"\\Grounded2PanelES";
    }
    if (base) CoTaskMemFree(base);
    if (dir.empty()) dir = L".\\Grounded2PanelES";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Folder of the module that contains `address` (DLL or EXE), with trailing backslash.
inline std::wstring ModuleDir(const void* address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module);
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : path.substr(0, slash + 1);
}

// %USERPROFILE%\Saved Games\Grounded2 (Steam saves).
inline std::wstring SavesDir() {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &base)) && base) {
        dir = base;
        dir += L"\\Grounded2";
    }
    if (base) CoTaskMemFree(base);
    return dir;
}

inline std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

inline std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

} // namespace Paths
