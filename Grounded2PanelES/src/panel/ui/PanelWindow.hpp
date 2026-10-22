// SPDX-License-Identifier: GPL-3.0-or-later
// The panel window: runs on its own thread inside the game process (or inside the preview).
#pragma once
#include <windows.h>

namespace PanelWindow {

constexpr const wchar_t* kClassName = L"Grounded2PanelES";

bool Start(HINSTANCE module);   // starts the UI thread
void Stop();                    // closes the window and waits for the thread
int Run(HINSTANCE module, bool preview, int startPage = -1);   // runs on the calling thread until closed

} // namespace PanelWindow
