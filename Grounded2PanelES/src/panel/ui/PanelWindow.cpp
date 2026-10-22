// SPDX-License-Identifier: GPL-3.0-or-later
#include "PanelWindow.hpp"

#include "Pages.hpp"
#include "common/DxWindow.hpp"
#include "common/Theme.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <string>

namespace PanelWindow {
namespace {

std::atomic<bool> running{false};
HANDLE thread = nullptr;
HINSTANCE resources = nullptr;

bool ForegroundIsThisProcess() {
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

const char* KeyName(int vk) {
    switch (vk) {
    case 0x70: return "F1"; case 0x71: return "F2"; case 0x72: return "F3"; case 0x73: return "F4";
    case 0x74: return "F5"; case 0x75: return "F6"; case 0x76: return "F7"; case 0x77: return "F8";
    case 0x78: return "F9"; case 0x79: return "F10"; case 0x7A: return "F11"; case 0x7B: return "F12";
    case 0x2D: return "Insert"; case 0x24: return "Inicio"; case 0x23: return "Fin";
    case 0x21: return "Re Pág"; case 0x22: return "Av Pág"; case 0x13: return "Pausa"; case 0x91: return "Bloq Despl";
    default: return "?";
    }
}

struct State {
    Bridge::Snapshot snapshot;
    uint64_t lastRevision = ~0ull;
    uint64_t lastMessage = 0;
    Settings::Data settings;
    bool settingsChanged = false;
    bool scaleChanged = false;
    double lastSave = 0;
    Pages::Page page = Pages::Page::Home;
};

void Header(DxWindow& window, State& state, float height) {
    const auto& fonts = Theme::GetFonts();
    const auto& s = state.snapshot;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), Theme::Color::Sidebar);
    draw->AddLine(ImVec2(origin.x, origin.y + height - 1), ImVec2(origin.x + width, origin.y + height - 1), Theme::Color::Border);

