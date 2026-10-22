// SPDX-License-Identifier: GPL-3.0-or-later
// Objects resolved fresh on every game-thread tick. Pointers are never kept between ticks
// except where noted, so world changes and respawns cannot leave dangling references.
#pragma once
#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/Engine_classes.hpp"

#include "Bridge.hpp"

#include <array>
#include <string>
#include <vector>

namespace Game {

struct Ctx {
    SDK::UWorld* world = nullptr;
    SDK::APlayerController* controller = nullptr;
    SDK::APlayerState* playerState = nullptr;     // ASurvivalPlayerState
    SDK::ACharacter* character = nullptr;          // ASurvivalPlayerCharacter (alive pawn)
    SDK::AGameStateBase* gameState = nullptr;
    SDK::UObject* modes = nullptr;                 // USurvivalModeManagerComponent
    SDK::UObject* settings = nullptr;              // USurvivalGameModeSettings
    SDK::UObject* perks = nullptr;                 // UPerkComponent
    SDK::UObject* cheats = nullptr;                // USurvivalCheatManager owned by the local controller
    bool host = false;

    // Components of the local character (may be null).
    SDK::UObject* health = nullptr;
    SDK::UObject* stamina = nullptr;
    SDK::UObject* survival = nullptr;
    SDK::UObject* inventory = nullptr;
};

// Resolved per session (from the class names of this game build).
struct Classes {
    SDK::UClass* survivalPlayerCharacter = nullptr;
    SDK::UClass* survivalPlayerState = nullptr;
    SDK::UClass* survivalPlayerController = nullptr;
    SDK::UClass* survivalCheatManager = nullptr;
    SDK::UClass* spawnedItem = nullptr;
    SDK::UClass* dataTable = nullptr;
};

Classes& GetClasses();

// Shared helpers (Session.cpp)
SDK::APlayerState* FindPlayer(SDK::UWorld* world, int id);
SDK::ACharacter* PawnOf(SDK::APlayerState* player);
std::wstring PlayerName(SDK::APlayerState* player);
SDK::UObject* EnsureCheatManager(Ctx& ctx);

// Features.cpp
void EvaluateFeatures(Ctx& ctx, std::array<Bridge::FeatureState, Bridge::kFeatureCount>& out, std::vector<std::wstring>& notes);

} // namespace Game
