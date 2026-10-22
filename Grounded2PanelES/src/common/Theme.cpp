// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hpp"

#include "imgui_internal.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>

namespace Theme {
namespace {

Fonts fonts;

// Latin + Spanish punctuation, dashes, quotes, ellipsis, arrows.
const ImWchar kTextRanges[] = {0x0020, 0x00FF, 0x0152, 0x0153, 0x2013, 0x2014, 0x2018, 0x201E, 0x2022, 0x2022,
                               0x2026, 0x2026, 0x2190, 0x2193, 0x2212, 0x2212, 0x00D7, 0x00D7, 0};
const ImWchar kIconRanges[] = {0xE000, 0xF8FF, 0};

struct Blob {
    const void* data = nullptr;
    int size = 0;
};

Blob Resource(HMODULE module, int id) {
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!info) return {};
    HGLOBAL handle = LoadResource(module, info);
    if (!handle) return {};
    return {LockResource(handle), static_cast<int>(SizeofResource(module, info))};
}

ImFont* AddFont(const Blob& blob, const char* fallbackFile, float px, const ImWchar* ranges, bool merge = false,
                float glyphOffsetY = 0, float minAdvance = 0) {
    ImFontConfig config;
    config.MergeMode = merge;
    config.PixelSnapH = true;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.GlyphOffset.y = glyphOffsetY;
    config.GlyphMinAdvanceX = minAdvance;
    auto& io = ImGui::GetIO();
    if (blob.data) {
        config.FontDataOwnedByAtlas = false;  // resource memory lives as long as the module
        return io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(blob.data), blob.size, px, &config, ranges);
    }
    if (fallbackFile) {
        char windows[MAX_PATH] = {};
        GetWindowsDirectoryA(windows, MAX_PATH);
        std::string path = std::string(windows) + "\\Fonts\\" + fallbackFile;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return io.Fonts->AddFontFromFileTTF(path.c_str(), px, &config, ranges);
    }
    if (merge) return nullptr;
    ImFontConfig basic;
    basic.SizePixels = px;
    return io.Fonts->AddFontDefault(&basic);
}

struct Toast {
    std::string text;
    ToastKind kind;
    double born;
};
std::mutex toastMutex;
std::deque<Toast> toasts;

ImU32 KindColor(ToastKind kind) {
    switch (kind) {
    case ToastKind::Success: return Color::Accent;
    case ToastKind::Warning: return Color::Amber;
    case ToastKind::Error: return Color::Red;
    default: return Color::Blue;
    }
}

const char* KindIcon(ToastKind kind) {
    switch (kind) {
    case ToastKind::Success: return Icon::CircleCheck;
    case ToastKind::Warning: return Icon::Warning;
    case ToastKind::Error: return Icon::CircleX;
    default: return Icon::Info;
    }
}

int disabledRows = 0;

} // namespace

ImVec4 ToVec(ImU32 c, float alphaScale) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= alphaScale;
    return v;
}

