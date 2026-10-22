// SPDX-License-Identifier: GPL-3.0-or-later
// Grounded 2 · Panel en español — entry point.
// Derived from Grounded2Minimal (x0reaxeax) and Grounded2PanelES 0.2.0.
#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Reflect.hpp"
#include "core/Resolver.hpp"
#include "game/Hooks.hpp"
#include "game/Session.hpp"
#include "ui/PanelWindow.hpp"
#include "common/Paths.hpp"

#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/Engine_classes.hpp"

#include <windows.h>

#include <string>

namespace {

HMODULE self = nullptr;
HANDLE instanceMutex = nullptr;

// A pointer read from another thread is only trusted if the object array still holds it.
bool LiveObject(const SDK::UObject* object) {
    if (!object || !Mem::IsReadable(object, sizeof(SDK::UObject))) return false;
    const int index = object->Index;
    return index >= 0 && index < SDK::UObject::GObjects->Num() && SDK::UObject::GObjects->GetByIndex(index) == object;
}

// Engine members the panel reads through G2F (at their runtime offsets). They may move between
// game builds; if one disappeared or changed size, stop.
bool EngineLayoutOk() {
    bool ok = true;
    ok &= G2_FIELD_OK(UWorld, PersistentLevel);
    ok &= G2_FIELD_OK(UWorld, AuthorityGameMode);
    ok &= G2_FIELD_OK(UWorld, GameState);
    ok &= G2_FIELD_OK(UWorld, Levels);
    ok &= G2_FIELD_OK(UWorld, OwningGameInstance);
    ok &= G2_FIELD_OK(UGameInstance, LocalPlayers);
    ok &= G2_FIELD_OK(UPlayer, PlayerController);
    ok &= G2_FIELD_OK(UEngine, GameViewport);
    ok &= G2_FIELD_OK(UGameViewportClient, World);
    ok &= G2_FIELD_OK(AController, PlayerState);
    ok &= G2_FIELD_OK(APlayerController, CheatManager);
    ok &= G2_FIELD_OK(APlayerController, CheatClass);
    ok &= G2_FIELD_OK(APlayerState, PlayerId);
    ok &= G2_FIELD_OK(APlayerState, PawnPrivate);
    ok &= G2_FIELD_OK(APlayerState, PlayerNamePrivate);
    ok &= G2_FIELD_OK(APawn, PlayerState);
    ok &= G2_FIELD_OK(AGameStateBase, PlayerArray);
    ok &= G2_FIELD_OK(ACharacter, CharacterMovement);
    ok &= G2_FIELD_OK(UMovementComponent, Velocity);
    ok &= G2_FIELD_OK(UCharacterMovementComponent, MaxWalkSpeed);
    ok &= G2_FIELD_OK(UCharacterMovementComponent, JumpZVelocity);
    ok &= G2_FIELD_OK(UCharacterMovementComponent, MaxFlySpeed);
    ok &= G2_FIELD_OK(UCharacterMovementComponent, BrakingDecelerationFlying);
    ok &= G2_FIELD_OK(UCharacterMovementComponent, MovementMode);
    ok &= G2_FIELD_OK(UDataTable, RowStruct);
    ok &= G2_FIELD_OK(ULevel, OwningWorld);
    ok &= G2_SIZE(FVector);
    ok &= G2_SIZE(FRotator);
    ok &= G2_SIZE(FDataTableRowHandle);
    return ok;
}

SDK::APlayerController* LocalController() {
    auto* engine = SDK::UEngine::GetEngine();
    if (!LiveObject(engine)) return nullptr;
    auto* viewport = G2F(engine, UEngine, GameViewport);
    if (!LiveObject(viewport)) return nullptr;
    auto* world = G2F(viewport, UGameViewportClient, World);
    if (!LiveObject(world)) return nullptr;
    auto* instance = G2F(world, UWorld, OwningGameInstance);
    if (!LiveObject(instance)) return nullptr;
    auto& players = G2F(instance, UGameInstance, LocalPlayers);
    if (players.Num() < 1 || !LiveObject(players[0])) return nullptr;
    auto* controller = G2F(players[0], UPlayer, PlayerController);
    return LiveObject(controller) ? controller : nullptr;
}

DWORD WINAPI Startup(LPVOID) {
    Log::Init(Paths::DataDir());
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    Log::Info("Inicio", "Grounded 2 · Panel en español " G2P_VERSION " cargado en " + Paths::Utf8(exe));

    PanelWindow::Start(self);

    Resolver::Report report;
    for (int attempt = 0;; ++attempt) {
        if (Resolver::Run(report)) break;
        if (attempt == 0) Session::SetStartupMessage(L"Buscando las estructuras internas del juego…", false);
        if (attempt == 15 || attempt == 60 || attempt % 300 == 299) {
            Log::Warn("Inicio", "Sigue sin localizarse: " + report.problem);
            if (attempt == 15 && !report.detail.empty()) Log::Warn("Inicio", "Detalle: " + report.detail);
            Session::SetStartupMessage(L"Buscando las estructuras internas del juego… (" + Paths::Wide(report.problem) +
                                           L"). Si sigue así, copia el diagnóstico desde el iniciador.",
                                       false);
        }
        if (attempt == 600) {
            Log::Error("Inicio", "No se localizaron las estructuras del motor tras 10 minutos");
            Session::SetStartupMessage(L"No se pudieron localizar las estructuras del juego. Esta versión de Grounded 2 no es compatible "
                                       L"todavía; no se ha modificado nada. Envía el registro (Ajustes › Abrir registro).",
                                       false, true);
            return 1;
        }
        Sleep(1000);
    }
    Session::SetEngineReport(report);

    if (!EngineLayoutOk()) {
        Log::Error("Inicio", "El motor del juego cambió: el panel se queda en pausa para proteger la partida");
        Session::SetStartupMessage(L"Esta versión de Grounded 2 cambió estructuras básicas del motor Unreal. El panel queda en pausa "
                                   L"para proteger tu partida; no se ha modificado nada. Busca una actualización del panel.",
                                   true, true);
        return 1;
    }
    Session::SetStartupMessage(L"Listo. Carga una partida en solitario o como anfitrión.", true);
    Log::Info("Inicio", "Estructuras del motor verificadas; esperando una partida");

    auto* controllerClass = Reflect::FindClass("SurvivalPlayerController");
    bool idleReported = false;
    std::string lastSeen;
    bool hudHooked = false;
    uint64_t hookedAt = 0;
    for (;;) {
        const bool ticking = Hooks::TickInstalled() && GetTickCount64() - Session::LastTickMs() < 4000;
        if (!ticking) {
            if (!controllerClass) controllerClass = Reflect::FindClass("SurvivalPlayerController");
            auto* controller = LocalController();
            if (controller && controllerClass && controller->IsA(controllerClass)) {
                if (Hooks::InstallTick(controller) && !hookedAt) hookedAt = GetTickCount64();
                // Hooked but silent: the controller's class may not run script events every frame.
                // The HUD draws (ReceiveDrawHUD) every frame, so hook it as well.
                if (hookedAt && GetTickCount64() - hookedAt > 8000 && GetTickCount64() - Session::LastTickMs() > 8000) {
                    auto* hud = Reflect::Obj<SDK::UObject>(controller, "MyHUD");
                    if (LiveObject(hud) && Hooks::InstallTick(hud) && !hudHooked) {
                        Log::Warn("Inicio", "El controlador no genera actividad; se usa también la HUD (" + hud->Class->GetName() + ")");
                        hudHooked = true;
                    }
                }
            } else {
                // Log what we see (once per situation) so a stuck "waiting" state can be diagnosed.
                std::string seen = !controllerClass ? "no existe la clase SurvivalPlayerController"
                                   : controller     ? "controlador local de clase " + controller->Class->GetName()
                                                    : "sin controlador local (menú o pantalla de carga)";
                if (seen != lastSeen) {
                    Log::Info("Inicio", "Esperando partida: " + seen);
                    lastSeen = seen;
                }
            }
            if (Hooks::TickInstalled() && !idleReported) {
                Session::MarkIdle();
                idleReported = true;
            }
        } else {
            idleReported = false;
        }
        Sleep(1000);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // One panel per game process, even if another copy of the DLL is injected.
        std::wstring name = L"Local\\Grounded2PanelES_" + std::to_wstring(GetCurrentProcessId());
        instanceMutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (instanceMutex) CloseHandle(instanceMutex);
            return FALSE;
        }
        self = module;
        // Pin the module: hooks point into it for the rest of the game session.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        HANDLE thread = CreateThread(nullptr, 0, Startup, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
