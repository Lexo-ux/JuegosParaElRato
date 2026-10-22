// SPDX-License-Identifier: GPL-3.0-or-later
// Based on the ProcessEvent and native hooks of Grounded2Minimal (x0reaxeax), reworked so that
// member offsets and enum values come from the running game instead of the SDK dump.
#include "Hooks.hpp"

#include "Session.hpp"
#include "core/Log.hpp"
#include "core/Reflect.hpp"

#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/Maine_structs.hpp"

#include <windows.h>

#include <array>
#include <mutex>

namespace Hooks {

std::atomic<bool> buildAnywhere{false};

namespace {

using ProcessEventFn = void (*)(SDK::UObject*, SDK::UFunction*, void*);
using NativeFn = void (*)(SDK::UObject*, void*, void*);

struct VTableHook {
    void** vtable = nullptr;
    ProcessEventFn original = nullptr;
};
constexpr int kMaxVTables = 8;
std::array<VTableHook, kMaxVTables> vtableHooks{};
std::atomic<int> vtableCount{0};
std::mutex installMutex;
thread_local bool insideTick = false;

void HookedProcessEvent(SDK::UObject* object, SDK::UFunction* function, void* params) {
    void** vtable = *reinterpret_cast<void***>(object);
    ProcessEventFn original = nullptr;
    const int count = vtableCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        if (vtableHooks[i].vtable == vtable) { original = vtableHooks[i].original; break; }
    if (!insideTick) {
        insideTick = true;
        Session::Tick();
        insideTick = false;
    }
    if (original) original(object, function, params);
}

bool PatchPointer(void** slot, void* value, void** previous) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    if (previous) *previous = *slot;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    return true;
}

// ------------------------------------------------------------------- build anywhere

struct BuildingLayout {
    int32_t surfaceOffset = -1, stateOffset = -1;
    uint8_t surfaceNone = 0, surfaceInvalid = 0, surfaceDefault = 0;
    uint8_t statePlacedInvalid = 0, statePlaced = 0;
    SDK::UClass* buildingClass = nullptr;
} building;

std::string problem = "todavía no se han instalado los ganchos de colocación";
bool ready = false;

constexpr int kMaxNative = 4;
std::array<NativeFn, kMaxNative> originalCollision{};
std::array<NativeFn, kMaxNative> originalPlacement{};

template <int N>
void HookedCollision(SDK::UObject* object, void* stack, void* result) {
    if (buildAnywhere.load(std::memory_order_relaxed) && object && building.buildingClass && object->IsA(building.buildingClass)) {
        auto* bytes = reinterpret_cast<uint8_t*>(object);
        uint8_t& surface = bytes[building.surfaceOffset];
        if (surface == building.surfaceNone || surface == building.surfaceInvalid) surface = building.surfaceDefault;
        uint8_t& state = bytes[building.stateOffset];
        if (state == building.statePlacedInvalid) state = building.statePlaced;
    }
    originalCollision[N](object, stack, result);
}

template <int N>
void HookedPlacement(SDK::UObject* object, void* stack, void* result) {
    // Always run the original so the script stack is consumed, then override the answer.
    originalPlacement[N](object, stack, result);
    if (buildAnywhere.load(std::memory_order_relaxed) && result) *static_cast<uint8_t*>(result) = 1;
}

template <int N>
NativeFn CollisionThunk() { return &HookedCollision<N>; }
template <int N>
NativeFn PlacementThunk() { return &HookedPlacement<N>; }

const std::array<NativeFn, kMaxNative> kCollisionThunks = {CollisionThunk<0>(), CollisionThunk<1>(), CollisionThunk<2>(), CollisionThunk<3>()};
const std::array<NativeFn, kMaxNative> kPlacementThunks = {PlacementThunk<0>(), PlacementThunk<1>(), PlacementThunk<2>(), PlacementThunk<3>()};

std::vector<SDK::UFunction*> NativeFunctions(const char* name, const char* owner) {
    std::vector<SDK::UFunction*> result;
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count && result.size() < kMaxNative; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || !object->HasTypeFlag(SDK::EClassCastFlags::Function)) continue;
        if (object->GetName() != name) continue;
        if (owner && (!object->Outer || object->Outer->GetName() != owner)) continue;
        auto* function = static_cast<SDK::UFunction*>(object);
        if (!(function->FunctionFlags & 0x400) || !function->ExecFunction) continue;  // FUNC_Native
        result.push_back(function);
    }
    return result;
}

} // namespace

