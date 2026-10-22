// SPDX-License-Identifier: GPL-3.0-or-later
// Request queue + snapshot publishing (pattern from Panel 0.2.0) driven by the game thread.
#include "Session.hpp"

#include "Actions.hpp"
#include "Catalog.hpp"
#include "Context.hpp"
#include "Hooks.hpp"
#include "core/Log.hpp"
#include "core/Reflect.hpp"
#include "core/Resolver.hpp"
#include "common/Paths.hpp"

#include "SDK/Engine_classes.hpp"
#include "SDK/Maine_classes.hpp"

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace Game {

Classes& GetClasses() {
    static Classes classes;
    return classes;
}

SDK::APlayerState* FindPlayer(SDK::UWorld* world, int id) {
    if (!world || !world->GameState) return nullptr;
    for (auto* p : world->GameState->PlayerArray)
        if (p && p->PlayerId == id) return p;
    return nullptr;
}

SDK::ACharacter* PawnOf(SDK::APlayerState* player) {
    auto* pawn = player ? player->PawnPrivate : nullptr;
    auto* type = GetClasses().survivalPlayerCharacter;
    if (!pawn || !type || !pawn->IsA(type) || pawn->bActorIsBeingDestroyed) return nullptr;
    return static_cast<SDK::ACharacter*>(pawn);
}

std::wstring PlayerName(SDK::APlayerState* player) {
    // Read the replicated name in place (no allocation, unlike GetPlayerName()).
    return player ? Reflect::ReadString(&player->PlayerNamePrivate) : std::wstring();
}

SDK::UObject* EnsureCheatManager(Ctx& ctx) {
    auto* type = GetClasses().survivalCheatManager;
    if (!ctx.controller || !type) return nullptr;
    if (ctx.controller->CheatManager && ctx.controller->CheatManager->IsA(type)) return ctx.controller->CheatManager;
    if (!ctx.host) return nullptr;
    // Same approach as Grounded2Minimal: create the game's own cheat manager for the local
    // controller. It only runs the game's built-in debug commands.
    ctx.controller->CheatClass = type;
    auto* created = SDK::UGameplayStatics::SpawnObject(type, ctx.controller);
    if (!created || !created->IsA(type)) {
        Log::Error("Sesión", "No se pudo crear el gestor de comandos del juego");
        return nullptr;
    }
    ctx.controller->CheatManager = static_cast<SDK::UCheatManager*>(created);
    Log::Info("Sesión", "Gestor de comandos del juego creado");
    return created;
}

} // namespace Game

