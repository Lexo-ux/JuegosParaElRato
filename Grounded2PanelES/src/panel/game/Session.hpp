// SPDX-License-Identifier: GPL-3.0-or-later
// Game-thread state machine and the Bridge implementation used by the window.
#pragma once
#include <cstdint>
#include <string>

namespace Resolver { struct Report; }

namespace Session {

// Called on the game thread from the controller's ProcessEvent hook.
void Tick();

// Called from the start-up thread.
void SetEngineReport(const Resolver::Report& report);
void SetStartupMessage(const std::wstring& message, bool resolved, bool blocked = false);
uint64_t LastTickMs();
void MarkIdle();   // no tick for a while (main menu, loading): publish "waiting"

// A C++ exception escaped panel code (caught at the hook or thread boundary): log it and
// stop touching the game for the rest of the session. Never lets it reach the game.
void Fault(const char* where, const char* what) noexcept;

} // namespace Session