ImU32 WithAlpha(ImU32 c, float alpha) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 Lerp(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

const Fonts& GetFonts() { return fonts; }

void Load(HINSTANCE__* module, float scale) {
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    const HMODULE m = reinterpret_cast<HMODULE>(module);
    const Blob body = Resource(m, 101), bold = Resource(m, 102), title = Resource(m, 103), icons = Resource(m, 104);
    auto withIcons = [&](ImFont* base, float px) {
        if (base) AddFont(icons, "segmdl2.ttf", px * 0.92f, kIconRanges, true, px * 0.02f, px * 1.05f);
        return base;
    };
    fonts.body = withIcons(AddFont(body, "segoeui.ttf", 17.0f * scale, kTextRanges), 17.0f * scale);
    fonts.bold = withIcons(AddFont(bold, "seguisb.ttf", 17.0f * scale, kTextRanges), 17.0f * scale);
    fonts.small = withIcons(AddFont(body, "segoeui.ttf", 14.5f * scale, kTextRanges), 14.5f * scale);
    fonts.title = withIcons(AddFont(title, "seguisb.ttf", 25.0f * scale, kTextRanges), 25.0f * scale);
    fonts.huge = withIcons(AddFont(title, "seguisb.ttf", 36.0f * scale, kTextRanges), 36.0f * scale);
    fonts.icons = AddFont(icons, "segmdl2.ttf", 19.0f * scale, kIconRanges, false, 0, 26.0f * scale);
    io.FontDefault = fonts.body;
    io.Fonts->Build();
}

void Apply(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding = 0;
    s.ChildRounding = 14;
    s.FrameRounding = 9;
    s.PopupRounding = 12;
    s.GrabRounding = 9;
    s.TabRounding = 9;
    s.ScrollbarRounding = 9;
    s.WindowPadding = ImVec2(20, 18);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = ImVec2(10, 6);
    s.ScrollbarSize = 11;
    s.GrabMinSize = 16;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.SelectableTextAlign = ImVec2(0, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ToVec(Color::Text);
    c[ImGuiCol_TextDisabled] = ToVec(Color::TextDim);
    c[ImGuiCol_WindowBg] = ToVec(Color::Background);
    c[ImGuiCol_ChildBg] = ToVec(Color::Card);
    c[ImGuiCol_PopupBg] = ToVec(IM_COL32(24, 36, 29, 250));
    c[ImGuiCol_Border] = ToVec(Color::Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ToVec(IM_COL32(12, 18, 15, 255));
    c[ImGuiCol_FrameBgHovered] = ToVec(IM_COL32(20, 30, 24, 255));
    c[ImGuiCol_FrameBgActive] = ToVec(IM_COL32(26, 38, 31, 255));
    c[ImGuiCol_TitleBg] = ToVec(Color::Sidebar);
    c[ImGuiCol_TitleBgActive] = ToVec(Color::Sidebar);
    c[ImGuiCol_MenuBarBg] = ToVec(Color::Sidebar);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ToVec(IM_COL32(56, 74, 61, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = ToVec(IM_COL32(76, 99, 82, 255));
    c[ImGuiCol_ScrollbarGrabActive] = ToVec(Color::AccentDark);
    c[ImGuiCol_CheckMark] = ToVec(Color::Accent);
    c[ImGuiCol_SliderGrab] = ToVec(Color::Accent);
    c[ImGuiCol_SliderGrabActive] = ToVec(IM_COL32(170, 230, 120, 255));
    c[ImGuiCol_Button] = ToVec(IM_COL32(34, 50, 41, 255));
    c[ImGuiCol_ButtonHovered] = ToVec(IM_COL32(44, 64, 52, 255));
    c[ImGuiCol_ButtonActive] = ToVec(IM_COL32(54, 78, 63, 255));
    c[ImGuiCol_Header] = ToVec(IM_COL32(38, 58, 44, 255));
    c[ImGuiCol_HeaderHovered] = ToVec(IM_COL32(44, 66, 51, 255));
    c[ImGuiCol_HeaderActive] = ToVec(IM_COL32(52, 78, 60, 255));
    c[ImGuiCol_Separator] = ToVec(Color::Border);
    c[ImGuiCol_SeparatorHovered] = ToVec(Color::AccentDark);
    c[ImGuiCol_SeparatorActive] = ToVec(Color::Accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ToVec(Color::AccentDark);
    c[ImGuiCol_ResizeGripActive] = ToVec(Color::Accent);
    c[ImGuiCol_Tab] = ToVec(IM_COL32(24, 36, 29, 255));
    c[ImGuiCol_TabHovered] = ToVec(IM_COL32(44, 66, 51, 255));
    c[ImGuiCol_TabSelected] = ToVec(IM_COL32(40, 60, 46, 255));
    c[ImGuiCol_TableHeaderBg] = ToVec(IM_COL32(24, 36, 29, 255));
    c[ImGuiCol_TableBorderStrong] = ToVec(Color::Border);
    c[ImGuiCol_TableBorderLight] = ToVec(IM_COL32(30, 44, 36, 255));
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ToVec(IM_COL32(255, 255, 255, 6));
    c[ImGuiCol_TextSelectedBg] = ToVec(Color::AccentDark, 0.45f);
    c[ImGuiCol_NavCursor] = ToVec(Color::Accent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.04f, 0.03f, 0.65f);
    s.ScaleAllSizes(scale);
}

// ---------------------------------------------------------------- widgets

bool Toggle(const char* id, bool* value, bool enabled) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float height = ImGui::GetFrameHeight() * 0.9f;
    const float width = height * 1.85f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (clicked) *value = !*value;
    const bool hovered = enabled && ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGuiStorage* storage = window->DC.StateStorage;
    const ImGuiID key = ImGui::GetItemID();
    float t = storage->GetFloat(key, *value ? 1.0f : 0.0f);
    const float target = *value ? 1.0f : 0.0f;
    t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    if (std::fabs(target - t) < 0.001f) t = target;
    storage->SetFloat(key, t);

    ImDrawList* draw = window->DrawList;
    ImU32 off = hovered ? IM_COL32(64, 84, 70, 255) : IM_COL32(48, 64, 54, 255);
    ImU32 on = hovered ? IM_COL32(160, 226, 104, 255) : Color::Accent;
    ImU32 track = Lerp(off, on, t);
    if (!enabled) track = WithAlpha(track, 0.35f);
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), track, height * 0.5f);
    const float radius = height * 0.5f - 3.0f;
    const ImVec2 knob(pos.x + radius + 3.0f + t * (width - radius * 2 - 6.0f), pos.y + height * 0.5f);
    draw->AddCircleFilled(ImVec2(knob.x, knob.y + 1.2f), radius, IM_COL32(0, 0, 0, 70));
    draw->AddCircleFilled(knob, radius, enabled ? IM_COL32(250, 252, 248, 255) : IM_COL32(160, 170, 162, 255));
    return clicked;
}

bool Button(const char* label, ImVec2 size, ImU32 color, bool enabled) {
    const bool light = color == Color::Accent || color == Color::Amber;
    ImGui::PushStyleColor(ImGuiCol_Button, ToVec(color, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToVec(Lerp(color, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToVec(Lerp(color, IM_COL32(0, 0, 0, 255), 0.12f)));
    ImGui::PushStyleColor(ImGuiCol_Text, light ? ToVec(IM_COL32(16, 26, 18, 255)) : ToVec(IM_COL32(255, 255, 255, 255)));
    ImGui::PushFont(fonts.bold);
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = ImGui::Button(label, size);
    if (!enabled) ImGui::EndDisabled();
    if (enabled && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed && enabled;
}

bool GhostButton(const char* label, ImVec2 size, bool enabled) {
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = ImGui::Button(label, size);
    if (!enabled) ImGui::EndDisabled();
    if (enabled && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed && enabled;
}

bool IconButton(const char* icon, const char* id, const char* tooltip, ImU32 color, float size) {
    ImGui::PushID(id);
    const float side = size > 0 ? size : ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##icon", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered) {
        draw->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), IM_COL32(255, 255, 255, 18), side * 0.3f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const ImVec2 textSize = ImGui::CalcTextSize(icon);
    draw->AddText(ImVec2(pos.x + (side - textSize.x) * 0.5f, pos.y + (side - textSize.y) * 0.5f),
                  hovered ? Lerp(color, IM_COL32(255, 255, 255, 255), 0.35f) : color, icon);
    if (tooltip && hovered) Tooltip(tooltip);
    ImGui::PopID();
    return pressed;
}

void Pill(const char* text, ImU32 color, bool filled) {
    ImGui::PushFont(fonts.small);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(9, 3);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 end(pos.x + size.x + pad.x * 2, pos.y + size.y + pad.y * 2);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, end, filled ? color : WithAlpha(color, 0.16f), (end.y - pos.y) * 0.5f);
    draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), filled ? IM_COL32(16, 26, 18, 255) : color, text);
    ImGui::Dummy(ImVec2(end.x - pos.x, end.y - pos.y));
    ImGui::PopFont();
}

void Tooltip(const char* text) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 9));
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

void HelpMarker(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::TextDim));
    ImGui::TextUnformatted(Icon::Question);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) Tooltip(text);
}