    // Logo: seedling in a rounded green square.
    const float logo = height * 0.62f;
    const ImVec2 logoPos(origin.x + 18, origin.y + (height - logo) * 0.5f);
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logo, logoPos.y + logo), Theme::Color::AccentDark, logo * 0.28f);
    draw->AddRectFilled(logoPos, ImVec2(logoPos.x + logo, logoPos.y + logo * 0.62f), Theme::Color::Accent, logo * 0.28f,
                        ImDrawFlags_RoundCornersTop);
    ImFont* iconFont = fonts.icons;
    const ImVec2 iconSize = iconFont->CalcTextSizeA(iconFont->FontSize, FLT_MAX, 0, Icon::Seedling);
    draw->AddText(iconFont, iconFont->FontSize, ImVec2(logoPos.x + (logo - iconSize.x) * 0.5f, logoPos.y + (logo - iconSize.y) * 0.5f),
                  IM_COL32(16, 26, 18, 255), Icon::Seedling);

    // Title
    ImGui::PushFont(fonts.title);
    const float titleX = logoPos.x + logo + 14;
    draw->AddText(fonts.title, fonts.title->FontSize, ImVec2(titleX, origin.y + (height - fonts.title->FontSize) * 0.5f - 1),
                  Theme::Color::Text, "Grounded 2");
    const float titleWidth = ImGui::CalcTextSize("Grounded 2").x;
    ImGui::PopFont();
    draw->AddText(fonts.body, fonts.body->FontSize,
                  ImVec2(titleX + titleWidth + 10, origin.y + (height - fonts.body->FontSize) * 0.5f + 1), Theme::Color::TextDim,
                  "Panel en español");

    // Status pill
    std::string status;
    ImU32 statusColor;
    if (s.ready) {
        status = std::string(Icon::CircleCheck) + "  " + Pages::Utf8(s.player.empty() ? L"Conectado" : s.player);
        statusColor = Theme::Color::Accent;
    } else if (s.resolved) {
        status = std::string(Icon::Hourglass) + "  Esperando partida";
        statusColor = Theme::Color::Amber;
    } else {
        status = std::string(Icon::Search) + "  Localizando el juego";
        statusColor = Theme::Color::Blue;
    }
    ImGui::PushFont(fonts.small);
    const ImVec2 pillText = ImGui::CalcTextSize(status.c_str());
    const std::string version = "v" G2P_VERSION;
    const ImVec2 versionText = ImGui::CalcTextSize(version.c_str());
    ImGui::PopFont();

    const float button = height * 0.62f;
    const float buttonsWidth = button * 3 + 12;
    const float pillWidth = pillText.x + 22;
    const ImVec2 pillPos(origin.x + width - buttonsWidth - 24 - pillWidth, origin.y + (height - pillText.y - 8) * 0.5f);
    draw->AddRectFilled(pillPos, ImVec2(pillPos.x + pillWidth, pillPos.y + pillText.y + 8), Theme::WithAlpha(statusColor, 0.16f), 20);
    draw->AddText(fonts.small, fonts.small->FontSize, ImVec2(pillPos.x + 11, pillPos.y + 4), statusColor, status.c_str());
    if (!s.gameVersion.empty()) {
        std::string game = "Grounded " + s.gameVersion;
        const ImVec2 gameSize = fonts.small->CalcTextSizeA(fonts.small->FontSize, FLT_MAX, 0, game.c_str());
        draw->AddText(fonts.small, fonts.small->FontSize, ImVec2(pillPos.x - gameSize.x - 14, pillPos.y + 4), Theme::Color::TextDim, game.c_str());
    }
    draw->AddText(fonts.small, fonts.small->FontSize,
                  ImVec2(titleX + titleWidth + 10 + ImGui::CalcTextSize("Panel en español").x + 10, origin.y + (height - versionText.y) * 0.5f + 2),
                  Theme::WithAlpha(Theme::Color::TextDim, 0.7f), version.c_str());

    // Window buttons
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - buttonsWidth - 12, origin.y + (height - button) * 0.5f));
    const bool pinned = state.settings.alwaysOnTop;
    if (Theme::IconButton(pinned ? Icon::Lock : Icon::Unlock, "pin", pinned ? "Dejar de mantener encima" : "Mantener siempre encima",
                          pinned ? Theme::Color::Accent : Theme::Color::TextDim, button)) {
        state.settings.alwaysOnTop = !pinned;
        state.settingsChanged = true;
        window.SetTopmost(state.settings.alwaysOnTop);
    }
    ImGui::SameLine(0, 6);
    if (Theme::IconButton(Icon::Minus, "min", "Minimizar", Theme::Color::TextDim, button)) window.Minimize();
    ImGui::SameLine(0, 6);
    std::string hide = std::string("Ocultar (") + KeyName(state.settings.hotkey) + " para volver)";
    if (Theme::IconButton(Icon::Close, "close", hide.c_str(), Theme::Color::Red, button)) window.SetVisible(false);
}

