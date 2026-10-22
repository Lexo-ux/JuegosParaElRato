// SPDX-License-Identifier: GPL-3.0-or-later
// Contract between the panel window and the game thread.
// The window only sees copies; it never holds Unreal pointers or writes game memory.
// Requests are validated here (shared by both sides and by the unit tests).
#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Bridge {

enum class DamageScope : uint8_t { Self, Player, World };

// Everything the panel can do. Each feature is checked against the running game build;
// unavailable ones are shown disabled with the reason.
enum class Feature : uint8_t {
    Items, ItemsForOthers, SpanishNames,
    FreeCraft, FreeBuild, BuildAnywhere, FinishBlueprints, BuildingRules,
    DamageWorld, DamagePersonal, DamageIncoming,
    Mutations,
    AutoPickup, ClearNearby,
    WhiteMolars, GoldMolars, Science, BuggyPoints,
    Heal, Revive, RestoreStamina, FullRestore,
    GodMode, InfiniteStamina, NoHunger, Speed, Jump, Fly,
    Teleport, TeleportParty,
    AdvanceTime, SetTime, TimeSpeed, WorldRules,
    RevealMap, UnlockLandmarks, UnlockRecipes, UnlockTechTrees, UnlockScabs, OmniTool,
    RepairItems, RecoverBackpacks, RespawnResources, EndRaid, Arachnophobia, ToggleHud,
    TameNearest, SpawnMount, ReviveMount,
    Count
};
constexpr size_t kFeatureCount = static_cast<size_t>(Feature::Count);

enum class Action : uint8_t {
    Refresh,
    Spawn,                                                   // id, count, playerId (-1 = me)
    FreeCraft, FreeBuild, BuildAnywhere, FinishBlueprints,   // enabled
    BuildingIntegrity, BuildingHealth,                       // enabled / value
    OutgoingDamage, IncomingDamage,                          // enabled, value, scope, playerId
    MutationSlots, UnlockMutation, EquipMutation, UnequipMutation,
    AutoPickup, ClearNearby,                                 // enabled, value = radius
    AddWhiteMolars, AddGoldMolars, AddScience, AddBuggyPoints,
    Heal, Revive, RestoreStamina, FullRestore,
    GodMode, InfiniteStamina, NoHunger, Fly, Noclip,         // enabled
    Speed, Jump,                                             // enabled, value = multiplier
    TeleportTo,                                              // x, y, z
    TeleportForward,                                         // value = distance
    TeleportBack,
    TeleportToPlayer, BringPlayer,                           // playerId
    BringParty,
    AdvanceHours,                                            // count
    SetTime,                                                 // count = hour, enabled = lock
    TimeSpeed,                                               // value
    SetRule,                                                 // id, enabled / value
    RestoreRules,
    RevealMap, UnlockLandmarks, UnlockRecipes, UnlockTechTrees, UnlockScabs,
    OmniToolTier,                                            // count
    RepairItems, RecoverBackpacks, RespawnResources, EndRaid,
    Arachnophobia,                                           // count = level
    ToggleHud,
    TameNearest, ReviveMount,
    SpawnMount,                                              // id = creature, count = tier
    Restore,
    Count
};

struct Request {
    Action action = Action::Refresh;
    std::string id;
    float value = 0;
    int count = 1;
    bool enabled = false;
    uint64_t session = 0;
    DamageScope scope = DamageScope::Self;
    int playerId = -1;
    double x = 0, y = 0, z = 0;
};

// ---------------------------------------------------------------- validation (pure)
constexpr float kMinRadius = 25, kMaxRadius = 2000;

inline bool ValidRadius(float radius) { return std::isfinite(radius) && radius >= kMinRadius && radius <= kMaxRadius; }

inline bool WithinRadius(double x, double y, double z, double radius) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && ValidRadius(static_cast<float>(radius)) &&
           x * x + y * y + z * z <= radius * radius;
}

inline bool ValidMultiplier(float v, float max = 1000) { return std::isfinite(v) && v >= 0 && v <= max; }

inline bool ValidCoordinate(double v) { return std::isfinite(v) && std::fabs(v) < 1e7; }

inline bool Valid(const Request& r) {
    switch (r.action) {
    case Action::Spawn: return !r.id.empty() && r.count >= 1 && r.count <= 999;
    case Action::OutgoingDamage:
        if (r.scope != DamageScope::Self && r.scope != DamageScope::Player && r.scope != DamageScope::World) return false;
        if (r.scope == DamageScope::Player && r.playerId < 0) return false;
        [[fallthrough]];
    case Action::IncomingDamage: return ValidMultiplier(r.value);
    case Action::BuildingHealth: return std::isfinite(r.value) && r.value >= 0.1f && r.value <= 100;
    case Action::AutoPickup:
    case Action::ClearNearby: return ValidRadius(r.value);
    case Action::AddWhiteMolars:
    case Action::AddGoldMolars:
    case Action::AddBuggyPoints: return r.count >= 1 && r.count <= 999;
    case Action::AddScience: return r.count >= 1 && r.count <= 1000000;
    case Action::MutationSlots: return r.count >= 2 && r.count <= 32;
    case Action::UnlockMutation: return !r.id.empty() && r.count >= 1 && r.count <= 100;
    case Action::EquipMutation:
    case Action::UnequipMutation: return !r.id.empty();
    case Action::Speed: return std::isfinite(r.value) && r.value >= 0.25f && r.value <= 10;
    case Action::Jump: return std::isfinite(r.value) && r.value >= 0.5f && r.value <= 10;
    case Action::TeleportTo: return ValidCoordinate(r.x) && ValidCoordinate(r.y) && ValidCoordinate(r.z);
    case Action::TeleportForward: return std::isfinite(r.value) && r.value >= 10 && r.value <= 5000;
    case Action::TeleportToPlayer:
    case Action::BringPlayer: return r.playerId >= 0;
    case Action::AdvanceHours: return r.count >= 1 && r.count <= 48;
    case Action::SetTime: return r.count >= 0 && r.count <= 23;
    case Action::TimeSpeed: return std::isfinite(r.value) && r.value >= 0 && r.value <= 100;
    case Action::SetRule: return !r.id.empty() && std::isfinite(r.value) && r.value >= 0 && r.value <= 1000;
    case Action::OmniToolTier: return r.count >= 1 && r.count <= 10;
    case Action::Arachnophobia: return r.count >= 0 && r.count <= 3;
    case Action::SpawnMount: return !r.id.empty() && r.count >= 1 && r.count <= 3;
    default: return r.action < Action::Count;
    }
}

