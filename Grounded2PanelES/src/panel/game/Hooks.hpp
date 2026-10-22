// SPDX-License-Identifier: GPL-3.0-or-later
// Game-thread entry point (ProcessEvent of the local player controller) and the native
// placement hooks used by "Construir sin restricciones".
#pragma once
#include <atomic>
#include <string>

namespace SDK { class UObject; }

namespace Hooks {

// Hooks ProcessEvent in the controller's vtable (once per vtable). Session::Tick() then runs on
// the game thread every time the controller processes an event.
bool InstallTick(SDK::UObject* controller);
bool TickInstalled();

// Swaps the native implementation of the building placement checks.
void InstallBuildAnywhere();
bool BuildAnywhereReady();
std::string BuildAnywhereProblem();
extern std::atomic<bool> buildAnywhere;

} // namespace Hooks
