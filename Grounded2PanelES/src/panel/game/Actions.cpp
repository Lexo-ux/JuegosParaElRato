// SPDX-License-Identifier: GPL-3.0-or-later
// Panel actions. Damage scopes, auto pickup, cleanup, utilities and mutations come from
// Panel 0.2.0; game members and functions are now reached by name through reflection.
#include "Actions.hpp"

#include "Catalog.hpp"
#include "Hooks.hpp"
#include "core/Log.hpp"
#include "core/Reflect.hpp"
#include "common/Paths.hpp"

#include "SDK/Engine_classes.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

using Bridge::Action;
using Bridge::Feature;
using Bridge::Tone;

namespace Actions {
namespace {

// ------------------------------------------------------------------------------- helpers

float CallFloat(SDK::UObject* object, const char* function, float fallback = 0) {
    if (!object) return fallback;
    Reflect::Call call(object, function);
    return call.Run() ? call.Get<float>("ReturnValue", fallback) : fallback;
}

int CallInt(SDK::UObject* object, const char* function, int fallback = 0) {
    if (!object) return fallback;
    Reflect::Call call(object, function);
    return call.Run() ? call.Get<int32_t>("ReturnValue", fallback) : fallback;
}

bool CallBool(SDK::UObject* object, const char* function) {
    if (!object) return false;
    Reflect::Call call(object, function);
    return call.Run() && call.GetBool();
}

bool CallVoid(SDK::UObject* object, const char* function) {
    if (!object) return false;
    Reflect::Call call(object, function);
    return call.Run();
}

// Runs a SurvivalCheatManager command with optional arguments.
template <class Fill>
bool Cheat(Game::Ctx& ctx, const char* function, Fill fill) {
    if (!ctx.cheats) return false;
    Reflect::Call call(ctx.cheats, function);
    fill(call);
    return call.Run();
}
bool Cheat(Game::Ctx& ctx, const char* function) {
    return Cheat(ctx, function, [](Reflect::Call&) {});
}

bool Has(const Bridge::Snapshot& s, Feature f) { return s.Has(f); }

std::wstring Unavailable(const Bridge::Snapshot& s, Feature f) {
    const auto& why = s.features[static_cast<size_t>(f)].why;
    return L"No disponible en esta versión del juego" + (why.empty() ? std::wstring(L".") : L": " + why);
}

SDK::UCharacterMovementComponent* Movement(SDK::ACharacter* character) {
    return character ? character->CharacterMovement : nullptr;
}

bool ForegroundIsGame() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid != GetCurrentProcessId()) return false;
    wchar_t className[64] = {};
    GetClassNameW(foreground, className, 64);
    return std::wstring(className) != L"Grounded2PanelES";   // not the panel window itself
}

// ------------------------------------------------------------------------------- session state

struct PersonalDamage {
    uintptr_t playerKey = 0, pawnKey = 0;
    int pawnIndex = -1;
    float original = 1, multiplier = 1;
};
std::unordered_map<int, PersonalDamage> personalDamage;

struct RuleState {
    std::string name;
    std::wstring label;
    bool isBool = true;
    Reflect::BoolField boolField;
    int32_t offset = -1;
    bool baseBool = false;
    float baseFloat = 1;
};
std::vector<RuleState> rules;

struct Baseline {
    int rawSlots = 0;
    bool slotsCaptured = false;
    std::vector<std::string> equipped;
    bool anywhere = false;
} baseline;

bool outgoingEnabled = false, incomingEnabled = false, slotsChanged = false;

// Player effects
bool god = false, infiniteStamina = false, noHunger = false, flying = false, noclip = false;
float speed = 1, jump = 1;
bool speedViaCheat = false;
float walkBaseline = -1, jumpBaseline = -1, flySpeedBaseline = -1, flyBrakeBaseline = -1;
struct GodFlags { bool captured = false; bool ever = true, damage = true, fragility = true; } godFlags;

// Pickup
bool autoPickup = false;
float pickupRadius = 300;
uint64_t collected = 0;

// Teleport
bool hasBack = false;
SDK::FVector back{};

// World
float timeSpeed = 1;
int lockedHour = -1;

uint64_t nextGod = 0, nextStamina = 0, nextHunger = 0, nextReapply = 0, nextPickup = 0;

// ------------------------------------------------------------------------------- rules

const std::unordered_map<std::string, const wchar_t*>& RuleLabels() {
    static const std::unordered_map<std::string, const wchar_t*> labels = {
        {"bUnlockAllRecipes", L"Todas las recetas desbloqueadas"},
        {"bAutoCompleteBuildings", L"Construir sin recursos"},
        {"bEnableBuildingIntegrity", L"Integridad estructural"},
        {"bRecipesRequireIngredients", L"Las recetas piden ingredientes"},
        {"bEncountersEnabled", L"Encuentros de criaturas"},
        {"bCreaturesIgnorePlayers", L"Las criaturas ignoran a los jugadores"},
        {"bStaminaEnabled", L"Gasto de resistencia"},
        {"bDamageEnabled", L"Daño activado"},
        {"bFriendlyFire", L"Fuego amigo"},
        {"bSizzleEnabled", L"Calor extremo"},
        {"bChillEnabled", L"Frío extremo"},
        {"bPoisonGasEnabled", L"Gas venenoso"},
        {"bCorrosionEnabled", L"Corrosión"},
        {"bSpoilingEnabled", L"La comida se estropea"},
        {"bMenusPauseGame", L"Los menús pausan el juego"},
        {"bQuestsEnabled", L"Misiones"},
        {"bPetInvincible", L"Mascotas invencibles"},
        {"bBuggyPermadeath", L"Muerte permanente de monturas"},
        {"bPerksUnlocked", L"Mutaciones desbloqueadas"},
        {"bUpgradesUnlocked", L"Mejoras desbloqueadas"},
        {"bBuildModeAvailable", L"Modo construcción disponible"},
        {"bBuildingsEnabled", L"Se puede construir"},
        {"bCraftingEnabled", L"Se puede fabricar"},
        {"bPhotoModeEnabled", L"Modo foto"},
        {"PlayerDamageMultiplier", L"Daño de los jugadores"},
        {"EnemyDamageMultiplier", L"Daño de los enemigos"},
        {"BuildingHealthMultiplier", L"Salud de las construcciones"},
        {"HeatMultiplier", L"Frecuencia de asedios"},
        {"ItemSpoilageMultiplier", L"Velocidad de deterioro de la comida"},
        {"ItemDurabilityMultiplier", L"Desgaste del equipo"},
        {"ItemDurabilityPenaltyOnDeathPercentage", L"Desgaste del equipo al morir"},
        {"SizzleMultiplier", L"Intensidad del calor"},
        {"ChillMultiplier", L"Intensidad del frío"},
        {"PoisonGasMultiplier", L"Intensidad del gas"},
        {"CorrosionMultiplier", L"Intensidad de la corrosión"},
        {"TimeOfDaySpeedMultiplier", L"Velocidad del ciclo de día"},
        {"HungerMultiplier", L"Velocidad del hambre"},
        {"ThirstMultiplier", L"Velocidad de la sed"},
        {"ResourceRespawnMultiplier", L"Reaparición de recursos"},
        {"CreatureHealthMultiplier", L"Salud de las criaturas"},
        {"FallDamageMultiplier", L"Daño por caída"},
    };
    return labels;
}

