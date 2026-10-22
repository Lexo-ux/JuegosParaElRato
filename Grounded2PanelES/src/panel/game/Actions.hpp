// SPDX-License-Identifier: GPL-3.0-or-later
// Implementation of every panel action and the periodic effects (god mode, flight, pickup…).
// Runs only on the game thread.
#pragma once
#include "Context.hpp"

namespace Actions {

struct Outcome {
    std::wstring message;
    Bridge::Tone tone = Bridge::Tone::Info;
    bool mutationsChanged = false;
};

void BeginSession(Game::Ctx& ctx);    // capture the values to restore later
void EndSession();                    // the world went away: forget per-session state
void Execute(Game::Ctx& ctx, const Bridge::Request& request, const Bridge::Snapshot& snapshot, Outcome& outcome);
uint64_t Maintain(Game::Ctx& ctx, uint64_t nowMs);   // returns the delay until the next call (ms)
void Fill(Game::Ctx& ctx, Bridge::Snapshot& snapshot);

} // namespace Actions
