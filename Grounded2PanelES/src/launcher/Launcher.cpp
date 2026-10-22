// SPDX-License-Identifier: GPL-3.0-or-later
// IniciarPanel.exe — opens Grounded 2 through Steam, loads the panel when the game is ready,
// and backs up saves. Arguments: --auto (start/load and close), --cargar (load only).
#include "Launcher.hpp"

#include "common/DxWindow.hpp"
#include "common/Paths.hpp"
#include "common/Theme.hpp"

#include <shellapi.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace C = Theme::Color;

namespace {

struct Options {
    bool autoBackup = true;
    bool closeAfterLoad = false;
};

Options LoadOptions() {
    Options o;
    std::ifstream file(std::filesystem::path(Paths::DataDir() + L"\\iniciador.ini"));
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with("autoBackup=")) o.autoBackup = line.back() == '1';
        if (line.starts_with("closeAfterLoad=")) o.closeAfterLoad = line.back() == '1';
    }
    return o;
}

void SaveOptions(const Options& o) {
    std::ofstream file(std::filesystem::path(Paths::DataDir() + L"\\iniciador.ini"), std::ios::trunc);
    file << "autoBackup=" << (o.autoBackup ? 1 : 0) << "\ncloseAfterLoad=" << (o.closeAfterLoad ? 1 : 0) << "\n";
}

enum class Job { None, Start, Load, Backup };

struct Shared {
    std::mutex mutex;
    Inject::Game game;
    Steam::Install install;
    std::deque<std::pair<Theme::ToastKind, std::string>> log;
    std::string phase;          // what the worker is doing now
    std::atomic<bool> busy{false};
    std::atomic<bool> quit{false};
    std::atomic<bool> loadedThisRun{false};
    int backups = 0;
} shared;

std::wstring dllPath;
Options options;

void Note(Theme::ToastKind kind, const std::wstring& text) {
    std::lock_guard lock(shared.mutex);
    shared.log.emplace_back(kind, Paths::Utf8(text));
    while (shared.log.size() > 60) shared.log.pop_front();
}

void Phase(const std::string& text) {
    std::lock_guard lock(shared.mutex);
    shared.phase = text;
}

bool LoadPanel(const Inject::Game& game) {
    if (game.panelLoaded) {
        Note(Theme::ToastKind::Info, L"El panel ya estaba cargado: se muestra su ventana.");
        Inject::ShowPanel();
        return true;
    }
    if (game.otherModLoaded) {
        Note(Theme::ToastKind::Warning, L"Grounded2Minimal ya está cargado en el juego. Cierra el juego y ábrelo de nuevo para usar este panel.");
        return false;
    }
    Phase("Cargando el panel en el juego…");
    const std::wstring error = Inject::Load(game.pid, dllPath);
    if (!error.empty()) {
        Note(Theme::ToastKind::Error, error);
        return false;
    }
    Note(Theme::ToastKind::Success, L"Panel cargado. Aparecerá en unos segundos; pulsa F8 dentro del juego para mostrarlo u ocultarlo.");
    shared.loadedThisRun = true;
    return true;
}