void CaptureRules(SDK::UObject* settings) {
    rules.clear();
    if (!settings || !settings->Class) return;
    for (const SDK::UStruct* type = settings->Class; type; type = type->SuperStruct) {
        for (SDK::FField* field = type->ChildProperties; field; field = field->Next) {
            const std::string kind = Reflect::FieldType(field);
            if (kind != "BoolProperty" && kind != "FloatProperty") continue;
            auto* property = static_cast<SDK::FProperty*>(field);
            RuleState rule;
            rule.name = field->Name.ToString();
            auto label = RuleLabels().find(rule.name);
            rule.label = label != RuleLabels().end() ? label->second : Paths::Wide(rule.name);
            rule.isBool = kind == "BoolProperty";
            auto* bytes = reinterpret_cast<uint8_t*>(settings);
            if (rule.isBool) {
                rule.boolField = Reflect::FindBool(settings->Class, rule.name);
                if (!rule.boolField) continue;
                rule.baseBool = Reflect::GetBool(settings, rule.boolField);
            } else {
                if (property->ElementSize != 4) continue;
                rule.offset = property->Offset;
                std::memcpy(&rule.baseFloat, bytes + rule.offset, 4);
                if (!std::isfinite(rule.baseFloat)) continue;
            }
            rules.push_back(std::move(rule));
        }
    }
    Log::Info("Mundo", std::to_string(rules.size()) + " reglas del mundo detectadas");
}

RuleState* FindRule(const std::string& name) {
    for (auto& rule : rules)
        if (rule.name == name) return &rule;
    return nullptr;
}

bool RuleBool(SDK::UObject* settings, const char* name, bool fallback) {
    auto* rule = FindRule(name);
    return settings && rule && rule->isBool ? Reflect::GetBool(settings, rule->boolField) : fallback;
}

float RuleFloat(SDK::UObject* settings, const char* name, float fallback) {
    auto* rule = FindRule(name);
    if (!settings || !rule || rule->isBool) return fallback;
    float value;
    std::memcpy(&value, reinterpret_cast<uint8_t*>(settings) + rule->offset, 4);
    return value;
}

bool SetRuleBool(SDK::UObject* settings, const char* name, bool value) {
    auto* rule = FindRule(name);
    if (!settings || !rule || !rule->isBool) return false;
    Reflect::SetBool(settings, rule->boolField, value);
    return true;
}

bool SetRuleFloat(SDK::UObject* settings, const char* name, float value) {
    auto* rule = FindRule(name);
    if (!settings || !rule || rule->isBool || !std::isfinite(value)) return false;
    std::memcpy(reinterpret_cast<uint8_t*>(settings) + rule->offset, &value, 4);
    return true;
}

void RestoreRules(SDK::UObject* settings) {
    if (!settings) return;
    for (const auto& rule : rules) {
        if (rule.isBool) Reflect::SetBool(settings, rule.boolField, rule.baseBool);
        else std::memcpy(reinterpret_cast<uint8_t*>(settings) + rule.offset, &rule.baseFloat, 4);
    }
}

// ------------------------------------------------------------------------------- damage (0.2.0)

float* DamageField(SDK::ACharacter* pawn) {
    return Reflect::Value<float>(pawn, "CustomDamageMultiplier", "FloatProperty");
}

bool SamePawn(const PersonalDamage& d, SDK::ACharacter* pawn) {
    return pawn && d.pawnKey == reinterpret_cast<uintptr_t>(pawn) && d.pawnIndex == pawn->Index;
}

void CapturePawn(PersonalDamage& d, SDK::ACharacter* pawn) {
    d.pawnKey = reinterpret_cast<uintptr_t>(pawn);
    d.pawnIndex = pawn->Index;
    if (auto* field = DamageField(pawn)) d.original = *field;
}

// Re-applies personal multipliers after a respawn and forgets players who left.
void RefreshPersonalDamage(SDK::UWorld* world, std::vector<Bridge::Player>* out, int localId) {
    std::unordered_set<int> connected;
    if (!world || !world->GameState) return;
    for (auto* ps : world->GameState->PlayerArray) {
        if (!ps || ps->PlayerId < 0) continue;
        connected.insert(ps->PlayerId);
        auto* pawn = Game::PawnOf(ps);
        auto active = personalDamage.find(ps->PlayerId);
        if (active != personalDamage.end() && active->second.playerKey != reinterpret_cast<uintptr_t>(ps)) {
            personalDamage.erase(active);
            active = personalDamage.end();
        }
        if (pawn && active != personalDamage.end() && !SamePawn(active->second, pawn)) {
            CapturePawn(active->second, pawn);
            if (auto* field = DamageField(pawn); field && std::isfinite(active->second.original) && active->second.original >= 0)
                *field = active->second.multiplier;
            else {
                personalDamage.erase(active);
                active = personalDamage.end();
            }
        }
        if (out) {
            Bridge::Player p;
            p.id = ps->PlayerId;
            p.name = Game::PlayerName(ps);
            p.available = pawn != nullptr;
            p.local = ps->PlayerId == localId;
            p.damageEnabled = active != personalDamage.end();
            if (p.damageEnabled) p.damage = active->second.multiplier;
            out->push_back(std::move(p));
        }
    }
    std::erase_if(personalDamage, [&](const auto& p) { return !connected.contains(p.first); });
}

// ------------------------------------------------------------------------------- loose items (0.2.0)

struct SpawnedLayout {
    SDK::UClass* type = nullptr;
    int32_t item = -1, equippedOwner = -1, attachActor = -1, attachComponent = -1, zipline = -1;
    Reflect::BoolField attached, processing, thrown;
    int32_t rowHandle = -1;
    bool ok = false;
} spawned;

int32_t ObjectOffset(SDK::UStruct* type, const char* name) {
    auto* p = Reflect::FindProperty(type, name);
    return p && p->ElementSize == 8 ? p->Offset : -1;
}

void ResolveSpawnedLayout() {
    spawned = {};
    spawned.type = Game::GetClasses().spawnedItem;
    if (!spawned.type) return;
    spawned.item = ObjectOffset(spawned.type, "Item");
    spawned.equippedOwner = ObjectOffset(spawned.type, "EquippedOwner");
    spawned.attachActor = ObjectOffset(spawned.type, "IntendedAttachParentActor");
    spawned.attachComponent = ObjectOffset(spawned.type, "IntendedAttachParentSceneComponent");
    spawned.zipline = ObjectOffset(spawned.type, "AttachedZipline");
    spawned.attached = Reflect::FindBool(spawned.type, "bIsAttached");
    spawned.processing = Reflect::FindBool(spawned.type, "bProcessingItem");
    spawned.thrown = Reflect::FindBool(spawned.type, "bIsThrown");
    auto* itemType = Reflect::FindClass("Item");
    auto* handle = Reflect::FindProperty(itemType, "ItemDataRowHandle");
    spawned.rowHandle = handle && handle->ElementSize >= 16 ? handle->Offset : -1;
    spawned.ok = spawned.item >= 0 && spawned.equippedOwner >= 0 && spawned.attachActor >= 0 && spawned.attachComponent >= 0 &&
                 spawned.zipline >= 0 && spawned.attached && spawned.processing && spawned.thrown && spawned.rowHandle >= 0;
}