void BeginCard(const char* id, const char* icon, const char* title, ImU32 accent, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToVec(Color::Card));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0) flags |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, ImVec2(0, height > 0 ? height : 0), flags, height > 0 ? 0 : ImGuiWindowFlags_NoScrollbar);
    if (title) {
        ImGui::PushFont(fonts.bold);
        if (icon) {
            ImGui::PushStyleColor(ImGuiCol_Text, ToVec(accent));
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 10);
        }
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
    }
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

float BeginRow(const char* icon, ImU32 iconColor, const char* title, const char* description, float controlWidth,
               bool enabled) {
    ImGui::PushID(title);
    if (!enabled) {
        ImGui::BeginDisabled();
        ++disabledRows;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float box = ImGui::GetFrameHeight() * 1.15f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, ImVec2(start.x + box, start.y + box), WithAlpha(iconColor, enabled ? 0.15f : 0.07f), box * 0.32f);
    if (icon) {
        const ImVec2 size = ImGui::CalcTextSize(icon);
        draw->AddText(ImVec2(start.x + (box - size.x) * 0.5f, start.y + (box - size.y) * 0.5f),
                      enabled ? iconColor : WithAlpha(iconColor, 0.4f), icon);
    }
    ImGui::Dummy(ImVec2(box, box));
    ImGui::SameLine(0, 12);
    const float textWidth = std::max(80.0f, avail - box - 12 - controlWidth - 16);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
    ImGui::TextUnformatted(title);
    if (description && *description) {
        ImGui::PushFont(fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::TextDim));
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float rowHeight = std::max(box, ImGui::GetItemRectSize().y);
    const float controlX = start.x + avail - controlWidth;
    ImGui::SetCursorScreenPos(ImVec2(controlX, start.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f));
    // Keep the row height reserved even if the control is shorter.
    ImGui::GetCurrentWindow()->DC.CursorMaxPos.y = std::max(ImGui::GetCurrentWindow()->DC.CursorMaxPos.y, start.y + rowHeight);
    return controlX;
}