void Sidebar(State& state, float width) {
    const auto& fonts = Theme::GetFonts();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec(Theme::Color::Sidebar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
    ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    auto section = [&](const char* label) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushFont(fonts.small);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
        Theme::TextDim(label);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
    };
    using P = Pages::Page;
    auto item = [&](P page) {
        if (Theme::SidebarItem(Pages::IconOf(page), Pages::LabelOf(page), state.page == page)) {
            state.page = page;
            state.settings.page = static_cast<int>(page);
            state.settingsChanged = true;
        }
    };
    item(P::Home);
    section("JUEGO");
    for (P page : {P::Items, P::Player, P::Damage, P::Mutations, P::Build, P::Pickup}) item(page);
    section("MUNDO");
    for (P page : {P::World, P::Teleport, P::Creatures, P::Progress}) item(page);
    section("PANEL");
    item(P::Options);
    item(P::Log);

    // Hotkey hint at the bottom.
    const float hintHeight = ImGui::GetFrameHeight() * 1.6f;
    const float y = ImGui::GetWindowHeight() - hintHeight - 14;
    if (ImGui::GetCursorPosY() < y) {
        ImGui::SetCursorPosY(y);
        ImGui::PushFont(fonts.small);
        std::string hint = std::string(Icon::Keyboard) + "  " + KeyName(state.settings.hotkey) + " muestra u oculta";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
        Theme::TextDim(hint.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
        Theme::TextDim("Haz copia de tus partidas");
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void Frame(DxWindow& window, State& state) {
    if (Bridge::Revision() != state.lastRevision) {
        state.snapshot = Bridge::Read();
        state.lastRevision = state.snapshot.revision;
        if (state.snapshot.messageId != state.lastMessage && !state.snapshot.message.empty()) {
            state.lastMessage = state.snapshot.messageId;
            Theme::ToastKind kind = Theme::ToastKind::Info;
            switch (state.snapshot.tone) {
            case Bridge::Tone::Success: kind = Theme::ToastKind::Success; break;
            case Bridge::Tone::Warning: kind = Theme::ToastKind::Warning; break;
            case Bridge::Tone::Error: kind = Theme::ToastKind::Error; break;
            default: break;
            }
            Theme::PushToast(Pages::Utf8(state.snapshot.message), kind);
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    const float headerHeight = ImGui::GetFrameHeight() * 2.0f;
    window.SetHeaderHeight(headerHeight);
    Header(window, state, headerHeight);
    const bool headerHovered = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                               ImGui::GetMousePos().y - ImGui::GetWindowPos().y < headerHeight;

    ImGui::SetCursorPos(ImVec2(0, headerHeight));
    Sidebar(state, ImGui::GetFontSize() * 13.5f);
    ImGui::SameLine(0, 0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec(Theme::Color::Background));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    Theme::SectionTitle(Pages::IconOf(state.page), Pages::LabelOf(state.page));
    Pages::Context context{state.snapshot, state.settings, state.settingsChanged, state.scaleChanged, window, state.page};
    Pages::Draw(context);
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::End();

    Theme::DrawToasts();
    window.SetCaptionDraggable(headerHovered);
}

void ApplyFonts(DxWindow& window, State& state) {
    const float scale = window.Scale() * state.settings.uiScale;
    Theme::Load(resources, scale);
    Theme::Apply(scale);
    window.InvalidateFonts();
}

} // namespace

int Run(HINSTANCE module, bool preview, int startPage) {
    resources = module;
    State state;
    state.settings = Settings::Load();
    if (startPage >= 0) state.settings.page = startPage;
    state.page = static_cast<Pages::Page>(std::clamp(state.settings.page, 0, static_cast<int>(Pages::Page::Count) - 1));

    DxWindow window;
    DxWindow::Options options;
    options.className = kClassName;
    options.title = preview ? L"Grounded 2 · Panel en español (vista previa)" : L"Grounded 2 · Panel en español";
    options.instance = module;
    options.topmost = state.settings.alwaysOnTop;
    if (!window.Create(options)) {
        Log::Error("Ventana", "No se pudo crear la ventana del panel (Direct3D 11)");
        MessageBoxW(nullptr, L"No se pudo crear la ventana del panel (Direct3D 11). Consulta el registro.",
                    L"Grounded 2 · Panel en español", MB_OK | MB_ICONERROR);
        return 1;
    }
    window.SetOpacity(state.settings.opacity);
    ApplyFonts(window, state);
    Log::Info("Ventana", "Panel listo. Tecla para mostrar u ocultar: " + std::string(KeyName(state.settings.hotkey)));

    bool keyWasDown = true;   // ignore a key held while the panel starts
    running = true;
    while (running) {
        if (!window.Pump()) break;
        if (window.TakeCloseRequest()) {
            if (preview) break;
            window.SetVisible(false);
        }
        const bool keyDown = (GetAsyncKeyState(state.settings.hotkey) & 0x8000) != 0;
        if (keyDown && !keyWasDown && ForegroundIsThisProcess()) window.SetVisible(!window.Visible());
        keyWasDown = keyDown;

        if (window.ScaleChanged() || state.scaleChanged) {
            state.scaleChanged = false;
            ApplyFonts(window, state);
        }
        if (!window.BeginFrame()) {
            Sleep(40);
        } else {
            Frame(window, state);
            window.EndFrame();
        }
        const double now = GetTickCount64() / 1000.0;
        if (state.settingsChanged && now - state.lastSave > 1.0) {
            Settings::Save(state.settings);
            state.settingsChanged = false;
            state.lastSave = now;
        }
    }
    if (state.settingsChanged) Settings::Save(state.settings);
    window.Destroy();
    running = false;
    return 0;
}

bool Start(HINSTANCE module) {
    if (thread) return true;
    thread = CreateThread(nullptr, 0, [](LPVOID parameter) -> DWORD {
        return static_cast<DWORD>(Run(static_cast<HINSTANCE>(parameter), false));
    }, module, 0, nullptr);
    return thread != nullptr;
}

void Stop() {
    running = false;
    if (thread) {
        WaitForSingleObject(thread, 3000);
        CloseHandle(thread);
        thread = nullptr;
    }
}

} // namespace PanelWindow