template <class T = SDK::UObject>
T* At(SDK::UObject* object, int32_t offset) {
    return *reinterpret_cast<T**>(reinterpret_cast<uint8_t*>(object) + offset);
}

std::vector<SDK::AActor*> NearbyItems(Game::Ctx& ctx, float radius, bool pickup) {
    std::vector<std::pair<double, SDK::AActor*>> nearby;
    std::unordered_set<SDK::AActor*> seen;
    if (!spawned.ok || !ctx.character || !ctx.controller || !Bridge::ValidRadius(radius)) return {};
    const SDK::FVector origin = ctx.character->K2_GetActorLocation();
    auto scan = [&](SDK::ULevel* level) {
        if (!level || level->OwningWorld != ctx.world) return;
        for (auto* actor : level->Actors) {
            if (!actor || actor->IsDefaultObject() || actor->bActorIsBeingDestroyed || !actor->IsA(spawned.type)) continue;
            if (!seen.insert(actor).second) continue;
            const SDK::FVector location = actor->K2_GetActorLocation();
            const double dx = location.X - origin.X, dy = location.Y - origin.Y, dz = location.Z - origin.Z;
            if (!Bridge::WithinRadius(dx, dy, dz, radius)) continue;
            auto* item = At(actor, spawned.item);
            if (!item || At(actor, spawned.equippedOwner) || At(actor, spawned.attachActor) || At(actor, spawned.attachComponent) ||
                At(actor, spawned.zipline) || actor->GetAttachParentActor())
                continue;
            Bridge::LooseItemState flags;
            const float lifespan = actor->GetLifeSpan();
            flags.destroying = actor->IsActorBeingDestroyed() || (lifespan > 0 && lifespan <= 0.05f);
            flags.attached = Reflect::GetBool(actor, spawned.attached);
            flags.processing = Reflect::GetBool(actor, spawned.processing);
            flags.inFlight = Reflect::GetBool(actor, spawned.thrown) || CallBool(actor, "IsThrownAttackTimerActive");
            if (flags.destroying || flags.attached || flags.processing || flags.inFlight) continue;
            flags.equipped = CallBool(item, "IsEquipped");
            flags.keyItem = CallBool(item, "IsPartyKeyItem") || CallBool(item, "IsPersonalKeyItem");
            const auto* handle = reinterpret_cast<const uint8_t*>(item) + spawned.rowHandle;
            SDK::UDataTable* table;
            SDK::FName row;
            std::memcpy(&table, handle, 8);
            std::memcpy(&row, handle + 8, 8);
            flags.inventoryItem = Catalog::CanAddToInventory(table, row);
            flags.haulingItem = CallBool(actor, "IsHaulingItem");
            if (!Bridge::EligibleLooseItem(flags, pickup)) continue;
            if (!ctx.controller->LineOfSightTo(actor, SDK::FVector{}, false)) continue;
            nearby.emplace_back(dx * dx + dy * dy + dz * dz, actor);
        }
    };
    scan(ctx.world->PersistentLevel);
    for (auto* level : ctx.world->Levels) scan(level);
    std::sort(nearby.begin(), nearby.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<SDK::AActor*> result;
    for (const auto& entry : nearby) result.push_back(entry.second);
    return result;
}

void Pickup(Game::Ctx& ctx) {
    if (!ctx.character || !ctx.inventory || !ctx.health || CallFloat(ctx.health, "GetCurrentHealth") <= 0) return;
    auto nearby = NearbyItems(ctx, pickupRadius, true);
    int attempts = 0;
    for (auto* actor : nearby) {
        if (++attempts > 32) break;  // the rest waits for the next half-second pass
        if (!actor || actor->bActorIsBeingDestroyed) continue;
        auto* item = At(actor, spawned.item);
        if (!item) continue;
        Reflect::Call space(ctx.inventory, "HasAnySpaceForItem");
        space.Object("Item", item);
        if (!space.Run() || !space.GetBool()) continue;
        Reflect::Call pick(ctx.controller, "TryPickupItem");
        pick.Object("Item", item).Bool("bEquip", false);
        if (pick.Run() && pick.GetBool()) ++collected;
        // Never destroy an item after a failed pickup: the game owns the transfer and remainder.
    }
}

// ------------------------------------------------------------------------------- items

bool GiveItem(Game::Ctx& ctx, int playerId, const std::string& id, int count, std::wstring& error) {
    Catalog::Row row;
    if (!Catalog::FindItem(id, row)) {
        error = L"El objeto ya no está disponible. Actualiza la lista.";
        return false;
    }
    auto* ps = playerId < 0 ? ctx.playerState : Game::FindPlayer(ctx.world, playerId);
    auto* pawn = Game::PawnOf(ps);
    if (!pawn) {
        error = L"Ese jugador no tiene un personaje disponible ahora mismo.";
        return false;
    }
    SDK::FDataTableRowHandle handle;
    handle.DataTable = row.table;
    handle.RowName = row.name;
    Reflect::Call call(ctx.world->AuthorityGameMode, "GrantItemsToPlayer");
    call.Object("SurvivalPlayer", pawn).Raw("ItemData", &handle, sizeof(handle)).Int("Count", count);
    if (!call.Run()) {
        error = L"No se pudo generar el objeto. Consulta el registro.";
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------- movement

void SetSpeed(Game::Ctx& ctx, float multiplier) {
    if (ctx.cheats && Reflect::HasFunction(ctx.cheats, "SetMaxGroundSpeedModifier")) {
        Cheat(ctx, "SetMaxGroundSpeedModifier", [&](Reflect::Call& c) { c.Float("MaxGroundSpeedModifier", multiplier); });
        speedViaCheat = true;
        return;
    }
    speedViaCheat = false;
    auto* movement = Movement(ctx.character);
    if (!movement) return;
    if (walkBaseline < 0) walkBaseline = movement->MaxWalkSpeed;
    movement->MaxWalkSpeed = walkBaseline * multiplier;
}

void ApplyJump(Game::Ctx& ctx) {
    auto* movement = Movement(ctx.character);
    if (!movement) return;
    if (jump == 1.0f) {
        if (jumpBaseline > 0) movement->JumpZVelocity = jumpBaseline;
        jumpBaseline = -1;
        return;
    }
    if (jumpBaseline < 0) jumpBaseline = movement->JumpZVelocity;
    movement->JumpZVelocity = jumpBaseline * jump;
}

void StartFlying(Game::Ctx& ctx) {
    auto* movement = Movement(ctx.character);
    if (!movement) return;
    if (flySpeedBaseline < 0) {
        flySpeedBaseline = movement->MaxFlySpeed;
        flyBrakeBaseline = movement->BrakingDecelerationFlying;
    }
    movement->MaxFlySpeed = std::max(flySpeedBaseline, movement->MaxWalkSpeed * 2.0f);
    movement->BrakingDecelerationFlying = std::max(flyBrakeBaseline, 2048.0f);
    movement->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);
}

void StopFlying(Game::Ctx& ctx) {
    auto* movement = Movement(ctx.character);
    if (!movement) return;
    if (flySpeedBaseline >= 0) {
        movement->MaxFlySpeed = flySpeedBaseline;
        movement->BrakingDecelerationFlying = flyBrakeBaseline;
    }
    flySpeedBaseline = flyBrakeBaseline = -1;
    if (movement->MovementMode == SDK::EMovementMode::MOVE_Flying) movement->SetMovementMode(SDK::EMovementMode::MOVE_Falling, 0);
}

void FlyControl(Game::Ctx& ctx) {
    auto* movement = Movement(ctx.character);
    if (!movement) return;
    if (movement->MovementMode != SDK::EMovementMode::MOVE_Flying) StartFlying(ctx);
    const bool keys = ForegroundIsGame();
    const bool up = keys && (GetAsyncKeyState(VK_SPACE) & 0x8000);
    const bool down = keys && ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000));
    const double vertical = movement->MaxFlySpeed * 0.8;
    movement->Velocity.Z = up ? vertical : (down ? -vertical : 0.0);
}