void EndRow() {
    if (disabledRows > 0) {
        ImGui::EndDisabled();
        --disabledRows;
    }
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 2));
}

bool SidebarItem(const char* icon, const char* label, bool selected, ImU32 accent) {
    ImGui::PushID(label);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() * 1.12f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##item", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (selected) {
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), WithAlpha(accent, 0.14f), 10);
        draw->AddRectFilled(ImVec2(pos.x, pos.y + height * 0.22f), ImVec2(pos.x + 4, pos.y + height * 0.78f), accent, 2);
    } else if (hovered) {
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(255, 255, 255, 10), 10);
    }
    ImFont* iconFont = fonts.icons ? fonts.icons : ImGui::GetFont();
    const float iconSize = iconFont->FontSize;
    const ImVec2 iconDim = iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0, icon);
    const float iconBox = iconSize * 1.45f;
    draw->AddText(iconFont, iconSize, ImVec2(pos.x + 14 + (iconBox - iconDim.x) * 0.5f, pos.y + (height - iconDim.y) * 0.5f),
                  selected ? accent : (hovered ? Color::Text : Color::TextDim), icon);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pos.x + 14 + iconBox + 8, pos.y + (height - textSize.y) * 0.5f),
                  selected ? Color::Text : (hovered ? Color::Text : Color::TextDim), label);
    ImGui::PopID();
    return pressed;
}

void Meter(const char* icon, ImU32 color, float value, float max, const char* label, float width) {
    const float fraction = max > 0 ? std::clamp(value / max, 0.0f, 1.0f) : 0.0f;
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec(color));
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 8);
    ImGui::PushFont(fonts.small);
    ImGui::TextUnformatted(label);
    char numbers[48];
    if (max > 0) std::snprintf(numbers, sizeof(numbers), "%.0f / %.0f", value, max);
    else std::snprintf(numbers, sizeof(numbers), "—");
    const float numbersWidth = ImGui::CalcTextSize(numbers).x;
    ImGui::SameLine(width - numbersWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::TextDim));
    ImGui::TextUnformatted(numbers);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = 8;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), IM_COL32(255, 255, 255, 16), h * 0.5f);
    if (fraction > 0)
        draw->AddRectFilled(pos, ImVec2(pos.x + std::max(h, width * fraction), pos.y + h), color, h * 0.5f);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::EndGroup();
}