// "2,5" and "2.5" are both accepted; used by numeric text boxes.
inline bool ParseMultiplier(std::wstring_view text, float& result, float max = 1000) {
    std::string ascii;
    for (wchar_t c : text) {
        if (c > 127) return false;
        ascii += c == L',' ? '.' : static_cast<char>(c);
    }
    if (ascii.empty()) return false;
    auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == ascii.data() + ascii.size() && ValidMultiplier(result, max);
}

// Accent- and case-insensitive key for searching Spanish names.
inline std::wstring SearchKey(std::wstring s) {
    const std::wstring accented = L"áéíóúüñÁÉÍÓÚÜÑàèìòùÀÈÌÒÙ";
    const std::wstring plain = L"aeiouunAEIOUUNaeiouAEIOU";
    for (auto& c : s) {
        const auto pos = accented.find(c);
        if (pos != std::wstring::npos) c = plain[pos];
        if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    }
    return s;
}

// Loose item filter used by auto pickup and cleanup.
struct LooseItemState {
    bool destroying = false, attached = false, equipped = false, processing = false, inFlight = false, keyItem = false;
    bool inventoryItem = false, haulingItem = false;
};
inline bool EligibleLooseItem(const LooseItemState& s, bool pickup) {
    return !s.destroying && !s.attached && !s.equipped && !s.processing && !s.inFlight && !s.keyItem &&
           (s.inventoryItem || (!pickup && s.haulingItem));
}

// ---------------------------------------------------------------- snapshot
struct Item {
    std::string id;
    std::wstring name;
    std::wstring key;   // SearchKey(name + id), precomputed
    bool translated = false;
};

struct Mutation {
    std::string id;
    std::wstring name;
    int tier = 0, maxTier = 1;
    bool equipped = false;
};

struct Player {
    int id = -1;
    std::wstring name;
    bool available = false, damageEnabled = false, local = false;
    float damage = 1;
};

struct Rule {
    std::string id;           // property name in SurvivalGameModeSettings
    std::wstring label;       // Spanish label (or the raw name when unknown)
    bool isBool = true;
    bool boolValue = false;
    float floatValue = 1;
    bool changed = false;     // differs from the value captured when the session started
};

struct FeatureState {
    bool ok = false;
    std::wstring why;
};

enum class Tone : uint8_t { Info, Success, Warning, Error };

struct Snapshot {
    // engine / connection
    bool resolved = false;       // engine globals located
    bool ready = false;          // in a world, host, everything found
    bool host = false;
    std::string gameVersion, engineVersion;
    std::array<FeatureState, kFeatureCount> features{};
    std::vector<std::wstring> compatibilityNotes;

    // build / craft / damage
    bool freeCraft = false, freeBuild = false, anywhere = false, buildingIntegrity = true;
    float buildingHealth = 1;
    bool outgoingEnabled = false, incomingEnabled = false, slotsChanged = false;
    float outgoing = 1, incoming = 1;
    int slots = 2;
    int localPlayerId = -1;

    // pickup
    bool autoPickup = false;
    float pickupRadius = 300;
    uint64_t collected = 0;

    // player
    bool god = false, infiniteStamina = false, noHunger = false, fly = false, noclip = false;
    float speed = 1, jump = 1;
    float health = 0, maxHealth = 0, food = 0, maxFood = 0, water = 0, maxWater = 0;
    bool hasPosition = false, hasBackPosition = false;
    double x = 0, y = 0, z = 0;

    // world
    float timeSpeed = 1;
    int lockedHour = -1;

    uint64_t revision = 0, catalogRevision = 0, session = 0, messageId = 0;
    std::wstring player, message = L"Buscando las estructuras del juego…";
    Tone tone = Tone::Info;
    std::string mapName;

    std::shared_ptr<const std::vector<Item>> items;
    std::vector<Mutation> mutations;
    std::vector<Player> players;
    std::vector<Rule> rules;

    bool Has(Feature f) const { return features[static_cast<size_t>(f)].ok; }
};

// Implemented by the game layer (or by the preview with sample data).
bool Submit(Request request);  // non-blocking; false when the queue is full or the request is invalid
Snapshot Read();
uint64_t Revision();           // cheap check before copying a snapshot

} // namespace Bridge