void SetGod(Game::Ctx& ctx, bool enable) {
    if (!ctx.health) return;
    auto ever = Reflect::ObjBool(ctx.health, "bCanDieEver");
    auto damage = Reflect::ObjBool(ctx.health, "bCanDieFromDamage");
    auto fragility = Reflect::ObjBool(ctx.health, "bCanDieFromFragility");
    if (enable) {
        if (!godFlags.captured) {
            godFlags.ever = ever ? Reflect::GetBool(ctx.health, ever) : true;
            godFlags.damage = damage ? Reflect::GetBool(ctx.health, damage) : true;
            godFlags.fragility = fragility ? Reflect::GetBool(ctx.health, fragility) : true;
            godFlags.captured = true;
        }
        if (ever) Reflect::SetBool(ctx.health, ever, false);
        if (damage) Reflect::SetBool(ctx.health, damage, false);
        if (fragility) Reflect::SetBool(ctx.health, fragility, false);
    } else if (godFlags.captured) {
        if (ever) Reflect::SetBool(ctx.health, ever, godFlags.ever);
        if (damage) Reflect::SetBool(ctx.health, damage, godFlags.damage);
        if (fragility) Reflect::SetBool(ctx.health, fragility, godFlags.fragility);
        godFlags.captured = false;
    }
}

void Heal(Game::Ctx& ctx) {
    if (!ctx.health) return;
    const float max = CallFloat(ctx.health, "GetMaxHealth");
    if (max <= 0) return;
    Reflect::Call call(ctx.health, "SetCurrentHealth");
    call.Float("DesiredHealth", max);
    call.Run();
}

void FillHunger(Game::Ctx& ctx) {
    if (!ctx.survival) return;
    const float food = CallFloat(ctx.survival, "GetMaxHunger"), water = CallFloat(ctx.survival, "GetMaxThirst");
    const float* currentFood = Reflect::Value<float>(ctx.survival, "CurrentFood", "FloatProperty");
    const float* currentWater = Reflect::Value<float>(ctx.survival, "CurrentWater", "FloatProperty");
    if (food > 0 && (!currentFood || *currentFood < food - 0.5f)) {
        Reflect::Call call(ctx.survival, "SetCurrentFood");
        call.Float("NewFoodLevel", food);
        call.Run();
    }
    if (water > 0 && (!currentWater || *currentWater < water - 0.5f)) {
        Reflect::Call call(ctx.survival, "SetCurrentWater");
        call.Float("NewWaterLevel", water);
        call.Run();
    }
}

bool Teleport(Game::Ctx& ctx, SDK::ACharacter* who, const SDK::FVector& to, bool remember) {
    if (!who) return false;
    if (remember && who == ctx.character) {
        back = who->K2_GetActorLocation();
        hasBack = true;
    }
    return who->K2_TeleportTo(to, who->K2_GetActorRotation());
}

// ------------------------------------------------------------------------------- mutations (0.2.0)

std::wstring MutationAction(Game::Ctx& ctx, const Bridge::Request& r) {
    Catalog::Row row;
    if (!Catalog::FindMutation(r.id, row) || !Catalog::MutationIsUsable(row)) return L"La mutación seleccionada no está disponible.";
    Catalog::PerkStatus status;
    if (!Catalog::GetPerkStatus(ctx.perks, row, status)) return L"No se pudo leer el estado de la mutación.";
    SDK::FDataTableRowHandle handle;
    handle.DataTable = row.table;
    handle.RowName = row.name;
    if (r.action == Action::UnlockMutation) {
        if (r.count > Catalog::MutationTiers(row) || r.count <= status.tier) return L"Elige un nivel superior válido para esta mutación.";
        Reflect::Call call(ctx.perks, "UnlockPerkTier");
        call.Raw("PerkRowHandle", &handle, sizeof(handle)).Int("Tier", r.count);
        call.Run();
        Catalog::GetPerkStatus(ctx.perks, row, status);
        return status.tier >= r.count ? L"Nivel desbloqueado. Este progreso se conserva en la partida." : L"El juego no aceptó desbloquear ese nivel.";
    }
    if (status.tier < 1) return L"Primero aprende o desbloquea esta mutación.";
    const bool equip = r.action == Action::EquipMutation;
    Reflect::Call call(ctx.perks, equip ? "ServerEquipPerk" : "ServerUnequipPerk");
    call.Raw("PerkName", status.name, static_cast<size_t>(status.nameSize));
    call.Run();
    Catalog::GetPerkStatus(ctx.perks, row, status);
    if (status.equipped != equip) return L"El juego no aceptó el cambio. Comprueba las ranuras disponibles.";
    return equip ? L"Mutación activada." : L"Mutación desactivada.";
}

void CaptureMutations(Game::Ctx& ctx) {
    baseline.equipped.clear();
    baseline.slotsCaptured = false;
    if (!ctx.perks) return;
    if (auto* slots = Reflect::Value<int32_t>(ctx.perks, "MaxEquippedPerks", "IntProperty")) {
        baseline.rawSlots = *slots;
        baseline.slotsCaptured = true;
    }
    for (const auto& perk : Catalog::GetPerks(ctx.perks)) {
        std::string id;
        if (perk.equipped && !perk.passive && Catalog::PerkStatusName(perk, id)) baseline.equipped.push_back(id);
    }
}

void RestoreMutations(Game::Ctx& ctx) {
    if (!ctx.perks || !baseline.slotsCaptured) return;
    auto contains = [](const std::string& id) {
        return std::find(baseline.equipped.begin(), baseline.equipped.end(), id) != baseline.equipped.end();
    };
    // Unequip additions before reducing the slot count. Learned tiers persist.
    const auto list = Catalog::GetPerks(ctx.perks);
    for (const auto& perk : list) {
        std::string id;
        if (perk.equipped && !perk.passive && Catalog::PerkStatusName(perk, id) && !contains(id)) {
            Reflect::Call call(ctx.perks, "ServerUnequipPerk");
            call.Raw("PerkName", perk.name, static_cast<size_t>(perk.nameSize));
            call.Run();
        }
    }
    if (auto* slots = Reflect::Value<int32_t>(ctx.perks, "MaxEquippedPerks", "IntProperty")) *slots = baseline.rawSlots;
    for (const auto& perk : list) {
        std::string id;
        if (!perk.equipped && !perk.passive && Catalog::PerkStatusName(perk, id) && contains(id)) {
            Reflect::Call call(ctx.perks, "ServerEquipPerk");
            call.Raw("PerkName", perk.name, static_cast<size_t>(perk.nameSize));
            call.Run();
        }
    }
}