void SectionTitle(const char* icon, const char* title, const char* subtitle) {
    ImGui::PushFont(fonts.title);
    if (icon) {
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::Accent));
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 12);
    }
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle) TextWrappedDim(subtitle);
    ImGui::Dummy(ImVec2(0, 4));
}

void Separator() {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 4), ImVec2(pos.x + width, pos.y + 4), Color::Border, 1.0f);
    ImGui::Dummy(ImVec2(width, 9));
}

void TextDim(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::TextDim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void TextWrappedDim(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec(Color::TextDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool SearchBox(const char* id, char* buffer, size_t size, const char* hint, float width) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38, ImGui::GetStyle().FramePadding.y + 2));
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputTextWithHint("##search", hint, buffer, size);
    ImGui::PopStyleVar();
    const float h = ImGui::GetItemRectSize().y;
    const ImVec2 iconSize = ImGui::CalcTextSize(Icon::Search);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 14, pos.y + (h - iconSize.y) * 0.5f), Color::TextDim, Icon::Search);
    if (buffer[0]) {
        ImGui::SameLine(0, 0);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - h, pos.y));
        if (IconButton(Icon::Close, "clear", nullptr, Color::TextDim, h)) {
            buffer[0] = 0;
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

void PushToast(const std::string& text, ToastKind kind) {
    std::lock_guard lock(toastMutex);
    toasts.push_back({text, kind, ImGui::GetTime()});
    while (toasts.size() > 4) toasts.pop_front();
}

void DrawToasts() {
    std::lock_guard lock(toastMutex);
    const double now = ImGui::GetTime();
    while (!toasts.empty() && now - toasts.front().born > 5.0) toasts.pop_front();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float y = viewport->WorkPos.y + viewport->WorkSize.y - 22;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImGui::PushFont(fonts.body);
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        const double age = now - it->born;
        const float alpha = static_cast<float>(std::min({1.0, age * 6.0, (5.0 - age) * 2.0}));
        const float maxWidth = std::min(460.0f, viewport->WorkSize.x * 0.6f);
        const ImVec2 textSize = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxWidth - 60);
        const ImVec2 size(std::min(maxWidth, textSize.x + 64), textSize.y + 26);
        const float slide = (1.0f - std::min(1.0f, static_cast<float>(age) * 5.0f)) * 30.0f;
        const ImVec2 pos(viewport->WorkPos.x + viewport->WorkSize.x - size.x - 22 + slide, y - size.y);
        const ImU32 accent = KindColor(it->kind);
        draw->AddRectFilled(ImVec2(pos.x + 2, pos.y + 4), ImVec2(pos.x + size.x + 2, pos.y + size.y + 4), WithAlpha(IM_COL32(0, 0, 0, 255), 0.35f * alpha), 12);
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(IM_COL32(28, 42, 34, 255), 0.98f * alpha), 12);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(accent, 0.55f * alpha), 12, 0, 1.2f);
        draw->AddText(ImVec2(pos.x + 16, pos.y + 13), WithAlpha(accent, alpha), KindIcon(it->kind));
        draw->AddText(nullptr, 0, ImVec2(pos.x + 44, pos.y + 13), WithAlpha(Color::Text, alpha), it->text.c_str(), nullptr, maxWidth - 60);
        y = pos.y - 10;
    }
    ImGui::PopFont();
}

void OpenConfirm(const char* id) { ImGui::OpenPopup(id); }

int Confirm(const char* id, const char* title, const char* text, const char* yes, ImU32 yesColor) {
    int result = -1;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(520.0f, viewport->WorkSize.x * 0.8f), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 22));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(fonts.title);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec(yesColor));
        ImGui::TextUnformatted(Icon::Warning);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 12);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 10));
        const float w = 150;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w * 2 - 24 - 10);
        if (GhostButton("Cancelar", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            result = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 10);
        if (Button(yes, ImVec2(w, 0), yesColor)) {
            result = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    return result;
}

} // namespace Theme
