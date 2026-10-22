// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the panel into the local user's Grounded 2 process (LoadLibraryW in a remote thread).
// Based on the loader of Panel 0.2.0; now accepts every Grounded 2 shipping executable name.
#include "Launcher.hpp"

#include <tlhelp32.h>

namespace Inject {
namespace {

bool IsGameExe(const wchar_t* name) {
    std::wstring exe(name);
    return (exe.starts_with(L"Grounded2") || exe.starts_with(L"grounded2")) && exe.ends_with(L"-Shipping.exe");
}

uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &module)) do {
            if (_wcsicmp(module.szModule, name) == 0) {
                base = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    return base;
}

struct WindowSearch {
    DWORD pid;
    bool found;
};

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid || !IsWindowVisible(hwnd)) return TRUE;
    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, 64);
    if (std::wstring(className) == L"UnrealWindow") {
        search->found = true;
        return FALSE;
    }
    return TRUE;
}

} // namespace

Game Find() {
    Game game;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return game;
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) do {
            if (IsGameExe(process.szExeFile)) {
                game.pid = process.th32ProcessID;
                game.exeName = process.szExeFile;
                break;
            }
        } while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    if (!game.pid) return game;
    game.panelLoaded = ModuleBase(game.pid, L"Grounded2PanelES.dll") != 0;
    game.otherModLoaded = ModuleBase(game.pid, L"Grounded2Minimal.dll") != 0;
    WindowSearch search{game.pid, false};
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
    game.windowReady = search.found;
    return game;
}

bool ShowPanel() {
    HWND panel = FindWindowW(L"Grounded2PanelES", nullptr);
    if (!panel) return false;
    ShowWindow(panel, IsIconic(panel) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(panel);
    return true;
}

std::wstring Load(DWORD pid, const std::wstring& dll) {
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
        return L"No se encuentra Grounded2PanelES.dll junto al iniciador. Reinstala el mod o extrae el paquete completo.";
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) return L"Windows no permitió conectar con el juego. Ejecuta el juego y el iniciador con los mismos permisos (sin «Ejecutar como administrador» en uno solo).";
    // Locate the real owner of the (possibly forwarded) LoadLibraryW export in both processes.
    auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    MEMORY_BASIC_INFORMATION info{};
    if (!load || !VirtualQuery(reinterpret_cast<void*>(load), &info, sizeof(info))) {
        CloseHandle(process);
        return L"No se pudo localizar el cargador de Windows.";
    }
    std::wstring owner(32768, L'\0');
    owner.resize(GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), owner.data(), static_cast<DWORD>(owner.size())));
    const auto slash = owner.find_last_of(L"\\/");
    const auto remoteBase = ModuleBase(pid, owner.c_str() + (slash == std::wstring::npos ? 0 : slash + 1));
    if (!remoteBase) {
        CloseHandle(process);
        return L"No se pudo localizar kernel32 en el juego. ¿Sigue cargando? Inténtalo de nuevo en unos segundos.";
    }
    const auto address = remoteBase + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(info.AllocationBase);
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written = 0;
    if (!memory || !WriteProcessMemory(process, memory, dll.c_str(), bytes, &written) || written != bytes) {
        if (memory) VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return L"No se pudo preparar la carga del panel.";
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(address), memory, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return L"Windows no pudo iniciar el panel (¿antivirus?).";
    }
    const DWORD wait = WaitForSingleObject(thread, 15000);
    DWORD loaded = 0;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeThread(thread, &loaded);
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
    }
    // On timeout the remote thread still owns the path: do not free it.
    CloseHandle(thread);
    CloseHandle(process);
    if (wait != WAIT_OBJECT_0) return L"La carga sigue pendiente. Espera unos segundos antes de volver a intentarlo.";
    if (!loaded) return L"El juego rechazó la DLL. Comprueba que el paquete está completo y que tu antivirus no la bloquea.";
    return {};
}

} // namespace Inject
