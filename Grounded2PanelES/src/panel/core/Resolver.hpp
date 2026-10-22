// SPDX-License-Identifier: GPL-3.0-or-later
// Finds the engine globals at runtime instead of trusting offsets from a dump of an older
// game build. This is what lets the panel keep working after Grounded 2 updates:
//  * GObjects: scanned in the game's writable sections and validated object by object.
//  * Names:    FNamePool located by its first entries ("None", "ByteProperty") and decoded
//              by the panel itself, replacing the SDK's FName::AppendString offset.
//  * ProcessEvent vtable index: verified by looking for its UFunction flag checks.
//  * GWorld:   not needed; the world is read from the engine's game viewport.
#pragma once
#include <cstdint>
#include <string>

namespace Resolver {

struct Report {
    bool objects = false;
    bool names = false;
    bool processEvent = false;
    bool processEventVerified = false;
    uintptr_t objectsRva = 0;
    uintptr_t namesRva = 0;
    int processEventIndex = -1;
    int objectCount = 0;
    std::string gameVersion;   // e.g. "0.6.0.1", empty if not found
    std::string engineVersion; // e.g. "5.6.1", empty if not found
    std::string problem;       // why the last attempt failed (diagnostics, Spanish)
};

// Runs every step that is still pending. Safe to call repeatedly from a worker thread;
// returns true once objects, names and ProcessEvent are usable.
bool Run(Report& report);

// FName -> UTF-8 using the panel's own decoder (valid after names are resolved).
std::string NameToString(int32_t comparisonIndex, uint32_t number);

} // namespace Resolver
