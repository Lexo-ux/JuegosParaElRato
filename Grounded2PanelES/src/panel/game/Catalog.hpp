// SPDX-License-Identifier: GPL-3.0-or-later
// Item catalogue (Table_AllItems), Spanish names and mutation (perk) data, read through
// reflection so a changed row layout disables the feature instead of reading garbage.
#pragma once
#include "Context.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Catalog {

struct Row {
    SDK::UDataTable* table = nullptr;
    SDK::FName name;
    uint8_t* data = nullptr;
};

void Reset();   // new session: tables may have been reloaded

// Loads items with localized names. Returns false (and a Spanish message) on failure.
bool LoadItems(Game::Ctx& ctx, std::shared_ptr<const std::vector<Bridge::Item>>& out, std::wstring& message);
bool FindItem(const std::string& id, Row& row);

// Whether a row of BaseItemData may go to the inventory (cached).
bool CanAddToInventory(SDK::UDataTable* table, SDK::FName rowName);

// Mutations of the local player.
std::vector<Bridge::Mutation> Mutations(Game::Ctx& ctx);
bool FindMutation(const std::string& id, Row& row);
bool MutationIsUsable(const Row& row);   // not passive, not for buggies, has tiers
int MutationTiers(const Row& row);

struct PerkStatus {
    int tier = 0;
    bool equipped = false;
    bool passive = false;
    uint8_t name[16] = {};   // FName_NetCrc32 as returned by the game (size checked)
    int nameSize = 0;
};
bool GetPerkStatus(SDK::UObject* perks, const Row& row, PerkStatus& out);
std::vector<PerkStatus> GetPerks(SDK::UObject* perks);
bool PerkStatusName(const PerkStatus& status, std::string& id);

} // namespace Catalog
