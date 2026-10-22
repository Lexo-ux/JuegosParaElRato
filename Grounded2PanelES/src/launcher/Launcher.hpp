// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers of IniciarPanel.exe: find the game, load the panel, back up saves.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Steam {
constexpr const wchar_t* kAppId = L"2661300";
struct Install {
    std::wstring steamPath;   // C:\Program Files (x86)\Steam
    std::wstring gameDir;     // ...\steamapps\common\Grounded2
    std::wstring exe;         // full path of the shipping executable, if found
    std::wstring version;     // file version of the executable
};
Install Detect(const std::wstring& launcherDir);
bool Launch();   // steam://rungameid/2661300
} // namespace Steam

namespace Inject {
struct Game {
    DWORD pid = 0;
    std::wstring exeName;
    bool panelLoaded = false;
    bool otherModLoaded = false;   // Grounded2Minimal already injected
    bool windowReady = false;      // the Unreal window exists
    bool elevated = false;         // the game runs with more privileges than the launcher
    std::wstring windowTitle;
    std::vector<std::wstring> candidates;   // every process with "grounded" in its name (diagnostics)
};
Game Find();
// Loads `dll` into the game. Returns an empty string on success or a Spanish error message.
std::wstring Load(DWORD pid, const std::wstring& dll);
bool ShowPanel();   // brings an already loaded panel window to the front
} // namespace Inject

namespace Backup {
// Copies %USERPROFILE%\Saved Games\Grounded2 into a dated folder. Returns the destination or
// an empty string and a message.
std::wstring Run(std::wstring& message);
std::wstring Folder();
int Count();
} // namespace Backup
