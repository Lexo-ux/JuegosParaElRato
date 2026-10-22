// SPDX-License-Identifier: GPL-3.0-or-later
// Visual identity shared by the panel and the launcher: colours inspired by a backyard at
// dusk, rounded fonts (Nunito / Fredoka) and Font Awesome icons, plus a few custom widgets.
#pragma once
#include "imgui.h"

#include <cstdint>
#include <string>

struct HINSTANCE__;

namespace Icon {
// Font Awesome 6 Free Solid (subset in assets/fonts/fa-solid-subset.ttf)
inline constexpr const char* Home = "\xef\x80\x95";          // f015
inline constexpr const char* BoxOpen = "\xef\x92\x9e";       // f49e
inline constexpr const char* Cubes = "\xef\x86\xb3";         // f1b3
inline constexpr const char* Hammer = "\xef\x9b\xa3";        // f6e3
inline constexpr const char* Fist = "\xef\x9b\x9e";          // f6de
inline constexpr const char* Dna = "\xef\x91\xb1";           // f471
inline constexpr const char* Broom = "\xef\x94\x9a";         // f51a
inline constexpr const char* Magnet = "\xef\x81\xb6";        // f076
inline constexpr const char* User = "\xef\x80\x87";          // f007
inline constexpr const char* Running = "\xef\x9c\x8c";       // f70c
inline constexpr const char* Earth = "\xef\x95\xbd";         // f57d
inline constexpr const char* Sun = "\xef\x86\x85";           // f185
inline constexpr const char* Moon = "\xef\x86\x86";          // f186
inline constexpr const char* LocationDot = "\xef\x8f\x85";   // f3c5
inline constexpr const char* MapLocation = "\xef\x96\xa0";   // f5a0
inline constexpr const char* Bug = "\xef\x86\x88";           // f188
inline constexpr const char* Gem = "\xef\x8e\xa5";           // f3a5
inline constexpr const char* Tooth = "\xef\x97\x89";         // f5c9
inline constexpr const char* Gear = "\xef\x80\x93";          // f013
inline constexpr const char* Scroll = "\xef\x9c\x8e";        // f70e
inline constexpr const char* FileLines = "\xef\x85\x9c";     // f15c
inline constexpr const char* Close = "\xef\x80\x8d";         // f00d
inline constexpr const char* Minus = "\xef\x81\xa8";         // f068
inline constexpr const char* Minimize = "\xef\x8b\x91";      // f2d1
inline constexpr const char* Star = "\xef\x80\x85";          // f005
inline constexpr const char* Search = "\xef\x80\x82";        // f002
inline constexpr const char* Check = "\xef\x80\x8c";         // f00c
inline constexpr const char* CircleCheck = "\xef\x81\x98";   // f058
inline constexpr const char* Warning = "\xef\x81\xb1";       // f071
inline constexpr const char* Info = "\xef\x81\x9a";          // f05a
inline constexpr const char* CircleX = "\xef\x81\x97";       // f057
inline constexpr const char* Heart = "\xef\x80\x84";         // f004
inline constexpr const char* Bolt = "\xef\x83\xa7";          // f0e7
inline constexpr const char* Drumstick = "\xef\x9b\x97";     // f6d7
inline constexpr const char* Droplet = "\xef\x81\x83";       // f043
inline constexpr const char* Shield = "\xef\x8f\xad";        // f3ed
inline constexpr const char* Feather = "\xef\x94\xad";       // f52d
inline constexpr const char* Walking = "\xef\x95\x94";       // f554
inline constexpr const char* Gauge = "\xef\x98\xa5";         // f625
inline constexpr const char* ArrowUp = "\xef\x81\xa2";       // f062
inline constexpr const char* ArrowDown = "\xef\x81\xa3";     // f063
inline constexpr const char* ArrowRight = "\xef\x81\xa1";    // f061
inline constexpr const char* ArrowLeft = "\xef\x81\xa0";     // f060
inline constexpr const char* Ghost = "\xef\x9b\xa2";         // f6e2
inline constexpr const char* Clock = "\xef\x80\x97";         // f017
inline constexpr const char* Forward = "\xef\x81\x8e";       // f04e
inline constexpr const char* Plus = "\xef\x81\xa7";          // f067
inline constexpr const char* Trash = "\xef\x87\xb8";         // f1f8
inline constexpr const char* Save = "\xef\x83\x87";          // f0c7
inline constexpr const char* Undo = "\xef\x8b\xaa";          // f2ea
inline constexpr const char* Rotate = "\xef\x8b\xb1";        // f2f1
inline constexpr const char* Play = "\xef\x81\x8b";          // f04b
inline constexpr const char* Plug = "\xef\x87\xa6";          // f1e6
inline constexpr const char* FolderOpen = "\xef\x81\xbc";    // f07c
inline constexpr const char* Copy = "\xef\x83\x85";          // f0c5
inline constexpr const char* List = "\xef\x80\xba";          // f03a
inline constexpr const char* Magic = "\xee\x8b\x8a";         // e2ca
inline constexpr const char* Seedling = "\xef\x93\x98";      // f4d8
inline constexpr const char* Leaf = "\xef\x81\xac";          // f06c
inline constexpr const char* Map = "\xef\x89\xb9";           // f279
inline constexpr const char* Unlock = "\xef\x82\x9c";        // f09c
inline constexpr const char* Lock = "\xef\x80\xa3";          // f023
inline constexpr const char* Wrench = "\xef\x82\xad";        // f0ad
inline constexpr const char* Suitcase = "\xef\x83\xb2";      // f0f2
inline constexpr const char* PeopleGroup = "\xee\x94\xb3";   // e533
inline constexpr const char* Users = "\xef\x83\x80";         // f0c0
inline constexpr const char* UserPlus = "\xef\x88\xb4";      // f234
inline constexpr const char* SignIn = "\xef\x82\x90";        // f090
inline constexpr const char* Swap = "\xef\x8d\xa2";          // f362
inline constexpr const char* Crosshairs = "\xef\x98\x81";    // f601
inline constexpr const char* Paw = "\xef\x86\xb0";           // f1b0
inline constexpr const char* Horse = "\xef\x9b\xb0";         // f6f0
inline constexpr const char* Spider = "\xef\x9c\x97";        // f717
inline constexpr const char* Flask = "\xef\x83\x83";         // f0c3
inline constexpr const char* Coins = "\xef\x94\x9e";         // f51e
inline constexpr const char* HandHeart = "\xef\x92\xbe";     // f4be
inline constexpr const char* Skull = "\xef\x95\x8c";         // f54c
inline constexpr const char* Eye = "\xef\x81\xae";           // f06e
inline constexpr const char* EyeSlash = "\xef\x81\xb0";      // f070
inline constexpr const char* Keyboard = "\xef\x84\x9c";      // f11c
inline constexpr const char* Palette = "\xef\x94\xbf";       // f53f
inline constexpr const char* Contrast = "\xef\x81\x82";      // f042
inline constexpr const char* Refresh = "\xef\x80\xa1";       // f021
inline constexpr const char* Hourglass = "\xef\x89\x94";     // f254
inline constexpr const char* Tree = "\xef\x86\xbb";          // f1bb
inline constexpr const char* Campground = "\xef\x9a\xbb";    // f6bb
inline constexpr const char* Gamepad = "\xef\x84\x9b";       // f11b
inline constexpr const char* Download = "\xef\x80\x99";      // f019
inline constexpr const char* Archive = "\xef\x86\x87";       // f187
inline constexpr const char* CirclePlay = "\xef\x85\x84";    // f144
inline constexpr const char* RotateLeft = "\xef\x83\xa2";    // f0e2
inline constexpr const char* CaretDown = "\xef\x83\x97";     // f0d7
inline constexpr const char* CaretRight = "\xef\x83\x9a";    // f0da
inline constexpr const char* History = "\xef\x87\x9a";       // f1da
inline constexpr const char* RotateRight = "\xef\x8b\xb9";   // f2f9
inline constexpr const char* Exclamation = "\xef\x84\xaa";   // f12a
inline constexpr const char* Question = "\xef\x84\xa8";      // f128
inline constexpr const char* Lightbulb = "\xef\x83\xab";     // f0eb
inline constexpr const char* HandSparkles = "\xef\x92\xba";  // f4ba (hands helping)
inline constexpr const char* Cat = "\xef\x9a\xbe";           // f6be
inline constexpr const char* Burger = "\xef\x9f\xa4";        // f7e4
inline constexpr const char* Burst = "\xee\x93\x9c";         // e4dc
inline constexpr const char* Bomb = "\xef\x87\xa2";          // f1e2
inline constexpr const char* PaintBrush = "\xef\x87\xbc";    // f1fc
inline constexpr const char* Hammer2 = "\xef\x9f\x99";       // f7d9 (screwdriver-wrench)
inline constexpr const char* Dragon = "\xef\x9b\x95";        // f6d5
inline constexpr const char* Tachometer = "\xef\x83\xa4";    // f0e4
} // namespace Icon

