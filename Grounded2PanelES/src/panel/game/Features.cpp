// SPDX-License-Identifier: GPL-3.0-or-later
// Checks every feature against the running game build: each one lists the classes,
// properties and functions (with parameter names and types) it needs. A feature whose
// requirements changed in an update is disabled with the reason instead of being called.
#include "Context.hpp"
#include "Hooks.hpp"

#include "core/Log.hpp"
#include "core/Reflect.hpp"
#include "common/Paths.hpp"

#include <initializer_list>
#include <set>
#include <utility>

namespace Game {
namespace {

using Bridge::Feature;
using Params = std::initializer_list<std::pair<const char*, const char*>>;

bool TypeMatches(const std::string& actual, const char* wanted) {
    if (!wanted || !*wanted) return true;
    const std::string w = wanted;
    if (w == "Float") return actual == "FloatProperty" || actual == "DoubleProperty";
    if (w == "Byte") return actual == "ByteProperty" || actual == "EnumProperty";
    if (w == "Object") return actual == "ObjectProperty" || actual == "WeakObjectProperty";
    return actual == w + "Property";
}

class Checker {
public:
    explicit Checker(std::set<std::string>& failures) : failures_(failures) {}

    std::string Fn(SDK::UStruct* type, const char* label, const char* function, Params params = {}) {
        if (!type) return Fail(std::string("no se encontró la clase ") + label);
        auto* fn = Reflect::FindFunction(type, function);
        if (!fn) return Fail(std::string(label) + "." + function + "() ya no existe");
        for (const auto& [name, kind] : params) {
            SDK::FField* found = nullptr;
            for (SDK::FField* field = fn->ChildProperties; field; field = field->Next)
                if (field->Name.ToString() == name) { found = field; break; }
            if (!found) return Fail(std::string(label) + "." + function + "(): falta el parámetro " + name);
            if (!TypeMatches(Reflect::FieldType(found), kind))
                return Fail(std::string(label) + "." + function + "(): el parámetro " + name + " cambió de tipo");
        }
        return {};
    }

