// SPDX-License-Identifier: GPL-3.0-or-later
// Panel pages. They only read Bridge snapshots and submit Bridge requests.
#pragma once
#include "game/Bridge.hpp"
#include "game/Settings.hpp"

class DxWindow;

namespace Pages {

enum class Page : int {
    Home, Items, Player, Damage, Mutations, Build, Pickup, World, Teleport, Creatures, Progress, Options, Log, Count
};

struct Context {
    const Bridge::Snapshot& snapshot;
    Settings::Data& settings;
    bool& settingsChanged;
    bool& scaleChanged;      // UI scale changed -> fonts must be rebuilt
    DxWindow& window;
    Page& page;
};

const char* IconOf(Page page);
const char* LabelOf(Page page);
bool IsGameSection(Page page);
void Draw(Context& context);

// Helpers shared with the window (header/status).
void Send(Bridge::Request request);
std::string Utf8(const std::wstring& text);

} // namespace Pages