void RestoreAll(Game::Ctx& ctx) {
    for (const auto& [id, override] : personalDamage) {
        auto* player = Game::FindPlayer(ctx.world, id);
        auto* pawn = Game::PawnOf(player);
        if (player && override.playerKey == reinterpret_cast<uintptr_t>(player) && SamePawn(override, pawn))
            if (auto* field = DamageField(pawn)) *field = override.original;
    }
    personalDamage.clear();
    RestoreRules(ctx.settings);
    RestoreMutations(ctx);
    Hooks::buildAnywhere.store(baseline.anywhere);
    autoPickup = false;
    if (god) SetGod(ctx, false);
    god = infiniteStamina = noHunger = false;
    if (noclip && ctx.character) ctx.character->SetActorEnableCollision(true);
    if (flying || noclip) StopFlying(ctx);
    flying = noclip = false;
    if (speed != 1.0f) SetSpeed(ctx, 1.0f);
    speed = 1.0f;
    jump = 1.0f;
    ApplyJump(ctx);
    if (timeSpeed != 1.0f) Cheat(ctx, "TimeSpeed", [](Reflect::Call& c) { c.Float("Multiplier", 1.0f); });
    timeSpeed = 1.0f;
    outgoingEnabled = incomingEnabled = slotsChanged = false;
}

} // namespace

// ------------------------------------------------------------------------------- public

void BeginSession(Game::Ctx& ctx) {
    personalDamage.clear();
    CaptureRules(ctx.settings);
    CaptureMutations(ctx);
    baseline.anywhere = Hooks::buildAnywhere.load();
    ResolveSpawnedLayout();
    outgoingEnabled = incomingEnabled = slotsChanged = false;
    god = infiniteStamina = noHunger = flying = noclip = false;
    speed = jump = 1.0f;
    walkBaseline = jumpBaseline = flySpeedBaseline = flyBrakeBaseline = -1;
    godFlags = {};
    autoPickup = false;
    collected = 0;
    hasBack = false;
    timeSpeed = 1.0f;
    lockedHour = -1;
}

void EndSession() {
    // The world (and every actor) is gone; just forget references.
    personalDamage.clear();
    rules.clear();
    god = infiniteStamina = noHunger = flying = noclip = false;
    autoPickup = false;
    hasBack = false;
    walkBaseline = jumpBaseline = flySpeedBaseline = flyBrakeBaseline = -1;
    godFlags = {};
    spawned = {};
}