void Worker(Job job) {
    shared.busy = true;
    if (job == Job::Backup || (job == Job::Start && options.autoBackup)) {
        Phase("Copiando tus partidas…");
        std::wstring message;
        const bool ok = !Backup::Run(message).empty();
        Note(ok ? Theme::ToastKind::Success : Theme::ToastKind::Warning, message);
        std::lock_guard lock(shared.mutex);
        shared.backups = Backup::Count();
    }
    if (job == Job::Start || job == Job::Load) {
        Inject::Game game = Inject::Find();
        if (!game.pid && job == Job::Start) {
            Phase("Abriendo Grounded 2 desde Steam…");
            if (!Steam::Launch()) Note(Theme::ToastKind::Error, L"No se pudo abrir Steam. Abre Grounded 2 manualmente y vuelve a pulsar el botón.");
            else Note(Theme::ToastKind::Info, L"Steam está abriendo Grounded 2…");
        }
        if (!game.pid && job == Job::Load) {
            Note(Theme::ToastKind::Warning, L"Grounded 2 no está abierto.");
        } else {
            Phase("Esperando a que el juego termine de abrirse…");
            const ULONGLONG deadline = GetTickCount64() + 5 * 60 * 1000;
            bool readyNoted = false;
            while (!shared.quit && GetTickCount64() < deadline) {
                game = Inject::Find();
                if (game.pid && game.windowReady) {
                    if (!readyNoted) {
                        readyNoted = true;
                        // Give the engine a moment after the window appears.
                        for (int i = 0; i < 8 && !shared.quit; ++i) Sleep(1000);
                        continue;
                    }
                    LoadPanel(game);
                    break;
                }
                Sleep(1000);
            }
            if (!game.windowReady && !shared.quit) Note(Theme::ToastKind::Error, L"El juego no terminó de abrirse en 5 minutos.");
        }
    }
    Phase("");
    shared.busy = false;
}

void StartJob(Job job) {
    if (shared.busy.exchange(true)) return;
    std::thread(Worker, job).detach();
}

void Poller() {
    while (!shared.quit) {
        Inject::Game game = Inject::Find();
        {
            std::lock_guard lock(shared.mutex);
            shared.game = game;
        }
        Sleep(1000);
    }
}

void Header(DxWindow& window, float height) {
    const auto& fonts = Theme::GetFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), C::Sidebar);
    draw->AddLine(ImVec2(origin.x, origin.y + height - 1), ImVec2(origin.x + width, origin.y + height - 1), C::Border);
    const float logo = height * 0.62f;
    const ImVec2 logoPos(origin.x + 18, origin.y + (height - logo) * 0.5f);
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logo, logoPos.y + logo), C::AccentDark, logo * 0.28f);
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logo, logoPos.y + logo * 0.62f), C::Accent, logo * 0.28f, ImDrawFlags_RoundCornersTop);
    const ImVec2 iconSize = fonts.icons->CalcTextSizeA(fonts.icons->FontSize, FLT_MAX, 0, Icon::Seedling);
    draw->AddText(fonts.icons, fonts.icons->FontSize, ImVec2(logoPos.x + (logo - iconSize.x) * 0.5f, logoPos.y + (logo - iconSize.y) * 0.5f),
                  IM_COL32(16, 26, 18, 255), Icon::Seedling);
    const float titleX = logoPos.x + logo + 14;
    draw->AddText(fonts.title, fonts.title->FontSize, ImVec2(titleX, origin.y + (height - fonts.title->FontSize) * 0.5f - 1), C::Text,
                  "Grounded 2");
    const float titleWidth = fonts.title->CalcTextSizeA(fonts.title->FontSize, FLT_MAX, 0, "Grounded 2").x;
    draw->AddText(fonts.body, fonts.body->FontSize, ImVec2(titleX + titleWidth + 10, origin.y + (height - fonts.body->FontSize) * 0.5f + 1),
                  C::TextDim, "Iniciar el panel en español  ·  v" G2P_VERSION);
    const float button = height * 0.62f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - button * 2 - 18, origin.y + (height - button) * 0.5f));
    if (Theme::IconButton(Icon::Minus, "min", "Minimizar", C::TextDim, button)) window.Minimize();
    ImGui::SameLine(0, 6);
    if (Theme::IconButton(Icon::Close, "close", "Cerrar", C::Red, button)) shared.quit = true;
}