bool InstallTick(SDK::UObject* controller) {
    if (!controller) return false;
    std::lock_guard lock(installMutex);
    void** vtable = *reinterpret_cast<void***>(controller);
    const int count = vtableCount.load();
    for (int i = 0; i < count; ++i)
        if (vtableHooks[i].vtable == vtable) return true;
    if (count >= kMaxVTables) return false;
    const int index = SDK::Offsets::ProcessEventIdx;
    void* previous = nullptr;
    vtableHooks[count].vtable = vtable;
    vtableHooks[count].original = reinterpret_cast<ProcessEventFn>(vtable[index]);
    vtableCount.store(count + 1, std::memory_order_release);
    if (!PatchPointer(&vtable[index], reinterpret_cast<void*>(&HookedProcessEvent), &previous)) {
        vtableCount.store(count);
        Log::Error("Ganchos", "No se pudo modificar la vtable del controlador");
        return false;
    }
    Log::Info("Ganchos", "ProcessEvent enganchado en " + controller->Class->GetName());
    return true;
}

bool TickInstalled() { return vtableCount.load() > 0; }

void InstallBuildAnywhere() {
    if (ready) return;
    building.buildingClass = Reflect::FindClass("Building");
    auto* surface = Reflect::FindProperty(building.buildingClass, "AnchoredSurface");
    auto* state = Reflect::FindProperty(building.buildingClass, "BuildingState");
    if (!building.buildingClass || !surface || !state || surface->ElementSize != 1 || state->ElementSize != 1) {
        problem = "la clase de construcciones cambió (AnchoredSurface/BuildingState)";
        Log::Warn("Ganchos", "Construir sin restricciones no disponible: " + problem);
        return;
    }
    building.surfaceOffset = surface->Offset;
    building.stateOffset = state->Offset;
    using SurfaceType = SDK::EBuildingGridSurfaceType;
    using StateType = SDK::EBuildingState;
    building.surfaceNone = static_cast<uint8_t>(Reflect::EnumValue("EBuildingGridSurfaceType", "None", static_cast<int64_t>(SurfaceType::None)));
    building.surfaceInvalid = static_cast<uint8_t>(Reflect::EnumValue("EBuildingGridSurfaceType", "Invalid", static_cast<int64_t>(SurfaceType::Invalid)));
    building.surfaceDefault = static_cast<uint8_t>(Reflect::EnumValue("EBuildingGridSurfaceType", "Default", static_cast<int64_t>(SurfaceType::Default)));
    building.statePlacedInvalid = static_cast<uint8_t>(Reflect::EnumValue("EBuildingState", "BeingPlacedInvalid", static_cast<int64_t>(StateType::BeingPlacedInvalid)));
    building.statePlaced = static_cast<uint8_t>(Reflect::EnumValue("EBuildingState", "BeingPlaced", static_cast<int64_t>(StateType::BeingPlaced)));

    auto collision = NativeFunctions("UpdateCollisionStateChange", "Building");
    auto placement = NativeFunctions("GetPlacementValid", nullptr);
    if (collision.empty() || placement.empty()) {
        problem = "no se encontraron las comprobaciones nativas de colocación";
        Log::Warn("Ganchos", "Construir sin restricciones no disponible: " + problem);
        return;
    }
    for (size_t i = 0; i < collision.size(); ++i) {
        void* previous = nullptr;
        if (PatchPointer(reinterpret_cast<void**>(&collision[i]->ExecFunction), reinterpret_cast<void*>(kCollisionThunks[i]), &previous))
            originalCollision[i] = reinterpret_cast<NativeFn>(previous);
    }
    for (size_t i = 0; i < placement.size(); ++i) {
        void* previous = nullptr;
        if (PatchPointer(reinterpret_cast<void**>(&placement[i]->ExecFunction), reinterpret_cast<void*>(kPlacementThunks[i]), &previous))
            originalPlacement[i] = reinterpret_cast<NativeFn>(previous);
        Log::Info("Ganchos", "GetPlacementValid enganchado en " + (placement[i]->Outer ? placement[i]->Outer->GetName() : std::string("?")));
    }
    ready = true;
    problem.clear();
    Log::Info("Ganchos", "Construir sin restricciones listo");
}

bool BuildAnywhereReady() { return ready; }
std::string BuildAnywhereProblem() { return problem; }

} // namespace Hooks