void Execute(Game::Ctx& ctx, const Bridge::Request& r, const Bridge::Snapshot& s, Outcome& o) {
    auto require = [&](Feature f) {
        if (Has(s, f)) return true;
        o.message = Unavailable(s, f);
        o.tone = Tone::Warning;
        return false;
    };
    auto done = [&](const std::wstring& text, Tone tone = Tone::Success) {
        o.message = text;
        o.tone = tone;
    };
    auto cheatDone = [&](Feature f, const char* function, const wchar_t* text) {
        if (!require(f)) return;
        if (Cheat(ctx, function)) done(text);
        else done(L"El juego no aceptó la orden. Consulta el registro.", Tone::Error);
    };

    switch (r.action) {
    case Action::Refresh:
        break;

    // ---------------------------------------------------------------- items
    case Action::Spawn: {
        if (!require(r.playerId < 0 ? Feature::Items : Feature::ItemsForOthers)) break;
        std::wstring error;
        if (GiveItem(ctx, r.playerId, r.id, r.count, error)) {
            std::wstring name = Paths::Wide(r.id);
            if (s.items)
                for (const auto& item : *s.items)
                    if (item.id == r.id) name = item.name;
            done(L"Generado: " + std::to_wstring(r.count) + L" × " + name);
        } else {
            done(error, Tone::Error);
        }
        break;
    }

    // ---------------------------------------------------------------- craft / build
    case Action::FreeCraft:
        if (!require(Feature::FreeCraft)) break;
        // Never touches bUnlockAllRecipes: only learned recipes become free.
        SetRuleBool(ctx.settings, "bRecipesRequireIngredients", !r.enabled);
        done(r.enabled ? L"Fabricación sin recursos activada: solo tus recetas aprendidas." : L"Fabricación sin recursos desactivada.");
        break;
    case Action::FreeBuild:
        if (!require(Feature::FreeBuild)) break;
        SetRuleBool(ctx.settings, "bAutoCompleteBuildings", r.enabled);
        done(r.enabled ? L"Los nuevos planos se completarán sin materiales." : L"Construcción sin recursos desactivada.");
        break;
    case Action::BuildAnywhere:
        if (!require(Feature::BuildAnywhere)) break;
        Hooks::buildAnywhere.store(r.enabled);
        done(r.enabled ? L"Construcción sin restricciones activada." : L"Restricciones de colocación restauradas.");
        break;
    case Action::FinishBlueprints:
        cheatDone(Feature::FinishBlueprints, "BuildAllBuildings", L"Se solicitó completar los planos colocados.");
        break;
    case Action::BuildingIntegrity:
        if (!require(Feature::BuildingRules)) break;
        SetRuleBool(ctx.settings, "bEnableBuildingIntegrity", r.enabled);
        done(r.enabled ? L"Integridad estructural activada." : L"Integridad estructural desactivada: las construcciones no necesitan apoyos.");
        break;
    case Action::BuildingHealth:
        if (!require(Feature::BuildingRules)) break;
        SetRuleFloat(ctx.settings, "BuildingHealthMultiplier", r.value);
        done(L"Resistencia de las construcciones actualizada.");
        break;

    // ---------------------------------------------------------------- damage
    case Action::OutgoingDamage: {
        if (r.scope == Bridge::DamageScope::World) {
            if (!require(Feature::DamageWorld)) break;
            auto* rule = FindRule("PlayerDamageMultiplier");
            SetRuleFloat(ctx.settings, "PlayerDamageMultiplier", r.enabled ? r.value : (rule ? rule->baseFloat : 1.0f));
            outgoingEnabled = r.enabled;
            done(r.enabled ? L"Daño causado aplicado al mundo y a todos los jugadores." : L"Daño del mundo restaurado al valor inicial.");
            break;
        }
        if (!require(Feature::DamagePersonal)) break;
        const int id = r.scope == Bridge::DamageScope::Self ? ctx.playerState->PlayerId : r.playerId;
        auto* psTarget = Game::FindPlayer(ctx.world, id);
        auto* target = Game::PawnOf(psTarget);
        float* field = DamageField(target);
        if (!target || !field) {
            done(L"Ese jugador ya no está disponible.", Tone::Warning);
            break;
        }
        auto existing = personalDamage.find(id);
        if (r.enabled) {
            if (existing == personalDamage.end()) {
                if (!std::isfinite(*field) || *field < 0) {
                    done(L"El daño de ese personaje no está disponible.", Tone::Warning);
                    break;
                }
                PersonalDamage adjustment;
                adjustment.playerKey = reinterpret_cast<uintptr_t>(psTarget);
                CapturePawn(adjustment, target);
                existing = personalDamage.emplace(id, adjustment).first;
            }
            existing->second.multiplier = r.value;
            *field = r.value;
            done(L"Daño aplicado solo a " + Game::PlayerName(psTarget) + L".");
        } else {
            if (existing != personalDamage.end()) {
                if (SamePawn(existing->second, target)) *field = existing->second.original;
                personalDamage.erase(existing);
            }
            done(L"Daño personal desactivado. Los demás jugadores no cambian.");
        }
        break;
    }
    case Action::IncomingDamage: {
        if (!require(Feature::DamageIncoming)) break;
        auto* rule = FindRule("EnemyDamageMultiplier");
        SetRuleFloat(ctx.settings, "EnemyDamageMultiplier", r.enabled ? r.value : (rule ? rule->baseFloat : 1.0f));
        incomingEnabled = r.enabled;
        done(r.enabled ? L"Multiplicador de daño enemigo aplicado al mundo." : L"Daño enemigo restaurado al valor inicial.");
        break;
    }

    // ---------------------------------------------------------------- mutations
    case Action::MutationSlots: {
        if (!require(Feature::Mutations)) break;
        const int effective = CallInt(ctx.perks, "GetMaxEquippedPerks", -1);
        const int equipped = CallInt(ctx.perks, "GetNumEquippedPerks", 0);
        auto* raw = Reflect::Value<int32_t>(ctx.perks, "MaxEquippedPerks", "IntProperty");
        if (effective < 0 || !raw) {
            done(L"No se pudieron leer las ranuras de mutaciones.", Tone::Error);
            break;
        }
        if (r.count < equipped) {
            done(L"Desactiva algunas mutaciones antes de reducir las ranuras.", Tone::Warning);
            break;
        }
        // Respect the game's own base/upgrade calculation instead of assuming +2.
        *raw += r.count - effective;
        slotsChanged = true;
        done(L"Límite de mutaciones actualizado.");
        o.mutationsChanged = true;
        break;
    }
    case Action::UnlockMutation:
    case Action::EquipMutation:
    case Action::UnequipMutation: {
        if (!require(Feature::Mutations)) break;
        const std::wstring text = MutationAction(ctx, r);
        const bool good = text.find(L"no ") == std::wstring::npos && text.find(L"Primero") == std::wstring::npos &&
                          text.find(L"Elige") == std::wstring::npos;
        done(text, good ? Tone::Success : Tone::Warning);
        o.mutationsChanged = true;
        break;
    }

    // ---------------------------------------------------------------- pickup / cleanup
    case Action::AutoPickup:
        if (!require(Feature::AutoPickup)) break;
        autoPickup = r.enabled;
        pickupRadius = r.value;
        nextPickup = 0;
        done(r.enabled ? L"Recogida automática activada para tu mochila." : L"Recogida automática desactivada.");
        break;
    case Action::ClearNearby: {
        if (!require(Feature::ClearNearby)) break;
        nextPickup = GetTickCount64() + 500;
        auto items = NearbyItems(ctx, r.value, false);
        int count = 0;
        for (auto* actor : items) {
            if (!actor || actor->bActorIsBeingDestroyed) continue;
            actor->SetLifeSpan(0.01f);
            ++count;
        }
        done(L"Se eliminarán " + std::to_wstring(count) + L" objetos sueltos cercanos.");
        break;
    }

    // ---------------------------------------------------------------- points & utilities
    case Action::AddWhiteMolars:
    case Action::AddGoldMolars:
    case Action::AddBuggyPoints:
    case Action::AddScience: {
        const Feature f = r.action == Action::AddWhiteMolars ? Feature::WhiteMolars
                        : r.action == Action::AddGoldMolars  ? Feature::GoldMolars
                        : r.action == Action::AddBuggyPoints ? Feature::BuggyPoints
                                                             : Feature::Science;
        if (!require(f)) break;
        const char* function = r.action == Action::AddWhiteMolars ? "AddPersonalUpgradePoints"
                             : r.action == Action::AddGoldMolars  ? "AddPartyUpgradePoints"
                             : r.action == Action::AddBuggyPoints ? "AddBuggyUpgradePoints"
                                                                  : "AddScience";
        if (Cheat(ctx, function, [&](Reflect::Call& c) { c.Int("Amount", r.count); }))
            done(L"Se añadieron " + std::to_wstring(r.count) + L" puntos. Este progreso puede guardarse.");
        else
            done(L"El juego no aceptó la orden.", Tone::Error);
        break;
    }
    case Action::Heal:
        if (!require(Feature::Heal)) break;
        if (!ctx.health) { done(L"Tu personaje no está disponible.", Tone::Warning); break; }
        Heal(ctx);
        done(L"Salud restaurada.");
        break;
    case Action::Revive:
        cheatDone(Feature::Revive, "Revive", L"Reanimación solicitada.");
        break;
    case Action::RestoreStamina:
        if (!require(Feature::RestoreStamina)) break;
        if (CallVoid(ctx.stamina, "Restore")) done(L"Resistencia restaurada.");
        else done(L"Tu personaje no está disponible.", Tone::Warning);
        break;
    case Action::FullRestore:
        cheatDone(Feature::FullRestore, "FullRestore", L"Salud, comida, agua y resistencia restauradas.");
        break;

    // ---------------------------------------------------------------- player
    case Action::GodMode:
        if (!require(Feature::GodMode)) break;
        god = r.enabled;
        SetGod(ctx, god);
        if (god) Heal(ctx);
        nextGod = 0;
        done(god ? L"Modo dios activado." : L"Modo dios desactivado.");
        break;
    case Action::InfiniteStamina:
        if (!require(Feature::InfiniteStamina)) break;
        infiniteStamina = r.enabled;
        nextStamina = 0;
        done(r.enabled ? L"Resistencia infinita activada." : L"Resistencia infinita desactivada.");
        break;
    case Action::NoHunger:
        if (!require(Feature::NoHunger)) break;
        noHunger = r.enabled;
        nextHunger = 0;
        done(r.enabled ? L"Comida y agua se mantendrán llenas." : L"Hambre y sed normales.");
        break;
    case Action::Speed:
        if (!require(Feature::Speed)) break;
        speed = r.enabled ? r.value : 1.0f;
        SetSpeed(ctx, speed);
        done(r.enabled ? L"Velocidad ajustada." : L"Velocidad normal.");
        break;
    case Action::Jump:
        if (!require(Feature::Jump)) break;
        jump = r.enabled ? r.value : 1.0f;
        ApplyJump(ctx);
        done(r.enabled ? L"Salto ajustado." : L"Salto normal.");
        break;
    case Action::Fly:
        if (!require(Feature::Fly)) break;
        flying = r.enabled || noclip;
        if (r.enabled) StartFlying(ctx);
        else if (!noclip) StopFlying(ctx);
        done(r.enabled ? L"Modo vuelo activado: Espacio sube, Ctrl baja." : L"Modo vuelo desactivado.");
        break;
    case Action::Noclip:
        if (!require(Feature::Fly)) break;
        if (!ctx.character) { done(L"Tu personaje no está disponible.", Tone::Warning); break; }
        noclip = r.enabled;
        ctx.character->SetActorEnableCollision(!noclip);
        if (noclip) {
            flying = true;
            StartFlying(ctx);
        } else {
            flying = false;
            StopFlying(ctx);
        }
        done(noclip ? L"Atravesar paredes activado (incluye vuelo)." : L"Colisiones restauradas.");
        break;

    // ---------------------------------------------------------------- teleport
    case Action::TeleportTo:
        if (!require(Feature::Teleport)) break;
        if (Teleport(ctx, ctx.character, SDK::FVector{r.x, r.y, r.z}, true)) done(L"Teletransporte completado.");
        else done(L"El juego no permitió el teletransporte.", Tone::Warning);
        break;
    case Action::TeleportForward: {
        if (!require(Feature::Teleport) || !ctx.character) break;
        const SDK::FVector from = ctx.character->K2_GetActorLocation();
        const SDK::FVector forward = ctx.character->GetActorForwardVector();
        const SDK::FVector to{from.X + forward.X * r.value, from.Y + forward.Y * r.value, from.Z + forward.Z * r.value + 20};
        if (Teleport(ctx, ctx.character, to, true)) done(L"Has avanzado " + std::to_wstring(static_cast<int>(r.value)) + L" unidades.");
        else done(L"Hay un obstáculo: el juego no permitió el teletransporte.", Tone::Warning);
        break;
    }
    case Action::TeleportBack:
        if (!require(Feature::Teleport)) break;
        if (!hasBack) { done(L"Todavía no hay una posición anterior.", Tone::Warning); break; }
        {
            const SDK::FVector target = back;
            if (Teleport(ctx, ctx.character, target, true)) done(L"Has vuelto a la posición anterior.");
        }
        break;
    case Action::TeleportToPlayer:
    case Action::BringPlayer: {
        if (!require(Feature::Teleport)) break;
        auto* other = Game::PawnOf(Game::FindPlayer(ctx.world, r.playerId));
        if (!other || !ctx.character) { done(L"Ese jugador no está disponible.", Tone::Warning); break; }
        if (r.action == Action::TeleportToPlayer) {
            SDK::FVector to = other->K2_GetActorLocation();
            to.Z += 60;
            if (Teleport(ctx, ctx.character, to, true)) done(L"Te has teletransportado con " + Game::PlayerName(other->PlayerState) + L".");
        } else {
            SDK::FVector to = ctx.character->K2_GetActorLocation();
            to.Z += 60;
            if (Teleport(ctx, other, to, false)) done(Game::PlayerName(other->PlayerState) + L" está contigo.");
        }
        break;
    }
    case Action::BringParty:
        cheatDone(Feature::TeleportParty, "TeleportPartyToMe", L"Todo el grupo viene hacia ti.");
        break;

    // ---------------------------------------------------------------- world
    case Action::AdvanceHours:
        if (!require(Feature::AdvanceTime)) break;
        if (Cheat(ctx, "AdvanceTimeByHours", [&](Reflect::Call& c) { c.Float("Hours", static_cast<float>(r.count)); }))
            done(r.count == 1 ? L"Se avanzó una hora del mundo." : L"Se avanzaron " + std::to_wstring(r.count) + L" horas del mundo.");
        break;
    case Action::SetTime:
        if (!require(Feature::SetTime)) break;
        if (Cheat(ctx, "SetTime", [&](Reflect::Call& c) { c.Int("Hour", r.count).Bool("Lock", r.enabled); })) {
            lockedHour = r.enabled ? r.count : -1;
            wchar_t text[96];
            swprintf(text, 96, L"Hora fijada a las %02d:00%ls.", r.count, r.enabled ? L" (bloqueada)" : L"");
            done(text);
        }
        break;
    case Action::TimeSpeed:
        if (!require(Feature::TimeSpeed)) break;
        if (Cheat(ctx, "TimeSpeed", [&](Reflect::Call& c) { c.Float("Multiplier", r.value); })) {
            timeSpeed = r.value;
            done(L"Velocidad del tiempo actualizada.");
        }
        break;
    case Action::SetRule: {
        if (!require(Feature::WorldRules)) break;
        auto* rule = FindRule(r.id);
        if (!rule) { done(L"Esa regla no existe en esta versión.", Tone::Warning); break; }
        if (rule->isBool) SetRuleBool(ctx.settings, r.id.c_str(), r.enabled);
        else SetRuleFloat(ctx.settings, r.id.c_str(), r.value);
        done(L"Regla actualizada: " + rule->label);
        break;
    }
    case Action::RestoreRules:
        if (!require(Feature::WorldRules)) break;
        RestoreRules(ctx.settings);
        outgoingEnabled = incomingEnabled = false;
        done(L"Reglas del mundo restauradas al inicio de la sesión.");
        break;
    case Action::RevealMap:
        cheatDone(Feature::RevealMap, "RevealMapFogOfWar", L"Mapa revelado.");
        break;
    case Action::UnlockLandmarks:
        if (!require(Feature::UnlockLandmarks)) break;
        Cheat(ctx, "UnlockAllPOIs");
        if (Has(s, Feature::RevealMap)) Cheat(ctx, "RevealMapFogOfWar");
        if (ctx.cheats && Reflect::HasFunction(ctx.cheats, "UnlockMapMarkersUI")) Cheat(ctx, "UnlockMapMarkersUI");
        done(L"Puntos de interés descubiertos y mapa revelado.");
        break;
    case Action::UnlockRecipes: {
        if (!require(Feature::UnlockRecipes)) break;
        const auto mode = static_cast<uint8_t>(Reflect::EnumValue("ERecipeUnlockMode", "IncludeHidden", 1));
        if (Cheat(ctx, "UnlockAllRecipes", [&](Reflect::Call& c) { c.Byte("Mode", mode); })) done(L"Todas las recetas desbloqueadas.");
        break;
    }
    case Action::UnlockTechTrees:
        if (!require(Feature::UnlockTechTrees)) break;
        Cheat(ctx, "UnlockAllTechTrees");
        if (ctx.cheats && Reflect::HasFunction(ctx.cheats, "UnlockAllTechTreeRecipes")) Cheat(ctx, "UnlockAllTechTreeRecipes");
        done(L"Árboles tecnológicos desbloqueados.");
        break;
    case Action::UnlockScabs:
        if (!require(Feature::UnlockScabs)) break;
        Cheat(ctx, "UnlockAllColorThemes");
        if (ctx.cheats && Reflect::HasFunction(ctx.cheats, "UnlockSCAB")) Cheat(ctx, "UnlockSCAB");
        done(L"Temas de color de la SCA.B desbloqueados.");
        break;
    case Action::OmniToolTier:
        if (!require(Feature::OmniTool)) break;
        if (Cheat(ctx, "SetOmniToolTier", [&](Reflect::Call& c) { c.Byte("Tier", static_cast<uint8_t>(r.count)); }))
            done(L"Omniherramienta mejorada al nivel " + std::to_wstring(r.count) + L".");
        break;
    case Action::RepairItems:
        cheatDone(Feature::RepairItems, "RepairAllItems", L"Equipo reparado.");
        break;
    case Action::RecoverBackpacks:
        cheatDone(Feature::RecoverBackpacks, "RecoverBackpacks", L"Mochilas recuperadas.");
        break;
    case Action::RespawnResources:
        cheatDone(Feature::RespawnResources, "ForceRespawnFoliage", L"Recursos regenerados en el mapa.");
        break;
    case Action::EndRaid:
        if (!require(Feature::EndRaid)) break;
        Cheat(ctx, "ForceEndWave");
        if (ctx.cheats && Reflect::HasFunction(ctx.cheats, "ResetPartyHeat")) Cheat(ctx, "ResetPartyHeat");
        done(L"Asedio terminado y amenaza reiniciada.");
        break;
    case Action::Arachnophobia:
        if (!require(Feature::Arachnophobia)) break;
        if (Cheat(ctx, "SetArachnophobiaSafeMode", [&](Reflect::Call& c) { c.Int("Level", r.count); }))
            done(r.count ? L"Modo aracnofobia aplicado." : L"Modo aracnofobia desactivado.");
        break;
    case Action::ToggleHud:
        cheatDone(Feature::ToggleHud, "ToggleHUD", L"Interfaz del juego alternada.");
        break;

    // ---------------------------------------------------------------- creatures
    case Action::TameNearest:
        cheatDone(Feature::TameNearest, "TameNearest", L"Se intentó domar la criatura más cercana.");
        break;
    case Action::ReviveMount:
        cheatDone(Feature::ReviveMount, "ReviveMount", L"Montura reanimada.");
        break;
    case Action::SpawnMount: {
        if (!require(Feature::SpawnMount)) break;
        static const std::unordered_set<std::string> allowed = {"RedSoldierAnt", "BlackSoldierAnt", "Ladybug",
                                                                 "DivingBellSpider", "Orbweaver", "ToeBiter"};
        if (!allowed.contains(r.id)) break;
        const std::string function = "SpawnAndMount" + r.id;
        if (!ctx.cheats || !Reflect::HasFunction(ctx.cheats, function)) {
            done(L"Esa montura no existe en esta versión del juego.", Tone::Warning);
            break;
        }
        if (Cheat(ctx, function.c_str(), [&](Reflect::Call& c) { c.Int("Tier", r.count); })) done(L"¡Montura lista!");
        break;
    }

    // ---------------------------------------------------------------- session
    case Action::Restore:
        RestoreAll(ctx);
        done(L"Ajustes restaurados y recogida automática desactivada. Objetos, puntos y niveles aprendidos se conservan.");
        o.mutationsChanged = true;
        break;

    default:
        break;
    }
}