    std::string Prop(SDK::UStruct* type, const char* label, const char* property, const char* kind) {
        if (!type) return Fail(std::string("no se encontró ") + label);
        auto* p = Reflect::FindProperty(type, property);
        if (!p) return Fail(std::string(label) + "." + property + " ya no existe");
        if (!TypeMatches(Reflect::FieldType(p), kind)) return Fail(std::string(label) + "." + property + " cambió de tipo");
        return {};
    }

private:
    std::string Fail(const std::string& reason) {
        failures_.insert(reason);
        return reason;
    }
    std::set<std::string>& failures_;
};

std::string First(std::initializer_list<std::string> reasons) {
    for (const auto& r : reasons)
        if (!r.empty()) return r;
    return {};
}

} // namespace

void EvaluateFeatures(Ctx& ctx, std::array<Bridge::FeatureState, Bridge::kFeatureCount>& out, std::vector<std::wstring>& notes) {
    std::set<std::string> failures;
    Checker c(failures);

    SDK::UStruct* cheats = Reflect::FindClass("SurvivalCheatManager");
    SDK::UStruct* character = ctx.character ? static_cast<SDK::UStruct*>(ctx.character->Class) : Reflect::FindClass("SurvivalPlayerCharacter");
    SDK::UStruct* playerState = ctx.playerState ? static_cast<SDK::UStruct*>(ctx.playerState->Class) : Reflect::FindClass("SurvivalPlayerState");
    SDK::UStruct* controller = ctx.controller ? static_cast<SDK::UStruct*>(ctx.controller->Class) : Reflect::FindClass("SurvivalPlayerController");
    SDK::UStruct* settings = ctx.settings ? static_cast<SDK::UStruct*>(ctx.settings->Class) : Reflect::FindClass("SurvivalGameModeSettings");
    SDK::UStruct* gameMode = ctx.world && G2F(ctx.world, UWorld, AuthorityGameMode) ? static_cast<SDK::UStruct*>(G2F(ctx.world, UWorld, AuthorityGameMode)->Class) : nullptr;
    SDK::UStruct* health = Reflect::FindClass("HealthComponent");
    SDK::UStruct* stamina = Reflect::FindClass("StaminaComponent");
    SDK::UStruct* survival = Reflect::FindClass("SurvivalComponent");
    SDK::UStruct* perks = Reflect::FindClass("PerkComponent");
    SDK::UStruct* spawned = Reflect::FindClass("SpawnedItem");
    SDK::UStruct* item = Reflect::FindClass("Item");
    SDK::UStruct* inventory = Reflect::FindClass("InventoryComponent");
    SDK::UStruct* movement = Reflect::FindClass("CharacterMovementComponent");
    SDK::UStruct* actor = Reflect::FindClass("Actor");
    SDK::UStruct* baseItem = Reflect::FindStruct("BaseItemData", Reflect::Kind::ScriptStruct);
    SDK::UStruct* perkStatus = Reflect::FindStruct("PerkStatus", Reflect::Kind::ScriptStruct);
    SDK::UStruct* perkData = Reflect::FindStruct("PerkData", Reflect::Kind::ScriptStruct);

    auto set = [&](Feature f, const std::string& why) {
        auto& state = out[static_cast<size_t>(f)];
        state.ok = why.empty();
        state.why = state.ok ? L"" : Paths::Wide(why);
    };
    auto cheat = [&](const char* fn, Params params = {}) { return c.Fn(cheats, "SurvivalCheatManager", fn, params); };

    // Items
    const std::string items = First({
        gameMode ? c.Fn(gameMode, "BP_SurvivalGameMode", "GrantItemsToPlayer", {{"SurvivalPlayer", "Object"}, {"ItemData", "Struct"}, {"Count", "Int"}})
                 : std::string("sin modo de juego de anfitrión"),
        c.Prop(baseItem, "BaseItemData", "bCanBeAddedToInventory", "Bool"),
        c.Prop(baseItem, "BaseItemData", "LocalizedDisplayName", "Struct"),
    });
    set(Feature::Items, items);
    set(Feature::ItemsForOthers, items);
    set(Feature::SpanishNames, First({c.Prop(Reflect::FindClass("OEITextSettings"), "OEITextSettings", "StringTableBundleSetMap", "Map")}));

    // Build / craft
    set(Feature::FreeCraft, c.Prop(settings, "SurvivalGameModeSettings", "bRecipesRequireIngredients", "Bool"));
    set(Feature::FreeBuild, c.Prop(settings, "SurvivalGameModeSettings", "bAutoCompleteBuildings", "Bool"));
    set(Feature::BuildAnywhere, Hooks::BuildAnywhereReady() ? std::string() : Hooks::BuildAnywhereProblem());
    set(Feature::FinishBlueprints, cheat("BuildAllBuildings"));
    set(Feature::BuildingRules, First({c.Prop(settings, "SurvivalGameModeSettings", "bEnableBuildingIntegrity", "Bool"),
                                       c.Prop(settings, "SurvivalGameModeSettings", "BuildingHealthMultiplier", "Float")}));

    // Damage
    set(Feature::DamageWorld, c.Prop(settings, "SurvivalGameModeSettings", "PlayerDamageMultiplier", "Float"));
    set(Feature::DamagePersonal, c.Prop(character, "SurvivalCharacter", "CustomDamageMultiplier", "Float"));
    set(Feature::DamageIncoming, c.Prop(settings, "SurvivalGameModeSettings", "EnemyDamageMultiplier", "Float"));

    // Mutations
    set(Feature::Mutations, First({
        c.Prop(playerState, "SurvivalPlayerState", "PerkComponent", "Object"),
        c.Prop(perks, "PerkComponent", "MaxEquippedPerks", "Int"),
        c.Fn(perks, "PerkComponent", "GetPerkStatus", {{"PerkRowHandle", "Struct"}, {"OutPerkStatus", "Struct"}}),
        c.Fn(perks, "PerkComponent", "UnlockPerkTier", {{"PerkRowHandle", "Struct"}, {"Tier", "Int"}}),
        c.Fn(perks, "PerkComponent", "ServerEquipPerk", {{"PerkName", "Struct"}}),
        c.Fn(perks, "PerkComponent", "ServerUnequipPerk", {{"PerkName", "Struct"}}),
        c.Fn(perks, "PerkComponent", "GetMaxEquippedPerks", {{"ReturnValue", "Int"}}),
        c.Fn(perks, "PerkComponent", "GetNumEquippedPerks", {{"ReturnValue", "Int"}}),
        c.Fn(perks, "PerkComponent", "GetPerks", {{"ReturnValue", "Array"}}),
        c.Prop(perkStatus, "PerkStatus", "PerkName", "Struct"),
        c.Prop(perkStatus, "PerkStatus", "Tier", "Int"),
        c.Prop(perkStatus, "PerkStatus", "bEquipped", "Bool"),
        c.Prop(perkStatus, "PerkStatus", "bIsPassive", "Bool"),
        c.Prop(perkData, "PerkData", "bIsPassive", "Bool"),
        c.Prop(perkData, "PerkData", "PerkTarget", "Byte"),
        c.Prop(perkData, "PerkData", "Tiers", "Array"),
        c.Prop(perkData, "PerkData", "LocalizedDisplayName", "Struct"),
    }));

    // Loose items
    const std::string loose = First({
        c.Prop(spawned, "SpawnedItem", "Item", "Object"),
        c.Prop(spawned, "SpawnedItem", "EquippedOwner", "Object"),
        c.Prop(spawned, "SpawnedItem", "IntendedAttachParentActor", "Object"),
        c.Prop(spawned, "SpawnedItem", "IntendedAttachParentSceneComponent", "Object"),
        c.Prop(spawned, "SpawnedItem", "AttachedZipline", "Object"),
        c.Prop(spawned, "SpawnedItem", "bIsAttached", "Bool"),
        c.Prop(spawned, "SpawnedItem", "bProcessingItem", "Bool"),
        c.Prop(spawned, "SpawnedItem", "bIsThrown", "Bool"),
        c.Fn(spawned, "SpawnedItem", "IsHaulingItem", {{"ReturnValue", "Bool"}}),
        c.Fn(spawned, "SpawnedItem", "IsThrownAttackTimerActive", {{"ReturnValue", "Bool"}}),
        c.Fn(item, "Item", "IsEquipped", {{"ReturnValue", "Bool"}}),
        c.Fn(item, "Item", "IsPartyKeyItem", {{"ReturnValue", "Bool"}}),
        c.Fn(item, "Item", "IsPersonalKeyItem", {{"ReturnValue", "Bool"}}),
        c.Prop(item, "Item", "ItemDataRowHandle", "Struct"),
        c.Prop(baseItem, "BaseItemData", "bCanBeAddedToInventory", "Bool"),
    });
    set(Feature::ClearNearby, loose);
    set(Feature::AutoPickup, First({
        loose,
        c.Fn(controller, "SurvivalPlayerController", "TryPickupItem", {{"Item", "Object"}, {"bEquip", "Bool"}, {"ReturnValue", "Bool"}}),
        c.Fn(inventory, "InventoryComponent", "HasAnySpaceForItem", {{"Item", "Object"}, {"ReturnValue", "Bool"}}),
        c.Prop(character, "SurvivalCharacter", "InventoryComponent", "Object"),
        c.Prop(character, "SurvivalCharacter", "HealthComponent", "Object"),
        c.Fn(health, "HealthComponent", "GetCurrentHealth", {{"ReturnValue", "Float"}}),
    }));

    // Points and utilities
    set(Feature::WhiteMolars, cheat("AddPersonalUpgradePoints", {{"Amount", "Int"}}));
    set(Feature::GoldMolars, cheat("AddPartyUpgradePoints", {{"Amount", "Int"}}));
    set(Feature::Science, cheat("AddScience", {{"Amount", "Int"}}));
    set(Feature::BuggyPoints, cheat("AddBuggyUpgradePoints", {{"Amount", "Int"}}));
    const std::string healthFns = First({
        c.Prop(character, "SurvivalCharacter", "HealthComponent", "Object"),
        c.Fn(health, "HealthComponent", "SetCurrentHealth", {{"DesiredHealth", "Float"}}),
        c.Fn(health, "HealthComponent", "GetMaxHealth", {{"ReturnValue", "Float"}}),
        c.Fn(health, "HealthComponent", "GetCurrentHealth", {{"ReturnValue", "Float"}}),
    });
    set(Feature::Heal, healthFns);
    set(Feature::Revive, cheat("Revive"));
    const std::string staminaFns = First({c.Prop(character, "SurvivalCharacter", "StaminaComponent", "Object"),
                                          c.Fn(stamina, "StaminaComponent", "Restore")});
    set(Feature::RestoreStamina, staminaFns);
    set(Feature::InfiniteStamina, staminaFns);
    set(Feature::FullRestore, cheat("FullRestore"));
    set(Feature::GodMode, First({healthFns, c.Prop(health, "HealthComponent", "bCanDieEver", "Bool"),
                                 c.Prop(health, "HealthComponent", "bCanDieFromDamage", "Bool")}));
    set(Feature::NoHunger, First({
        c.Prop(character, "SurvivalCharacter", "SurvivalComponent", "Object"),
        c.Fn(survival, "SurvivalComponent", "SetCurrentFood", {{"NewFoodLevel", "Float"}}),
        c.Fn(survival, "SurvivalComponent", "SetCurrentWater", {{"NewWaterLevel", "Float"}}),
        c.Fn(survival, "SurvivalComponent", "GetMaxHunger", {{"ReturnValue", "Float"}}),
        c.Fn(survival, "SurvivalComponent", "GetMaxThirst", {{"ReturnValue", "Float"}}),
    }));

    // Movement (engine) — speed prefers the game's own cheat but falls back to the engine value.
    const std::string engineMovement = First({c.Prop(movement, "CharacterMovementComponent", "MaxWalkSpeed", "Float"),
                                              c.Prop(movement, "CharacterMovementComponent", "JumpZVelocity", "Float")});
    set(Feature::Speed, engineMovement);
    set(Feature::Jump, engineMovement);
    set(Feature::Fly, First({engineMovement, c.Fn(movement, "CharacterMovementComponent", "SetMovementMode", {{"NewMovementMode", "Byte"}}),
                             c.Fn(actor, "Actor", "SetActorEnableCollision", {{"bNewActorEnableCollision", "Bool"}})}));
    set(Feature::Teleport, c.Fn(actor, "Actor", "K2_TeleportTo", {{"DestLocation", "Struct"}, {"DestRotation", "Struct"}}));
    set(Feature::TeleportParty, cheat("TeleportPartyToMe"));

    // World
    set(Feature::AdvanceTime, cheat("AdvanceTimeByHours", {{"Hours", "Float"}}));
    set(Feature::SetTime, cheat("SetTime", {{"Hour", "Int"}, {"Lock", "Bool"}}));
    set(Feature::TimeSpeed, cheat("TimeSpeed", {{"Multiplier", "Float"}}));
    set(Feature::WorldRules, ctx.settings ? std::string() : std::string("no se encontraron los ajustes de la partida"));
    set(Feature::RevealMap, cheat("RevealMapFogOfWar"));
    set(Feature::UnlockLandmarks, cheat("UnlockAllPOIs"));
    set(Feature::UnlockRecipes, cheat("UnlockAllRecipes", {{"Mode", "Byte"}}));
    set(Feature::UnlockTechTrees, cheat("UnlockAllTechTrees"));
    set(Feature::UnlockScabs, cheat("UnlockAllColorThemes"));
    set(Feature::OmniTool, cheat("SetOmniToolTier", {{"Tier", "Byte"}}));
    set(Feature::RepairItems, cheat("RepairAllItems"));
    set(Feature::RecoverBackpacks, cheat("RecoverBackpacks"));
    set(Feature::RespawnResources, cheat("ForceRespawnFoliage"));
    set(Feature::EndRaid, cheat("ForceEndWave"));
    set(Feature::Arachnophobia, cheat("SetArachnophobiaSafeMode", {{"Level", "Int"}}));
    set(Feature::ToggleHud, cheat("ToggleHUD"));
    set(Feature::TameNearest, cheat("TameNearest"));
    set(Feature::ReviveMount, cheat("ReviveMount"));
    set(Feature::SpawnMount, cheat("SpawnAndMountRedSoldierAnt", {{"Tier", "Int"}}));

    notes.clear();
    for (const auto& f : failures) notes.push_back(Paths::Wide(f));
    int ok = 0;
    for (const auto& state : out) ok += state.ok ? 1 : 0;
    Log::Info("Compatibilidad", std::to_string(ok) + " de " + std::to_string(Bridge::kFeatureCount) + " funciones verificadas");
    for (const auto& f : failures) Log::Warn("Compatibilidad", f);
}

} // namespace Game