void Frame(DxWindow& window) {
    const auto& fonts = Theme::GetFonts();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    const float headerHeight = ImGui::GetFrameHeight() * 2.0f;
    window.SetHeaderHeight(headerHeight);
    Header(window, headerHeight);
    const bool headerHovered = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                               ImGui::GetMousePos().y - ImGui::GetWindowPos().y < headerHeight;

    Inject::Game game;
    Steam::Install install;
    std::deque<std::pair<Theme::ToastKind, std::string>> log;
    std::string phase;
    int backups;
    {
        std::lock_guard lock(shared.mutex);
        game = shared.game;
        install = shared.install;
        log = shared.log;
        phase = shared.phase;
        backups = shared.backups;
    }

    ImGui::SetCursorPos(ImVec2(0, headerHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec(C::Background));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

    // ---- status
    Theme::BeginCard("##status", Icon::Gamepad, "Estado", C::Accent);
    auto status = [&](const char* icon, ImU32 color, const char* title, const std::string& value, ImU32 valueColor) {
        const float w = ImGui::GetContentRegionAvail().x * 0.55f;
        Theme::BeginRow(icon, color, title, nullptr, w, true);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec(valueColor));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        Theme::EndRow();
    };
    status(Icon::Gamepad, C::Accent, "Grounded 2", game.pid ? "Abierto (" + Paths::Utf8(game.exeName) + ")" : "No está abierto",
           game.pid ? C::Accent : C::TextDim);
    status(Icon::Seedling, C::Accent, "Panel", game.panelLoaded ? "Cargado en el juego" : (game.pid ? "Sin cargar" : "—"),
           game.panelLoaded ? C::Accent : C::TextDim);
    status(Icon::FolderOpen, C::Blue, "Instalación",
           install.gameDir.empty() ? "No encontrada (no es necesaria para cargar el panel)" : Paths::Utf8(install.gameDir), C::TextDim);
    if (!install.version.empty()) status(Icon::Info, C::Blue, "Versión del ejecutable", Paths::Utf8(install.version), C::TextDim);
    Theme::EndCard();

    // ---- main action
    std::string label;
    Job job = Job::None;
    if (!game.pid) {
        label = std::string(Icon::Play) + "   Abrir Grounded 2 con el panel";
        job = Job::Start;
    } else if (!game.panelLoaded) {
        label = std::string(Icon::Plug) + "   Cargar el panel en el juego";
        job = Job::Load;
    } else {
        label = std::string(Icon::Eye) + "   Mostrar el panel";
    }
    const bool busy = shared.busy;
    ImGui::PushFont(fonts.title);
    if (busy) label = std::string(Icon::Hourglass) + "   " + (phase.empty() ? "Trabajando…" : phase);
    if (Theme::Button(label.c_str(), ImVec2(-1, ImGui::GetFrameHeight() * 1.6f), C::Accent, !busy)) {
        if (job == Job::None) {
            if (!Inject::ShowPanel()) Note(Theme::ToastKind::Info, L"Pulsa F8 dentro del juego para mostrar el panel.");
        } else {
            StartJob(job);
        }
    }
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));

    Theme::BeginCard("##options", Icon::Gear, "Opciones", C::Amber);
    Theme::BeginRow(Icon::Archive, C::Amber, "Copia de seguridad antes de abrir el juego",
                    "Guarda una copia de «Partidas guardadas\\Grounded2» cada vez que abres el juego desde aquí.", ImGui::GetFrameHeight() * 1.7f, true);
    if (Theme::Toggle("##autobackup", &options.autoBackup)) SaveOptions(options);
    Theme::EndRow();
    Theme::BeginRow(Icon::Close, C::Amber, "Cerrar el iniciador al cargar el panel", nullptr, ImGui::GetFrameHeight() * 1.7f, true);
    if (Theme::Toggle("##close", &options.closeAfterLoad)) SaveOptions(options);
    Theme::EndRow();
    Theme::EndCard();

    Theme::BeginCard("##backups", Icon::Archive, "Copias de seguridad de partidas", C::Blue);
    char count[96];
    std::snprintf(count, sizeof(count), "%d copias guardadas (se conservan las 15 más recientes).", backups);
    Theme::TextDim(count);
    if (Theme::Button((std::string(Icon::Save) + "  Hacer una copia ahora").c_str(), ImVec2(0, 0), C::Blue, !busy)) StartJob(Job::Backup);
    ImGui::SameLine(0, 10);
    if (Theme::GhostButton((std::string(Icon::FolderOpen) + "  Abrir carpeta de copias").c_str())) {
        const std::wstring folder = Backup::Folder();
        CreateDirectoryW(folder.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine(0, 10);
    if (Theme::GhostButton((std::string(Icon::FileLines) + "  Registro del panel").c_str())) {
        const std::wstring file = Paths::DataDir() + L"\\registro.txt";
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    Theme::EndCard();

    Theme::BeginCard("##activity", Icon::List, "Actividad", C::TextDim);
    if (log.empty()) Theme::TextWrappedDim("Aquí verás lo que va haciendo el iniciador.");
    ImGui::PushFont(fonts.small);
    for (auto it = log.rbegin(); it != log.rend() && it - log.rbegin() < 6; ++it) {
        ImU32 color = it->first == Theme::ToastKind::Error     ? C::Red
                      : it->first == Theme::ToastKind::Warning ? C::Amber
                      : it->first == Theme::ToastKind::Success ? C::Accent
                                                               : C::Text;
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec(color));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(it->second.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    Theme::EndCard();

    Theme::TextWrappedDim("Consejo para Vortex: en el panel principal añade IniciarPanel.exe como herramienta (Añadir herramienta) "
                          "con el argumento --auto. Así abrirás el juego con el panel en un solo clic.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
    Theme::DrawToasts();
    window.SetCaptionDraggable(headerHovered);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Only one launcher at a time.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\Grounded2PanelES_Iniciador");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"Grounded2PanelES_Iniciador", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    bool autoMode = false, loadOnly = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--auto") autoMode = true;
        else if (arg == L"--cargar") loadOnly = true;
    }
    if (argv) LocalFree(argv);

    dllPath = Paths::ModuleDir(reinterpret_cast<const void*>(&wWinMain)) + L"Grounded2PanelES.dll";
    options = LoadOptions();
    {
        std::lock_guard lock(shared.mutex);
        shared.install = Steam::Detect(Paths::ModuleDir(reinterpret_cast<const void*>(&wWinMain)));
        shared.backups = Backup::Count();
    }
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        Note(Theme::ToastKind::Error, L"Falta Grounded2PanelES.dll junto al iniciador. Reinstala el mod desde Vortex o extrae el paquete completo.");

    DxWindow window;
    DxWindow::Options o;
    o.className = L"Grounded2PanelES_Iniciador";
    o.title = L"Grounded 2 · Iniciar el panel";
    o.instance = instance;
    o.width = 780;
    o.height = 720;
    o.minWidth = 640;
    o.minHeight = 520;
    if (!window.Create(o)) {
        MessageBoxW(nullptr, L"No se pudo crear la ventana (Direct3D 11).", L"Grounded 2 · Iniciar el panel", MB_ICONERROR);
        return 1;
    }
    Theme::Load(instance, window.Scale());
    Theme::Apply(window.Scale());
    window.InvalidateFonts();

    std::thread(Poller).detach();
    if (autoMode) StartJob(Job::Start);
    else if (loadOnly) StartJob(Job::Load);

    while (!shared.quit) {
        if (!window.Pump()) break;
        if (window.TakeCloseRequest()) break;
        if (window.ScaleChanged()) {
            Theme::Load(instance, window.Scale());
            Theme::Apply(window.Scale());
            window.InvalidateFonts();
        }
        if (shared.loadedThisRun && (options.closeAfterLoad || autoMode) && !shared.busy) {
            Sleep(1500);
            break;
        }
        if (!window.BeginFrame()) {
            Sleep(50);
            continue;
        }
        Frame(window);
        window.EndFrame();
    }
    shared.quit = true;
    window.Destroy();
    if (single) CloseHandle(single);
    return 0;
}