namespace Theme {

// Palette (RGBA)
namespace Color {
inline constexpr ImU32 Background = IM_COL32(14, 20, 17, 255);
inline constexpr ImU32 Sidebar = IM_COL32(17, 26, 21, 255);
inline constexpr ImU32 Card = IM_COL32(22, 33, 27, 255);
inline constexpr ImU32 CardHover = IM_COL32(27, 41, 33, 255);
inline constexpr ImU32 Border = IM_COL32(36, 51, 41, 255);
inline constexpr ImU32 Text = IM_COL32(231, 240, 233, 255);
inline constexpr ImU32 TextDim = IM_COL32(147, 166, 152, 255);
inline constexpr ImU32 Accent = IM_COL32(139, 212, 80, 255);
inline constexpr ImU32 AccentDark = IM_COL32(95, 163, 50, 255);
inline constexpr ImU32 Amber = IM_COL32(245, 184, 65, 255);
inline constexpr ImU32 Red = IM_COL32(239, 91, 91, 255);
inline constexpr ImU32 Blue = IM_COL32(92, 184, 230, 255);
inline constexpr ImU32 Purple = IM_COL32(180, 140, 242, 255);
inline constexpr ImU32 Pink = IM_COL32(240, 128, 170, 255);
} // namespace Color

ImVec4 ToVec(ImU32 color, float alphaScale = 1.0f);
ImU32 WithAlpha(ImU32 color, float alpha);
ImU32 Lerp(ImU32 a, ImU32 b, float t);

struct Fonts {
    ImFont* body = nullptr;    // Nunito SemiBold + icons
    ImFont* bold = nullptr;    // Nunito ExtraBold + icons
    ImFont* title = nullptr;   // Fredoka SemiBold, large + icons
    ImFont* huge = nullptr;    // Fredoka SemiBold, extra large
    ImFont* small = nullptr;   // Nunito SemiBold, small
    ImFont* icons = nullptr;   // Font Awesome, large (sidebar)
};
const Fonts& GetFonts();

// Fonts come from RCDATA resources 101..104 of `module` (falls back to Segoe UI).
void Load(HINSTANCE__* module, float scale);
void Apply(float scale);

// ---------------------------------------------------------------- widgets
bool Toggle(const char* id, bool* value, bool enabled = true);
bool Button(const char* label, ImVec2 size = {0, 0}, ImU32 color = Color::Accent, bool enabled = true);
bool GhostButton(const char* label, ImVec2 size = {0, 0}, bool enabled = true);
bool IconButton(const char* icon, const char* id, const char* tooltip = nullptr, ImU32 color = Color::TextDim, float size = 0);
void Pill(const char* text, ImU32 color, bool filled = false);
void Tooltip(const char* text);
void HelpMarker(const char* text);

// Rounded card with optional icon + title. Always pair with EndCard().
void BeginCard(const char* id, const char* icon = nullptr, const char* title = nullptr, ImU32 accent = Color::Accent,
               float height = 0);
void EndCard();

// One feature row: [icon] Title / description ............... [control]
// Returns the x position where the control should start (right aligned, width `controlWidth`).
float BeginRow(const char* icon, ImU32 iconColor, const char* title, const char* description, float controlWidth,
               bool enabled = true);
void EndRow();

bool SidebarItem(const char* icon, const char* label, bool selected, ImU32 accent = Color::Accent);
void Meter(const char* icon, ImU32 color, float value, float max, const char* label, float width);
void SectionTitle(const char* icon, const char* title, const char* subtitle = nullptr);
void Separator();
void TextDim(const char* text);
void TextWrappedDim(const char* text);
bool SearchBox(const char* id, char* buffer, size_t size, const char* hint, float width);

// Toasts (bottom-right notifications)
enum class ToastKind { Info, Success, Warning, Error };
void PushToast(const std::string& text, ToastKind kind);
void DrawToasts();

// Modal confirmation. Call every frame with the same id; returns 1 = yes, 0 = no, -1 = pending.
void OpenConfirm(const char* id);
int Confirm(const char* id, const char* title, const char* text, const char* yes, ImU32 yesColor = Color::Red);

} // namespace Theme