namespace Session {
namespace {

std::mutex mutex;
std::deque<Bridge::Request> pending;
std::atomic<size_t> pendingCount{0};
Bridge::Snapshot published, state;
std::atomic<uint64_t> publishedRevision{0};
std::atomic<uint64_t> lastTick{0};

uintptr_t currentWorld = 0, currentPlayer = 0;
bool sessionActive = false;
bool blocked = false;
uint64_t nextMaintain = 0, nextSnapshot = 0, nextIdlePublish = 0;
bool mutationsDirty = true;

void Publish() {
    ++state.revision;
    std::lock_guard lock(mutex);
    published = state;
    publishedRevision.store(state.revision, std::memory_order_release);
}

void Message(const std::wstring& text, Bridge::Tone tone) {
    state.message = text;
    state.tone = tone;
    ++state.messageId;
}

std::string lastResolveProblem;
bool Fail(const char* why) {
    if (lastResolveProblem != why) {
        Log::Info("Sesión", std::string("Sin partida activa: ") + why);
        lastResolveProblem = why;
    }
    return false;
}

bool Resolve(Game::Ctx& ctx) {
    auto& classes = Game::GetClasses();
    ctx.world = SDK::UWorld::GetWorld();
    if (!ctx.world) return Fail("no hay mundo cargado");
    auto* instance = ctx.world->OwningGameInstance;
    if (!instance || instance->LocalPlayers.Num() < 1) return Fail("sin jugador local");
    auto* local = instance->LocalPlayers[0];
    ctx.controller = local ? local->PlayerController : nullptr;
    if (!ctx.controller || !classes.survivalPlayerController || !ctx.controller->IsA(classes.survivalPlayerController))
        return Fail("el controlador local no es de partida");
    ctx.playerState = ctx.controller->PlayerState;
    if (!ctx.playerState || !classes.survivalPlayerState || !ctx.playerState->IsA(classes.survivalPlayerState))
        return Fail("el estado del jugador no es de partida (SurvivalPlayerState)");
    lastResolveProblem.clear();
    ctx.host = ctx.world->AuthorityGameMode != nullptr;
    ctx.gameState = ctx.world->GameState;
    ctx.modes = Reflect::Obj(ctx.gameState, "SurvivalModeManagerComponent");
    ctx.settings = Reflect::Obj(ctx.modes, "GameModeSettings");
    ctx.perks = Reflect::Obj(ctx.playerState, "PerkComponent");
    ctx.character = Game::PawnOf(ctx.playerState);
    if (ctx.character) {
        ctx.health = Reflect::Obj(ctx.character, "HealthComponent");
        ctx.stamina = Reflect::Obj(ctx.character, "StaminaComponent");
        ctx.survival = Reflect::Obj(ctx.character, "SurvivalComponent");
        ctx.inventory = Reflect::Obj(ctx.character, "InventoryComponent");
    }
    auto* cheats = ctx.controller->CheatManager;
    ctx.cheats = cheats && classes.survivalCheatManager && cheats->IsA(classes.survivalCheatManager) ? cheats : nullptr;
    return true;
}

void ResolveClasses() {
    auto& c = Game::GetClasses();
    c.survivalPlayerCharacter = Reflect::FindClass("SurvivalPlayerCharacter");
    c.survivalPlayerState = Reflect::FindClass("SurvivalPlayerState");
    c.survivalPlayerController = Reflect::FindClass("SurvivalPlayerController");
    c.survivalCheatManager = Reflect::FindClass("SurvivalCheatManager");
    c.spawnedItem = Reflect::FindClass("SpawnedItem");
    c.dataTable = SDK::UDataTable::StaticClass();
}

void StartSession(Game::Ctx& ctx) {
    if (sessionActive) Actions::EndSession();
    Log::Info("Sesión", "Nueva partida detectada (" + ctx.world->GetName() + ")");
    Reflect::Reset();
    Guard::Reset();
    Catalog::Reset();
    ResolveClasses();
    Resolve(ctx);
    currentWorld = reinterpret_cast<uintptr_t>(ctx.world);
    currentPlayer = reinterpret_cast<uintptr_t>(ctx.playerState);
    sessionActive = true;
    ++state.session;
    state.mapName = ctx.world->GetName();
    state.host = ctx.host;
    if (!ctx.host) {
        state.ready = false;
        Message(L"Solo el anfitrión puede usar el panel. Abre una partida en solitario o crea una partida como anfitrión.",
                Bridge::Tone::Warning);
        return;
    }
    ctx.cheats = Game::EnsureCheatManager(ctx);
    Hooks::InstallBuildAnywhere();
    Game::EvaluateFeatures(ctx, state.features, state.compatibilityNotes);
    Actions::BeginSession(ctx);
    std::wstring catalogMessage;
    std::shared_ptr<const std::vector<Bridge::Item>> items;
    if (Catalog::LoadItems(ctx, items, catalogMessage)) {
        state.items = items;
        ++state.catalogRevision;
    } else {
        state.items = std::make_shared<const std::vector<Bridge::Item>>();
        state.features[static_cast<size_t>(Bridge::Feature::Items)] = {false, catalogMessage};
        state.features[static_cast<size_t>(Bridge::Feature::ItemsForOthers)] = {false, catalogMessage};
    }
    mutationsDirty = true;
    state.ready = true;
    int ok = 0;
    for (const auto& f : state.features) ok += f.ok ? 1 : 0;
    Message(catalogMessage.empty() ? L"Conectado. " + std::to_wstring(ok) + L" de " + std::to_wstring(Bridge::kFeatureCount) +
                                         L" funciones verificadas en tu versión."
                                   : catalogMessage,
            catalogMessage.empty() ? Bridge::Tone::Success : Bridge::Tone::Warning);
}

} // namespace

void Tick() {
    if (blocked) return;
    const uint64_t now = GetTickCount64();
    lastTick.store(now, std::memory_order_relaxed);
    const bool hasRequest = pendingCount.load(std::memory_order_acquire) > 0;
    if (!hasRequest && now < nextMaintain && now < nextSnapshot) return;

    Bridge::Request request;
    bool requested = false;
    if (hasRequest) {
        std::lock_guard lock(mutex);
        if (!pending.empty()) {
            request = std::move(pending.front());
            pending.pop_front();
            pendingCount.store(pending.size(), std::memory_order_release);
            requested = true;
        }
    }

    if (!Game::GetClasses().survivalPlayerController) ResolveClasses();
    Game::Ctx ctx;
    if (!Resolve(ctx)) {
        if (sessionActive) {
            Actions::EndSession();
            sessionActive = false;
            currentWorld = currentPlayer = 0;
        }
        if (state.ready || requested || now >= nextIdlePublish) {
            state.ready = false;
            if (requested) Message(L"Carga una partida en la que seas anfitrión para usar el panel.", Bridge::Tone::Warning);
            Publish();
            nextIdlePublish = now + 2000;
        }
        nextMaintain = nextSnapshot = now + 500;
        return;
    }
    if (!sessionActive || currentWorld != reinterpret_cast<uintptr_t>(ctx.world) ||
        currentPlayer != reinterpret_cast<uintptr_t>(ctx.playerState)) {
        StartSession(ctx);
        nextSnapshot = 0;
    }
    if (!state.ready) {
        nextMaintain = nextSnapshot = now + 1000;
        if (requested) {
            Message(L"Solo el anfitrión puede usar el panel.", Bridge::Tone::Warning);
            Publish();
        }
        return;
    }
    if (!ctx.cheats) ctx.cheats = Game::EnsureCheatManager(ctx);

    if (requested) {
        if (request.action != Bridge::Action::Refresh && request.session != state.session) {
            Message(L"La partida o el personaje cambió. Repite la acción en la partida actual.", Bridge::Tone::Warning);
        } else if (!Bridge::Valid(request)) {
            Message(L"El valor solicitado no es válido.", Bridge::Tone::Warning);
        } else {
            Actions::Outcome outcome;
            Actions::Execute(ctx, request, state, outcome);
            if (!outcome.message.empty()) Message(outcome.message, outcome.tone);
            mutationsDirty |= outcome.mutationsChanged || request.action == Bridge::Action::Refresh;
        }
        nextSnapshot = 0;
    }
    if (now >= nextMaintain) nextMaintain = now + Actions::Maintain(ctx, now);
    if (now >= nextSnapshot) {
        Actions::Fill(ctx, state);
        if (mutationsDirty && state.Has(Bridge::Feature::Mutations)) {
            state.mutations = Catalog::Mutations(ctx);
            mutationsDirty = false;
        }
        state.player = Game::PlayerName(ctx.playerState);
        state.localPlayerId = ctx.playerState->PlayerId;
        Publish();
        nextSnapshot = now + 500;
    }
}

void SetEngineReport(const Resolver::Report& report) {
    std::lock_guard lock(mutex);
    state.resolved = report.objects && report.names && report.processEvent;
    state.gameVersion = report.gameVersion;
    state.engineVersion = report.engineVersion;
    ++state.revision;
    published = state;
    publishedRevision.store(state.revision, std::memory_order_release);
}

void SetStartupMessage(const std::wstring& message, bool resolved, bool block) {
    std::lock_guard lock(mutex);
    if (block) blocked = true;
    state.resolved = resolved;
    state.message = message;
    state.tone = block ? Bridge::Tone::Error : Bridge::Tone::Info;
    ++state.messageId;
    ++state.revision;
    published = state;
    publishedRevision.store(state.revision, std::memory_order_release);
}

uint64_t LastTickMs() { return lastTick.load(std::memory_order_relaxed); }

void MarkIdle() {
    // Called from the start-up thread when the game thread stopped ticking us (main menu).
    std::lock_guard lock(mutex);
    if (!published.ready) return;
    published.ready = false;
    published.message = L"Esperando una partida…";
    ++published.messageId;
    ++published.revision;
    publishedRevision.store(published.revision, std::memory_order_release);
}

} // namespace Session

// ------------------------------------------------------------------------------------- Bridge

namespace Bridge {

bool Submit(Request request) {
    if (!Valid(request)) return false;
    std::lock_guard lock(Session::mutex);
    if (request.action == Action::Refresh &&
        std::any_of(Session::pending.begin(), Session::pending.end(), [](const auto& p) { return p.action == Action::Refresh; }))
        return true;
    if (Session::pending.size() >= 64) return false;
    request.session = Session::published.session;
    Session::pending.push_back(std::move(request));
    Session::pendingCount.store(Session::pending.size(), std::memory_order_release);
    return true;
}

Snapshot Read() {
    std::lock_guard lock(Session::mutex);
    return Session::published;
}

uint64_t Revision() { return Session::publishedRevision.load(std::memory_order_acquire); }

} // namespace Bridge