uint64_t Maintain(Game::Ctx& ctx, uint64_t now) {
    if (god && ctx.health && now >= nextGod) {
        nextGod = now + 200;
        SetGod(ctx, true);
        const float max = CallFloat(ctx.health, "GetMaxHealth"), current = CallFloat(ctx.health, "GetCurrentHealth");
        if (max > 0 && current < max) Heal(ctx);
    }
    if (infiniteStamina && ctx.stamina && now >= nextStamina) {
        nextStamina = now + 250;
        CallVoid(ctx.stamina, "Restore");
    }
    if (noHunger && ctx.survival && now >= nextHunger) {
        nextHunger = now + 1000;
        FillHunger(ctx);
    }
    if (now >= nextReapply) {
        nextReapply = now + 500;
        if (jump != 1.0f) ApplyJump(ctx);
        if (speed != 1.0f && !speedViaCheat) SetSpeed(ctx, speed);
        if (noclip && ctx.character) ctx.character->SetActorEnableCollision(false);
    }
    if (autoPickup && now >= nextPickup) {
        nextPickup = now + 500;
        Pickup(ctx);
    }
    if (flying && ctx.character) {
        FlyControl(ctx);
        return 15;
    }
    return 100;
}

void Fill(Game::Ctx& ctx, Bridge::Snapshot& s) {
    auto* settings = ctx.settings;
    s.host = ctx.host;
    s.freeCraft = !RuleBool(settings, "bRecipesRequireIngredients", true);
    s.freeBuild = RuleBool(settings, "bAutoCompleteBuildings", false);
    s.buildingIntegrity = RuleBool(settings, "bEnableBuildingIntegrity", true);
    s.buildingHealth = RuleFloat(settings, "BuildingHealthMultiplier", 1.0f);
    s.anywhere = Hooks::buildAnywhere.load();
    s.outgoing = RuleFloat(settings, "PlayerDamageMultiplier", 1.0f);
    s.incoming = RuleFloat(settings, "EnemyDamageMultiplier", 1.0f);
    s.outgoingEnabled = outgoingEnabled;
    s.incomingEnabled = incomingEnabled;
    s.slotsChanged = slotsChanged;
    if (s.Has(Feature::Mutations)) s.slots = CallInt(ctx.perks, "GetMaxEquippedPerks", s.slots);
    s.autoPickup = autoPickup;
    s.pickupRadius = pickupRadius;
    s.collected = collected;
    s.god = god;
    s.infiniteStamina = infiniteStamina;
    s.noHunger = noHunger;
    s.fly = flying && !noclip;
    s.noclip = noclip;
    s.speed = speed;
    s.jump = jump;
    s.timeSpeed = timeSpeed;
    s.lockedHour = lockedHour;

    s.health = s.maxHealth = s.food = s.maxFood = s.water = s.maxWater = 0;
    if (ctx.health && s.Has(Feature::Heal)) {
        s.health = CallFloat(ctx.health, "GetCurrentHealth");
        s.maxHealth = CallFloat(ctx.health, "GetMaxHealth");
    }
    if (ctx.survival && s.Has(Feature::NoHunger)) {
        s.maxFood = CallFloat(ctx.survival, "GetMaxHunger");
        s.maxWater = CallFloat(ctx.survival, "GetMaxThirst");
        if (auto* food = Reflect::Value<float>(ctx.survival, "CurrentFood", "FloatProperty")) s.food = *food;
        if (auto* water = Reflect::Value<float>(ctx.survival, "CurrentWater", "FloatProperty")) s.water = *water;
    }
    s.hasPosition = ctx.character != nullptr;
    if (ctx.character) {
        const SDK::FVector location = ctx.character->K2_GetActorLocation();
        s.x = location.X;
        s.y = location.Y;
        s.z = location.Z;
    }
    s.hasBackPosition = hasBack;

    s.players.clear();
    RefreshPersonalDamage(ctx.world, &s.players, ctx.playerState ? ctx.playerState->PlayerId : -1);

    s.rules.clear();
    for (const auto& rule : rules) {
        Bridge::Rule out;
        out.id = rule.name;
        out.label = rule.label;
        out.isBool = rule.isBool;
        if (rule.isBool) {
            out.boolValue = settings && Reflect::GetBool(settings, rule.boolField);
            out.changed = out.boolValue != rule.baseBool;
        } else {
            out.floatValue = RuleFloat(settings, rule.name.c_str(), rule.baseFloat);
            out.changed = std::fabs(out.floatValue - rule.baseFloat) > 0.0001f;
        }
        s.rules.push_back(std::move(out));
    }
}

} // namespace Actions
