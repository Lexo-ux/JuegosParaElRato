// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pages.hpp"

#include "common/DxWindow.hpp"
#include "common/Paths.hpp"
#include "common/Theme.hpp"
#include "core/Log.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <unordered_map>

using namespace Bridge;
namespace C = Theme::Color;

namespace Pages {
namespace {

// ------------------------------------------------------------------------------------- helpers

const FeatureState& Fs(const Snapshot& s, Feature f) { return s.features[static_cast<size_t>(f)]; }
bool Ok(const Snapshot& s, Feature f) { return s.ready && s.Has(f); }

std::string Why(const Snapshot& s, Feature f) {
    if (!s.resolved) return "El panel todavía está localizando las estructuras del juego.";
    if (!s.ready) return "Carga una partida en solitario o como anfitrión para usar esta función.";
    const auto& state = Fs(s, f);
    if (state.ok) return {};
    return "No disponible en esta versión del juego: " + Utf8(state.why);
}

void WhyTooltip(const Snapshot& s, Feature f) {
    if (!Ok(s, f) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) Theme::Tooltip(Why(s, f).c_str());
}

Request Make(Action action) {
    Request r;
    r.action = action;
    return r;
}

// The game applies requests on its own thread a moment later; keep the clicked state on
// screen meanwhile so switches do not flicker back.
struct PendingValue {
    bool value;
    double until;
};
std::unordered_map<std::string, PendingValue> pending;

bool Shown(const std::string& key, bool actual) {
    auto it = pending.find(key);
    if (it == pending.end()) return actual;
    if (ImGui::GetTime() > it->second.until || it->second.value == actual) {
        pending.erase(it);
        return actual;
    }
    return it->second.value;
}
void SetPending(const std::string& key, bool value) { pending[key] = {value, ImGui::GetTime() + 1.5}; }

float ControlWidth(float base) { return base * ImGui::GetFontSize() / 17.0f; }

void ToggleRow(const Snapshot& s, Feature f, const char* key, const char* icon, ImU32 color, const char* title,
               const char* description, bool actual, const std::function<void(bool)>& onChange) {
    const bool ok = Ok(s, f);
    Theme::BeginRow(icon, color, title, description, ControlWidth(56), ok);
    bool value = Shown(key, actual);
    if (Theme::Toggle(key, &value, ok)) {
        SetPending(key, value);
        onChange(value);
    }
    WhyTooltip(s, f);
    Theme::EndRow();
}

bool ButtonRow(const Snapshot& s, Feature f, const char* icon, ImU32 color, const char* title, const char* description,
               const char* button, ImU32 buttonColor = C::Accent, float width = 170) {
    const bool ok = Ok(s, f);
    const float w = ControlWidth(width);
    Theme::BeginRow(icon, color, title, description, w, ok);
    bool pressed = Theme::Button(button, ImVec2(w, 0), buttonColor, ok);
    WhyTooltip(s, f);
    Theme::EndRow();
    return pressed;
}

bool Segmented(const char* id, int* value, std::initializer_list<const char*> labels, float width) {
    ImGui::PushID(id);
    bool changed = false;
    const float itemWidth = (width - (labels.size() - 1) * 4) / static_cast<float>(labels.size());
    int index = 0;
    for (const char* label : labels) {
        if (index) ImGui::SameLine(0, 4);
        const bool selected = *value == index;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::ToVec(C::Accent, 0.9f) : Theme::ToVec(IM_COL32(30, 44, 36, 255)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? Theme::ToVec(C::Accent) : Theme::ToVec(IM_COL32(40, 58, 47, 255)));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? Theme::ToVec(IM_COL32(16, 26, 18, 255)) : Theme::ToVec(C::Text));
        if (ImGui::Button(label, ImVec2(itemWidth, 0)) && !selected) {
            *value = index;
            changed = true;
        }
        ImGui::PopStyleColor(3);
        ++index;
    }
    ImGui::PopID();
    return changed;
}

// Multiplier presets under a slider.
bool Presets(const char* id, float* value, std::initializer_list<float> presets) {
    ImGui::PushID(id);
    bool changed = false;
    ImGui::PushFont(Theme::GetFonts().small);
    for (float p : presets) {
        char label[16];
        if (p == static_cast<int>(p)) std::snprintf(label, sizeof(label), "%d×", static_cast<int>(p));
        else std::snprintf(label, sizeof(label), "%.2g×", p);
        for (char* c = label; *c; ++c) if (*c == '.') *c = ',';
        ImGui::PushID(label);
        const bool active = std::fabs(*value - p) < 0.001f;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? Theme::ToVec(C::Accent, 0.25f) : Theme::ToVec(IM_COL32(30, 44, 36, 255)));
        if (ImGui::SmallButton(label)) {
            *value = p;
            changed = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::SameLine(0, 6);
    }
    ImGui::NewLine();
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}

void EmptyState(const char* icon, const char* title, const char* text) {
    Theme::BeginCard("##empty");
    ImGui::Dummy(ImVec2(0, 18));
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(Theme::GetFonts().huge);
    ImVec2 size = ImGui::CalcTextSize(icon);
    ImGui::SetCursorPosX((width - size.x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec(C::AccentDark));
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(Theme::GetFonts().title);
    size = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX((width - size.x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    size = ImGui::CalcTextSize(text, nullptr, false, width * 0.8f);
    ImGui::SetCursorPosX((width - size.x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width * 0.8f);
    Theme::TextWrappedDim(text);
    ImGui::PopTextWrapPos();
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 18));
    Theme::EndCard();
}

// Shown at the top of game pages while there is no usable session.
bool RequireSession(const Snapshot& s) {
    if (s.ready) return true;
    if (!s.resolved)
        EmptyState(Icon::Hourglass, "Preparando el panel…",
                   "Buscando las estructuras internas de Grounded 2. Si acabas de abrir el juego, espera a que cargue el menú.");
    else
        EmptyState(Icon::Campground, "Esperando una partida",
                   "Carga una partida en solitario o crea una partida como anfitrión. Las funciones aparecerán aquí automáticamente.");
    return false;
}

std::wstring Wide(const char* text) { return Paths::Wide(text); }

std::string Number(double value, int decimals = 0) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

const Player* LocalPlayer(const Snapshot& s) {
    for (const auto& p : s.players)
        if (p.local) return &p;
    return nullptr;
}

// Player picker; returns the selected id or -1.
int PlayerCombo(const char* id, const Snapshot& s, int current, bool includeLocal, float width, const char* none) {
    std::string preview = none;
    for (const auto& p : s.players)
        if (p.id == current) preview = Utf8(p.name) + (p.local ? " (tú)" : "");
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (none && ImGui::Selectable(none, current == -1)) current = -1;
        for (const auto& p : s.players) {
            if (p.local && !includeLocal) continue;
            std::string label = Utf8(p.name) + (p.local ? " (tú)" : "") + (p.available ? "" : " · sin personaje");
            ImGui::PushID(p.id);
            if (ImGui::Selectable(label.c_str(), p.id == current)) current = p.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return current;
}

bool ConfirmPermanent(Context& c, const char* id) {
    // Returns true if the action may run now (no confirmation required).
    if (!c.settings.confirmPermanent) return true;
    Theme::OpenConfirm(id);
    return false;
}

// ------------------------------------------------------------------------------------- Inicio

void PageHome(Context& c) {
    const auto& s = c.snapshot;
    const auto& fonts = Theme::GetFonts();

    Theme::BeginCard("##hero");
    ImGui::PushFont(fonts.huge);
    std::string hello = s.player.empty() ? "¡Hola!" : "¡Hola, " + Utf8(s.player) + "!";
    ImGui::TextUnformatted(hello.c_str());
    ImGui::PopFont();
    if (s.ready) Theme::Pill((std::string(Icon::CircleCheck) + "  Conectado").c_str(), C::Accent);
    else if (s.resolved) Theme::Pill((std::string(Icon::Hourglass) + "  Esperando partida").c_str(), C::Amber);
    else Theme::Pill((std::string(Icon::Search) + "  Localizando el juego").c_str(), C::Blue);
    ImGui::SameLine(0, 8);
    Theme::Pill(("Grounded 2 " + (s.gameVersion.empty() ? std::string("versión desconocida") : s.gameVersion)).c_str(), C::TextDim);
    if (s.ready) {
        ImGui::SameLine(0, 8);
        Theme::Pill(s.host ? "Anfitrión" : "Invitado", s.host ? C::Purple : C::Red);
    }
    if (!s.message.empty()) {
        ImGui::Dummy(ImVec2(0, 2));
        Theme::TextWrappedDim(Utf8(s.message).c_str());
    }
    Theme::EndCard();

    if (!ImGui::BeginTable("##home", 2, ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableNextColumn();

    // Vitals
    Theme::BeginCard("##vitals", Icon::Heart, "Tu personaje", C::Pink);
    const float width = ImGui::GetContentRegionAvail().x;
    if (s.ready && s.maxHealth > 0) {
        Theme::Meter(Icon::Heart, C::Red, s.health, s.maxHealth, "Salud", width);
        ImGui::Dummy(ImVec2(0, 4));
        Theme::Meter(Icon::Drumstick, C::Amber, s.food, s.maxFood, "Comida", width);
        ImGui::Dummy(ImVec2(0, 4));
        Theme::Meter(Icon::Droplet, C::Blue, s.water, s.maxWater, "Agua", width);
    } else {
        Theme::TextWrappedDim("Los indicadores aparecerán cuando entres en una partida.");
    }
    Theme::EndCard();

    // Quick switches
    Theme::BeginCard("##quick", Icon::Bolt, "Accesos rápidos", C::Amber);
    ToggleRow(s, Feature::GodMode, "q_god", Icon::Shield, C::Purple, "Modo dios", "No recibes daño mortal.", s.god,
              [](bool v) { auto r = Make(Action::GodMode); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::InfiniteStamina, "q_stamina", Icon::Bolt, C::Amber, "Resistencia infinita", nullptr, s.infiniteStamina,
              [](bool v) { auto r = Make(Action::InfiniteStamina); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::NoHunger, "q_hunger", Icon::Drumstick, C::Amber, "Sin hambre ni sed", nullptr, s.noHunger,
              [](bool v) { auto r = Make(Action::NoHunger); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::FreeCraft, "q_craft", Icon::Hammer2, C::Accent, "Fabricar sin recursos", nullptr, s.freeCraft,
              [](bool v) { auto r = Make(Action::FreeCraft); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::FreeBuild, "q_build", Icon::Hammer, C::Accent, "Construir sin recursos", nullptr, s.freeBuild,
              [](bool v) { auto r = Make(Action::FreeBuild); r.enabled = v; Send(r); });
    float radius = c.settings.pickupRadius;
    ToggleRow(s, Feature::AutoPickup, "q_pickup", Icon::Magnet, C::Blue, "Recogida automática", nullptr, s.autoPickup,
              [radius](bool v) { auto r = Make(Action::AutoPickup); r.enabled = v; r.value = radius; Send(r); });
    Theme::EndCard();

    ImGui::TableNextColumn();

    Theme::BeginCard("##actions", Icon::HandHeart, "Acciones rápidas", C::Accent);
    const float half = (ImGui::GetContentRegionAvail().x - 10) * 0.5f;
    auto quick = [&](const char* label, Feature f, Action a, ImU32 color, bool sameLine) {
        if (sameLine) ImGui::SameLine(0, 10);
        if (Theme::Button(label, ImVec2(half, 0), color, Ok(s, f))) Send(Make(a));
        WhyTooltip(s, f);
    };
    quick((std::string(Icon::Heart) + "  Curarme").c_str(), Feature::Heal, Action::Heal, C::Accent, false);
    quick((std::string(Icon::Magic) + "  Restauración total").c_str(), Feature::FullRestore, Action::FullRestore, C::Accent, true);
    quick((std::string(Icon::HandHeart) + "  Reanimarme").c_str(), Feature::Revive, Action::Revive, C::Purple, false);
    quick((std::string(Icon::Bolt) + "  Resistencia").c_str(), Feature::RestoreStamina, Action::RestoreStamina, C::Amber, true);
    {
        auto r = Make(Action::AdvanceHours);
        r.count = 1;
        if (Theme::Button((std::string(Icon::Clock) + "  +1 hora").c_str(), ImVec2(half, 0), C::Blue, Ok(s, Feature::AdvanceTime))) Send(r);
        WhyTooltip(s, Feature::AdvanceTime);
    }
    ImGui::SameLine(0, 10);
    if (Theme::Button((std::string(Icon::Undo) + "  Volver atrás").c_str(), ImVec2(half, 0), C::Blue,
                      Ok(s, Feature::Teleport) && s.hasBackPosition))
        Send(Make(Action::TeleportBack));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        Theme::Tooltip("Vuelve al lugar desde el que te teletransportaste por última vez.");
    Theme::EndCard();

    // Compatibility summary
    Theme::BeginCard("##compat", Icon::Seedling, "Compatibilidad con esta versión", C::Accent);
    int ok = 0;
    for (const auto& f : s.features) ok += f.ok ? 1 : 0;
    if (!s.ready) {
        Theme::TextWrappedDim("Las comprobaciones se hacen al entrar en una partida.");
    } else {
        char line[128];
        std::snprintf(line, sizeof(line), "%d de %d funciones verificadas en tu versión del juego.", ok, static_cast<int>(kFeatureCount));
        ImGui::TextUnformatted(line);
        const float fraction = static_cast<float>(ok) / static_cast<float>(kFeatureCount);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + 8), IM_COL32(255, 255, 255, 16), 4);
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + w * fraction, pos.y + 8), fraction > 0.9f ? C::Accent : C::Amber, 4);
        ImGui::Dummy(ImVec2(w, 10));
        if (ok < static_cast<int>(kFeatureCount)) {
            Theme::TextWrappedDim("Las funciones que el juego cambió aparecen desactivadas en vez de arriesgar tu partida. Encontrarás los detalles en Ajustes · Compatibilidad.");
        }
    }
    Theme::EndCard();

    ImGui::EndTable();
}

// ------------------------------------------------------------------------------------- Objetos

struct Kit {
    const char* icon;
    const char* name;
    const char* description;
    std::vector<std::pair<const char*, int>> items;
};

const std::vector<Kit>& Kits() {
    static const std::vector<Kit> kits = {
        {Icon::Leaf, "Materiales básicos", "Guijarros, fibra, ramitas, savia, arcilla y seda.",
         {{"StoneShale", 30}, {"FiberRaw", 30}, {"Twig", 30}, {"Sap", 15}, {"Clay", 15}, {"Web", 15}}},
        {Icon::Hammer, "Construcción", "Tablones, tallos, ramitas, cuerda, savia y arcilla.",
         {{"GrassPlank", 60}, {"log", 40}, {"Twig", 40}, {"FiberWoven", 20}, {"Sap", 20}, {"Clay", 20}}},
        {Icon::Drumstick, "Comida y curación", "Cecina, vendas, melaza, setas y bayas.",
         {{"DriedAphidMeat", 20}, {"BandageTier2", 10}, {"Honeydew", 10}, {"Mushroom", 10}, {"BerryRed", 10}}},
        {Icon::Map, "Exploración", "Antorchas, bombas curativas, cuerda y vendas.",
         {{"Torch", 3}, {"BombHeal", 3}, {"FiberWoven", 10}, {"BandageTier1", 10}}},
        {Icon::Gem, "Mejoras de armas", "Fragmentos de cuarcita y glóbulos poderosos.",
         {{"UpgradeWeapon1", 5}, {"UpgradeWeapon2", 5}, {"UpgradeWeapon3", 3}, {"IngotQuartzite", 5}}},
    };
    return kits;
}

struct ItemsState {
    char search[128] = {};
    std::string lastSearch;
    bool lastShowUntranslated = false;
    int tab = 0;  // 0 todos, 1 favoritos, 2 recientes
    int lastTab = -1;
    size_t favoritesVersion = 0;
    uint64_t catalogRevision = ~0ull;
    std::vector<int> filtered;
    std::string selected;
    int quantity = 1;
    int recipient = -1;
} items;

const Item* FindItem(const std::vector<Item>& list, const std::string& id) {
    for (const auto& item : list)
        if (item.id == id) return &item;
    return nullptr;
}

void Spawn(const Snapshot& s, Context& c, const std::string& id, int count, int recipient) {
    auto r = Make(Action::Spawn);
    r.id = id;
    r.count = std::clamp(count, 1, 999);
    r.playerId = recipient;
    Send(r);
    Settings::AddRecent(c.settings, id);
    c.settingsChanged = true;
    (void)s;
}

void PageItems(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    const auto& fonts = Theme::GetFonts();
    static const std::vector<Item> none;
    const auto& list = s.items ? *s.items : none;

    if (!Ok(s, Feature::Items)) {
        EmptyState(Icon::BoxOpen, "Generador de objetos no disponible", Why(s, Feature::Items).c_str());
        return;
    }

    // Filtering (only when something changed).
    const size_t favVersion = c.settings.favorites.size() * 31 + c.settings.recents.size();
    if (items.catalogRevision != s.catalogRevision || items.lastSearch != items.search ||
        items.lastShowUntranslated != c.settings.showUntranslated || items.lastTab != items.tab ||
        (items.tab != 0 && items.favoritesVersion != favVersion)) {
        items.catalogRevision = s.catalogRevision;
        items.lastSearch = items.search;
        items.lastShowUntranslated = c.settings.showUntranslated;
        items.lastTab = items.tab;
        items.favoritesVersion = favVersion;
        items.filtered.clear();
        const std::wstring key = SearchKey(Wide(items.search));
        std::vector<std::pair<size_t, int>> ordered;  // for recents: keep recency order
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            const auto& item = list[i];
            if (!item.translated && !c.settings.showUntranslated) continue;
            if (!key.empty() && item.key.find(key) == std::wstring::npos) continue;
            if (items.tab == 1 && !Settings::IsFavorite(c.settings, item.id)) continue;
            if (items.tab == 2) {
                auto it = std::find(c.settings.recents.begin(), c.settings.recents.end(), item.id);
                if (it == c.settings.recents.end()) continue;
                ordered.emplace_back(static_cast<size_t>(it - c.settings.recents.begin()), i);
                continue;
            }
            items.filtered.push_back(i);
        }
        if (items.tab == 2) {
            std::sort(ordered.begin(), ordered.end());
            for (auto& [_, index] : ordered) items.filtered.push_back(index);
        }
    }

    const float rightWidth = ControlWidth(360);
    if (!ImGui::BeginTable("##itemslayout", 2, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("lista", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("detalle", ImGuiTableColumnFlags_WidthFixed, rightWidth);
    ImGui::TableNextColumn();

    // ---- list
    Theme::BeginCard("##itemlist", nullptr, nullptr, C::Accent, ImGui::GetContentRegionAvail().y > 200 ? 0 : 0);
    const float listWidth = ImGui::GetContentRegionAvail().x;
    Theme::SearchBox("items", items.search, sizeof(items.search), "Busca por nombre (sin importar tildes)…", listWidth);
    Segmented("itemtabs", &items.tab, {"Todos", "Favoritos", "Recientes"}, listWidth * 0.62f);
    ImGui::SameLine(0, 14);
    bool showUntranslated = c.settings.showUntranslated;
    if (Theme::Toggle("##untranslated", &showUntranslated)) {
        c.settings.showUntranslated = showUntranslated;
        c.settingsChanged = true;
    }
    ImGui::SameLine(0, 8);
    ImGui::PushFont(fonts.small);
    ImGui::AlignTextToFramePadding();
    Theme::TextDim("Sin traducción");
    ImGui::PopFont();

    char count[96];
    std::snprintf(count, sizeof(count), "%d de %d objetos", static_cast<int>(items.filtered.size()), static_cast<int>(list.size()));
    ImGui::PushFont(fonts.small);
    Theme::TextDim(count);
    ImGui::PopFont();

    const float listHeight = std::max(240.0f, ImGui::GetMainViewport()->WorkSize.y - ImGui::GetCursorScreenPos().y - 40);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec(IM_COL32(15, 23, 19, 255)));
    ImGui::BeginChild("##rows", ImVec2(0, listHeight), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(items.filtered.size()), ImGui::GetFrameHeight() * 1.25f);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& item = list[items.filtered[row]];
            ImGui::PushID(item.id.c_str());
            const bool favorite = Settings::IsFavorite(c.settings, item.id);
            const float h = ImGui::GetFrameHeight() * 1.25f;
            if (Theme::IconButton(Icon::Star, "fav", favorite ? "Quitar de favoritos" : "Añadir a favoritos",
                                  favorite ? C::Amber : IM_COL32(70, 88, 76, 255), h)) {
                Settings::ToggleFavorite(c.settings, item.id);
                c.settingsChanged = true;
            }
            ImGui::SameLine(0, 4);
            const bool selected = item.id == items.selected;
            const std::string name = Utf8(item.name);
            if (ImGui::Selectable("##sel", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, h))) {
                items.selected = item.id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Spawn(s, c, item.id, items.quantity, items.recipient);
            }
            if (ImGui::IsItemHovered()) Theme::Tooltip("Doble clic para generar la cantidad elegida.");
            const ImVec2 rmin = ImGui::GetItemRectMin();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float textY = rmin.y + (h - ImGui::GetFontSize()) * 0.5f;
            draw->AddText(ImVec2(rmin.x + 8, textY), item.translated ? C::Text : C::Amber, name.c_str());
            ImGui::PushFont(fonts.small);
            const ImVec2 idSize = ImGui::CalcTextSize(item.id.c_str());
            draw->AddText(ImVec2(ImGui::GetItemRectMax().x - idSize.x - 10, rmin.y + (h - ImGui::GetFontSize()) * 0.5f), C::TextDim,
                          item.id.c_str());
            ImGui::PopFont();
            ImGui::PopID();
        }
    }
    if (items.filtered.empty()) {
        ImGui::Dummy(ImVec2(0, 20));
        Theme::TextWrappedDim(items.tab == 1   ? "Aún no tienes favoritos. Pulsa la estrella junto a un objeto."
                              : items.tab == 2 ? "Los objetos que generes aparecerán aquí."
                              : list.empty()   ? "El catálogo todavía se está cargando…"
                                               : "No hay objetos que coincidan con la búsqueda.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Theme::EndCard();

    // ---- detail
    ImGui::TableNextColumn();
    Theme::BeginCard("##spawn", Icon::BoxOpen, "Generar objeto", C::Accent);
    const Item* selected = FindItem(list, items.selected);
    if (selected) {
        ImGui::PushFont(fonts.title);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Utf8(selected->name).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PushFont(fonts.small);
        Theme::TextDim(selected->id.c_str());
        ImGui::PopFont();
    } else {
        Theme::TextWrappedDim("Selecciona un objeto de la lista para generarlo.");
    }
    ImGui::Dummy(ImVec2(0, 4));
    Theme::TextDim("Cantidad");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##qty", &items.quantity, 1, 999, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::PushFont(fonts.small);
    for (int preset : {1, 5, 10, 50, 100, 999}) {
        char label[8];
        std::snprintf(label, sizeof(label), "%d", preset);
        if (ImGui::SmallButton(label)) items.quantity = preset;
        ImGui::SameLine(0, 6);
    }
    ImGui::NewLine();
    ImGui::PopFont();
    Theme::TextDim("Para");
    const bool others = Ok(s, Feature::ItemsForOthers);
    if (!others) items.recipient = -1;
    if (!others) ImGui::BeginDisabled();
    items.recipient = PlayerCombo("##recipient", s, items.recipient, false, -1, "Para mí (mi inventario)");
    if (!others) ImGui::EndDisabled();
    WhyTooltip(s, Feature::ItemsForOthers);
    ImGui::Dummy(ImVec2(0, 4));
    char spawnLabel[64];
    std::snprintf(spawnLabel, sizeof(spawnLabel), "%s  Generar ×%d", Icon::Plus, items.quantity);
    if (Theme::Button(spawnLabel, ImVec2(-1, ImGui::GetFrameHeight() * 1.3f), C::Accent, selected != nullptr))
        Spawn(s, c, selected->id, items.quantity, items.recipient);
    Theme::EndCard();

    Theme::BeginCard("##kits", Icon::Suitcase, "Kits rápidos", C::Amber);
    Theme::TextWrappedDim("Generan varios objetos de una vez en tu inventario. Los que no existan en tu versión se omiten.");
    for (size_t k = 0; k < Kits().size(); ++k) {
        const auto& kit = Kits()[k];
        ImGui::PushID(static_cast<int>(k));
        Theme::BeginRow(kit.icon, C::Amber, kit.name, kit.description, ControlWidth(70), true);
        if (Theme::Button("Dar", ImVec2(ControlWidth(70), 0), C::Amber)) {
            int sent = 0;
            for (const auto& [id, qty] : kit.items) {
                if (!FindItem(list, id)) continue;
                auto r = Make(Action::Spawn);
                r.id = id;
                r.count = qty;
                r.playerId = items.recipient;
                if (Bridge::Submit(r)) ++sent;
            }
            Theme::PushToast(sent ? "Kit «" + std::string(kit.name) + "» enviado al juego." : "Ningún objeto del kit existe en esta versión.",
                             sent ? Theme::ToastKind::Success : Theme::ToastKind::Warning);
        }
        Theme::EndRow();
        ImGui::PopID();
    }
    Theme::EndCard();
    ImGui::EndTable();
}

// ------------------------------------------------------------------------------------- Jugador

struct PlayerState {
    float speed = 2.0f, jump = 2.0f;
    bool initialized = false;
} player;

void PagePlayer(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    if (!player.initialized && s.speed != 1.0f) player.speed = s.speed;
    if (!player.initialized && s.jump != 1.0f) player.jump = s.jump;
    player.initialized = true;

    Theme::BeginCard("##survival", Icon::Heart, "Supervivencia", C::Pink);
    ToggleRow(s, Feature::GodMode, "god", Icon::Shield, C::Purple, "Modo dios",
              "Tu salud se mantiene llena y no puedes morir por daño. Solo afecta a tu personaje.", s.god,
              [](bool v) { auto r = Make(Action::GodMode); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::InfiniteStamina, "stamina", Icon::Bolt, C::Amber, "Resistencia infinita",
              "Tu resistencia se rellena constantemente: corre, esquiva y bloquea sin parar.", s.infiniteStamina,
              [](bool v) { auto r = Make(Action::InfiniteStamina); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::NoHunger, "hunger", Icon::Drumstick, C::Amber, "Sin hambre ni sed",
              "Mantiene llenas tu comida y tu agua mientras esté activo.", s.noHunger,
              [](bool v) { auto r = Make(Action::NoHunger); r.enabled = v; Send(r); });
    Theme::Separator();
    const float w4 = (ImGui::GetContentRegionAvail().x - 30) / 4;
    auto action = [&](const char* label, Feature f, Action a, ImU32 color, bool same) {
        if (same) ImGui::SameLine(0, 10);
        if (Theme::Button(label, ImVec2(w4, 0), color, Ok(s, f))) Send(Make(a));
        WhyTooltip(s, f);
    };
    action((std::string(Icon::Heart) + "  Curarme").c_str(), Feature::Heal, Action::Heal, C::Accent, false);
    action((std::string(Icon::Bolt) + "  Resistencia").c_str(), Feature::RestoreStamina, Action::RestoreStamina, C::Amber, true);
    action((std::string(Icon::Magic) + "  Todo").c_str(), Feature::FullRestore, Action::FullRestore, C::Accent, true);
    action((std::string(Icon::HandHeart) + "  Reanimarme").c_str(), Feature::Revive, Action::Revive, C::Purple, true);
    Theme::EndCard();

    Theme::BeginCard("##movement", Icon::Running, "Movimiento", C::Blue);
    {
        const bool ok = Ok(s, Feature::Speed);
        const float w = ControlWidth(260);
        Theme::BeginRow(Icon::Gauge, C::Blue, "Velocidad al correr y caminar",
                        "Multiplica tu velocidad en el suelo. 1× es la normal.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(66));
        if (ImGui::SliderFloat("##speed", &player.speed, 0.25f, 5.0f, "%.2f×") && s.speed != 1.0f) {}
        const bool released = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine(0, 10);
        bool on = Shown("speed", s.speed != 1.0f);
        if (Theme::Toggle("speed", &on, ok)) {
            SetPending("speed", on);
            auto r = Make(Action::Speed);
            r.enabled = on;
            r.value = on ? player.speed : 1.0f;
            Send(r);
        } else if (released && s.speed != 1.0f) {
            auto r = Make(Action::Speed);
            r.enabled = true;
            r.value = player.speed;
            Send(r);
        }
        WhyTooltip(s, Feature::Speed);
        Theme::EndRow();
    }
    {
        const bool ok = Ok(s, Feature::Jump);
        const float w = ControlWidth(260);
        Theme::BeginRow(Icon::ArrowUp, C::Blue, "Altura de salto", "Multiplica la fuerza de tu salto.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(66));
        ImGui::SliderFloat("##jump", &player.jump, 0.5f, 5.0f, "%.2f×");
        const bool released = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine(0, 10);
        bool on = Shown("jump", s.jump != 1.0f);
        if (Theme::Toggle("jump", &on, ok)) {
            SetPending("jump", on);
            auto r = Make(Action::Jump);
            r.enabled = on;
            r.value = on ? player.jump : 1.0f;
            Send(r);
        } else if (released && s.jump != 1.0f) {
            auto r = Make(Action::Jump);
            r.enabled = true;
            r.value = player.jump;
            Send(r);
        }
        WhyTooltip(s, Feature::Jump);
        Theme::EndRow();
    }
    ToggleRow(s, Feature::Fly, "fly", Icon::Feather, C::Blue, "Modo vuelo",
              "Vuela en la dirección en la que miras. Mantén Espacio para subir y Ctrl para bajar.", s.fly,
              [](bool v) { auto r = Make(Action::Fly); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::Fly, "noclip", Icon::Ghost, C::Purple, "Atravesar paredes",
              "Activa el vuelo y desactiva las colisiones de tu personaje. Desactívalo en un lugar abierto.", s.noclip,
              [](bool v) { auto r = Make(Action::Noclip); r.enabled = v; Send(r); });
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Daño

struct DamageState {
    int scope = 0;
    int player = -1;
    float outgoing = 2.0f, incoming = 0.5f;
} damage;

void PageDamage(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;

    Theme::BeginCard("##outgoing", Icon::Fist, "Daño causado", C::Red);
    Theme::TextDim("Aplicar a");
    Segmented("scope", &damage.scope, {"Solo yo", "Un jugador elegido", "Mundo / todos"}, ImGui::GetContentRegionAvail().x);
    const auto scope = static_cast<DamageScope>(damage.scope);
    const Feature feature = scope == DamageScope::World ? Feature::DamageWorld : Feature::DamagePersonal;
    bool enabled = false;
    if (scope == DamageScope::World) {
        enabled = s.outgoingEnabled;
    } else {
        const int id = scope == DamageScope::Self ? s.localPlayerId : damage.player;
        for (const auto& p : s.players)
            if (p.id == id) enabled = p.damageEnabled;
    }
    if (scope == DamageScope::Player) {
        ImGui::Dummy(ImVec2(0, 2));
        damage.player = PlayerCombo("##dmgplayer", s, damage.player, true, -1, "Selecciona un jugador");
    }
    ImGui::Dummy(ImVec2(0, 2));
    const bool ok = Ok(s, feature) && (scope != DamageScope::Player || damage.player >= 0);
    const float w = ControlWidth(260);
    Theme::BeginRow(Icon::Burst, C::Red, "Modificar daño causado",
                    "1 = normal; 2 = el doble; 0 = sin daño. Desactivar restaura ese alcance. Si activas un ajuste personal y otro del mundo, se multiplican.",
                    w, ok);
    ImGui::SetNextItemWidth(w - ControlWidth(66));
    ImGui::SliderFloat("##out", &damage.outgoing, 0.0f, 20.0f, "%.2f×", ImGuiSliderFlags_Logarithmic);
    const bool released = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, 10);
    const std::string key = "dmg" + std::to_string(damage.scope) + "_" + std::to_string(damage.player);
    bool on = Shown(key, enabled);
    auto send = [&](bool value) {
        auto r = Make(Action::OutgoingDamage);
        r.enabled = value;
        r.value = damage.outgoing;
        r.scope = scope;
        r.playerId = scope == DamageScope::Player ? damage.player : -1;
        Send(r);
    };
    if (Theme::Toggle(key.c_str(), &on, ok)) {
        SetPending(key, on);
        send(on);
    } else if (released && enabled) {
        send(true);
    }
    WhyTooltip(s, feature);
    Theme::EndRow();
    if (Presets("outp", &damage.outgoing, {0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f}) && enabled) send(true);
    Theme::EndCard();

    Theme::BeginCard("##incoming", Icon::Shield, "Daño de los enemigos · ajuste del mundo", C::Blue);
    {
        const bool okIn = Ok(s, Feature::DamageIncoming);
        Theme::BeginRow(Icon::Spider, C::Blue, "Modificar daño enemigo para todos",
                        "0,5 reduce el daño enemigo a la mitad; 0 anula ese daño. Este control afecta al mundo, también cuando arriba eliges «Solo yo».",
                        w, okIn);
        ImGui::SetNextItemWidth(w - ControlWidth(66));
        ImGui::SliderFloat("##in", &damage.incoming, 0.0f, 10.0f, "%.2f×");
        const bool rel = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine(0, 10);
        bool onIn = Shown("dmgin", s.incomingEnabled);
        auto sendIn = [&](bool value) {
            auto r = Make(Action::IncomingDamage);
            r.enabled = value;
            r.value = damage.incoming;
            Send(r);
        };
        if (Theme::Toggle("dmgin", &onIn, okIn)) {
            SetPending("dmgin", onIn);
            sendIn(onIn);
        } else if (rel && s.incomingEnabled) {
            sendIn(true);
        }
        WhyTooltip(s, Feature::DamageIncoming);
        Theme::EndRow();
        if (Presets("inp", &damage.incoming, {0.0f, 0.25f, 0.5f, 1.0f, 2.0f}) && s.incomingEnabled) sendIn(true);
    }
    Theme::TextWrappedDim("El daño ambiental (caídas, gas, calor o frío) tiene reglas distintas y puede seguir afectándote. Encontrarás esas reglas en Mundo.");
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Mutaciones

struct MutationState {
    char search[96] = {};
    int slots = 0;
    std::string pendingUnlock;
    int pendingTier = 0;
} mutations;

void PageMutations(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    if (!Ok(s, Feature::Mutations)) {
        EmptyState(Icon::Dna, "Mutaciones no disponibles", Why(s, Feature::Mutations).c_str());
        return;
    }
    if (mutations.slots < 2) mutations.slots = std::max(2, s.slots);

    Theme::BeginCard("##slots", Icon::Dna, "Ranuras de mutaciones", C::Purple);
    const float w = ControlWidth(300);
    Theme::BeginRow(Icon::List, C::Purple, "Ranuras simultáneas", "Entre 2 y 32. Para reducirlas, desactiva antes algunas mutaciones.", w, true);
    ImGui::SetNextItemWidth(w - ControlWidth(130));
    ImGui::SliderInt("##slots", &mutations.slots, 2, 32);
    ImGui::SameLine(0, 10);
    if (Theme::Button("Aplicar", ImVec2(ControlWidth(120), 0), C::Purple)) {
        auto r = Make(Action::MutationSlots);
        r.count = mutations.slots;
        Send(r);
    }
    Theme::EndRow();
    Theme::EndCard();

    Theme::BeginCard("##mutlist", Icon::List, "Tus mutaciones", C::Purple);
    Theme::SearchBox("mut", mutations.search, sizeof(mutations.search), "Buscar una mutación…", ImGui::GetContentRegionAvail().x);
    const std::wstring key = SearchKey(Wide(mutations.search));
    if (ImGui::BeginTable("##muts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Mutación", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Nivel", ImGuiTableColumnFlags_WidthFixed, ControlWidth(70));
        ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthFixed, ControlWidth(110));
        ImGui::TableSetupColumn("Acciones", ImGuiTableColumnFlags_WidthFixed, ControlWidth(290));
        ImGui::TableHeadersRow();
        for (const auto& m : s.mutations) {
            if (!key.empty() && SearchKey(m.name + L" " + Wide(m.id.c_str())).find(key) == std::wstring::npos) continue;
            ImGui::PushID(m.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Utf8(m.name).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d / %d", m.tier, m.maxTier);
            ImGui::TableNextColumn();
            if (m.tier < 1) Theme::Pill("Sin aprender", C::TextDim);
            else if (m.equipped) Theme::Pill("Activa", C::Accent);
            else Theme::Pill("Inactiva", C::Amber);
            ImGui::TableNextColumn();
            if (m.tier >= 1) {
                if (Theme::Button(m.equipped ? "Desactivar" : "Activar", ImVec2(ControlWidth(120), 0), m.equipped ? C::Amber : C::Accent)) {
                    auto r = Make(m.equipped ? Action::UnequipMutation : Action::EquipMutation);
                    r.id = m.id;
                    Send(r);
                }
                ImGui::SameLine(0, 8);
            }
            if (m.tier < m.maxTier) {
                char label[48];
                std::snprintf(label, sizeof(label), "Desbloquear nivel %d", m.tier + 1);
                if (Theme::GhostButton(label, ImVec2(ControlWidth(160), 0))) {
                    mutations.pendingUnlock = m.id;
                    mutations.pendingTier = m.tier + 1;
                    Theme::OpenConfirm("¿Desbloquear nivel?");
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (s.mutations.empty()) Theme::TextWrappedDim("No se encontraron mutaciones en esta partida.");
    Theme::EndCard();

    if (Theme::Confirm("¿Desbloquear nivel?", "Desbloquear nivel",
                       "Este nivel quedará aprendido en tu partida. Desactivar la mutación no elimina el aprendizaje. ¿Desbloquearlo?",
                       "Desbloquear", C::Purple) == 1) {
        auto r = Make(Action::UnlockMutation);
        r.id = mutations.pendingUnlock;
        r.count = mutations.pendingTier;
        Send(r);
    }
}

// ------------------------------------------------------------------------------------- Construcción

float buildingHealth = 2.0f;

void PageBuild(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    Theme::BeginCard("##craft", Icon::Hammer2, "Fabricar y construir", C::Accent);
    ToggleRow(s, Feature::FreeCraft, "craft", Icon::Hammer2, C::Accent, "Fabricar recetas aprendidas sin recursos",
              "Usa el menú de fabricación normal. Conserva las recetas que ya conoces y los requisitos de estaciones de trabajo.",
              s.freeCraft, [](bool v) { auto r = Make(Action::FreeCraft); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::FreeBuild, "build", Icon::Hammer, C::Accent, "Construir sin recursos",
              "Los nuevos planos se completan sin entregar materiales. Solo utiliza los planos que ya tienes disponibles.",
              s.freeBuild, [](bool v) { auto r = Make(Action::FreeBuild); r.enabled = v; Send(r); });
    ToggleRow(s, Feature::BuildAnywhere, "anywhere", Icon::Magic, C::Purple, "Construir sin restricciones de colocación",
              "Permite colocar construcciones donde normalmente se bloquean. Es independiente de construir sin recursos.",
              s.anywhere, [](bool v) { auto r = Make(Action::BuildAnywhere); r.enabled = v; Send(r); });
    if (ButtonRow(s, Feature::FinishBlueprints, Icon::CircleCheck, C::Accent, "Completar los planos ya colocados",
                  "Completa las construcciones pendientes del mundo.", "Completar"))
        Send(Make(Action::FinishBlueprints));
    Theme::EndCard();

    Theme::BeginCard("##buildrules", Icon::Campground, "Reglas de construcción del mundo", C::Amber);
    ToggleRow(s, Feature::BuildingRules, "integrity", Icon::Cubes, C::Amber, "Integridad estructural",
              "Si la desactivas, las construcciones no necesitan apoyos para mantenerse en pie.", s.buildingIntegrity,
              [](bool v) { auto r = Make(Action::BuildingIntegrity); r.enabled = v; Send(r); });
    {
        const bool ok = Ok(s, Feature::BuildingRules);
        const float w = ControlWidth(300);
        Theme::BeginRow(Icon::Shield, C::Amber, "Resistencia de las construcciones",
                        "Multiplica la salud de muros, suelos y defensas frente a los ataques.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(130));
        ImGui::SliderFloat("##bh", &buildingHealth, 0.1f, 100.0f, "%.1f×", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine(0, 10);
        if (Theme::Button("Aplicar", ImVec2(ControlWidth(120), 0), C::Amber, ok)) {
            auto r = Make(Action::BuildingHealth);
            r.value = buildingHealth;
            Send(r);
        }
        WhyTooltip(s, Feature::BuildingRules);
        Theme::EndRow();
        char current[64];
        std::snprintf(current, sizeof(current), "Valor actual: %.2f×", s.buildingHealth);
        ImGui::PushFont(Theme::GetFonts().small);
        Theme::TextDim(current);
        ImGui::PopFont();
    }
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Recogida

void PagePickup(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    Theme::BeginCard("##pickup", Icon::Magnet, "Recogida automática", C::Blue);
    const float radius = c.settings.pickupRadius;
    ToggleRow(s, Feature::AutoPickup, "autopickup", Icon::Magnet, C::Blue, "Recoger automáticamente los objetos cercanos",
              "Solo objetos sueltos visibles que puedan entrar en tu mochila. Si no caben, quedan en el suelo. No recoge a través de obstáculos.",
              s.autoPickup, [radius](bool v) { auto r = Make(Action::AutoPickup); r.enabled = v; r.value = radius; Send(r); });
    Theme::TextDim("Radio de recogida (unidades del juego)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##pr", &c.settings.pickupRadius, kMinRadius, kMaxRadius, "%.0f")) c.settingsChanged = true;
    if (ImGui::IsItemDeactivatedAfterEdit() && s.autoPickup) {
        auto r = Make(Action::AutoPickup);
        r.enabled = true;
        r.value = c.settings.pickupRadius;
        Send(r);
    }
    char collected[96];
    std::snprintf(collected, sizeof(collected), "Recogidos automáticamente: %llu objetos/pilas en esta sesión.",
                  static_cast<unsigned long long>(s.collected));
    Theme::TextDim(collected);
    Theme::EndCard();

    Theme::BeginCard("##clear", Icon::Broom, "Eliminar objetos cercanos", C::Red);
    Theme::TextWrappedDim("Afecta a los objetos sueltos visibles de todos, incluidos tablones. Excluye objetos equipados, sujetos, en proceso y de misión. Es permanente.");
    Theme::TextDim("Radio de eliminación (unidades del juego)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##cr", &c.settings.clearRadius, kMinRadius, kMaxRadius, "%.0f")) c.settingsChanged = true;
    if (Theme::Button((std::string(Icon::Trash) + "  Eliminar objetos cercanos…").c_str(), ImVec2(-1, 0), C::Red, Ok(s, Feature::ClearNearby)))
        Theme::OpenConfirm("¿Eliminar objetos?");
    WhyTooltip(s, Feature::ClearNearby);
    Theme::TextDim("Radios permitidos: de 25 a 2000 unidades del juego.");
    Theme::EndCard();
    char text[256];
    std::snprintf(text, sizeof(text),
                  "¿Eliminar los objetos sueltos visibles dentro de %.0f unidades de tu posición? Afecta a los objetos de todos y no se puede deshacer.",
                  c.settings.clearRadius);
    if (Theme::Confirm("¿Eliminar objetos?", "Eliminar objetos cercanos", text, "Eliminar") == 1) {
        auto r = Make(Action::ClearNearby);
        r.value = c.settings.clearRadius;
        Send(r);
    }
}

// ------------------------------------------------------------------------------------- Mundo

struct WorldState {
    int hour = 8;
    bool lock = false;
    float timeSpeed = 1.0f;
    std::unordered_map<std::string, float> ruleValues;
} world;

void PageWorld(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;

    Theme::BeginCard("##time", Icon::Sun, "Hora y tiempo", C::Amber);
    {
        const bool ok = Ok(s, Feature::SetTime);
        const float w = ControlWidth(420);
        Theme::BeginRow(world.hour >= 7 && world.hour < 20 ? Icon::Sun : Icon::Moon, C::Amber, "Fijar la hora del día",
                        "Elige la hora y, si quieres, déjala bloqueada.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(230));
        ImGui::SliderInt("##hour", &world.hour, 0, 23, "%02d:00");
        ImGui::SameLine(0, 10);
        ImGui::Checkbox("Bloquear", &world.lock);
        ImGui::SameLine(0, 10);
        if (Theme::Button("Fijar", ImVec2(ControlWidth(90), 0), C::Amber, ok)) {
            auto r = Make(Action::SetTime);
            r.count = world.hour;
            r.enabled = world.lock;
            Send(r);
        }
        WhyTooltip(s, Feature::SetTime);
        Theme::EndRow();
    }
    {
        const bool ok = Ok(s, Feature::AdvanceTime);
        const float w = ControlWidth(260);
        Theme::BeginRow(Icon::Forward, C::Amber, "Avanzar el tiempo", "Adelanta el reloj del mundo.", w, ok);
        const float half = (w - 10) * 0.5f;
        auto r1 = Make(Action::AdvanceHours);
        r1.count = 1;
        if (Theme::Button("+1 hora", ImVec2(half, 0), C::Amber, ok)) Send(r1);
        ImGui::SameLine(0, 10);
        auto r6 = Make(Action::AdvanceHours);
        r6.count = 6;
        if (Theme::Button("+6 horas", ImVec2(half, 0), C::Amber, ok)) Send(r6);
        WhyTooltip(s, Feature::AdvanceTime);
        Theme::EndRow();
    }
    {
        const bool ok = Ok(s, Feature::TimeSpeed);
        const float w = ControlWidth(300);
        Theme::BeginRow(Icon::Clock, C::Amber, "Velocidad del tiempo", "1× es la normal; 0× detiene el reloj del día.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(130));
        ImGui::SliderFloat("##ts", &world.timeSpeed, 0.0f, 20.0f, "%.2f×");
        ImGui::SameLine(0, 10);
        if (Theme::Button("Aplicar", ImVec2(ControlWidth(120), 0), C::Amber, ok)) {
            auto r = Make(Action::TimeSpeed);
            r.value = world.timeSpeed;
            Send(r);
        }
        WhyTooltip(s, Feature::TimeSpeed);
        Theme::EndRow();
    }
    Theme::EndCard();

    Theme::BeginCard("##worldactions", Icon::Earth, "Acciones del mundo", C::Accent);
    if (ButtonRow(s, Feature::RespawnResources, Icon::Tree, C::Accent, "Regenerar recursos",
                  "Vuelve a hacer crecer hierba, plantas y recursos recolectados del mapa.", "Regenerar"))
        Send(Make(Action::RespawnResources));
    if (ButtonRow(s, Feature::EndRaid, Icon::Bomb, C::Red, "Terminar el asedio actual",
                  "Acaba la oleada de criaturas que ataca tu base y reinicia su nivel de amenaza.", "Terminar", C::Red))
        Send(Make(Action::EndRaid));
    if (ButtonRow(s, Feature::ToggleHud, Icon::Eye, C::Blue, "Mostrar u ocultar la interfaz del juego",
                  "Útil para capturas de pantalla.", "Alternar", C::Blue))
        Send(Make(Action::ToggleHud));
    Theme::EndCard();

    Theme::BeginCard("##rules", Icon::List, "Reglas del mundo", C::Purple);
    if (!Ok(s, Feature::WorldRules)) {
        Theme::TextWrappedDim(Why(s, Feature::WorldRules).c_str());
    } else {
        Theme::TextWrappedDim("Son las opciones de partida personalizada del juego. Se leen de tu versión, así que pueden aparecer reglas nuevas. Las marcadas en ámbar difieren del valor con el que empezó la sesión.");
        if (ImGui::BeginTable("##rulestable", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (const auto& rule : s.rules) {
                ImGui::TableNextColumn();
                ImGui::PushID(rule.id.c_str());
                const std::string label = Utf8(rule.label);
                if (rule.isBool) {
                    Theme::BeginRow(nullptr, rule.changed ? C::Amber : C::Purple, label.c_str(), nullptr, ControlWidth(56), true);
                    bool value = Shown("rule_" + rule.id, rule.boolValue);
                    if (Theme::Toggle("##v", &value)) {
                        SetPending("rule_" + rule.id, value);
                        auto r = Make(Action::SetRule);
                        r.id = rule.id;
                        r.enabled = value;
                        r.value = value ? 1.0f : 0.0f;
                        Send(r);
                    }
                    Theme::EndRow();
                } else {
                    const float w = ControlWidth(170);
                    Theme::BeginRow(nullptr, rule.changed ? C::Amber : C::Purple, label.c_str(), nullptr, w, true);
                    auto it = world.ruleValues.find(rule.id);
                    float value = it != world.ruleValues.end() ? it->second : rule.floatValue;
                    ImGui::SetNextItemWidth(w);
                    if (ImGui::SliderFloat("##f", &value, 0.0f, 10.0f, "%.2f×")) world.ruleValues[rule.id] = value;
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto r = Make(Action::SetRule);
                        r.id = rule.id;
                        r.value = value;
                        Send(r);
                        world.ruleValues.erase(rule.id);
                    }
                    Theme::EndRow();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (Theme::GhostButton((std::string(Icon::Undo) + "  Restaurar reglas del inicio de la sesión").c_str()))
            Send(Make(Action::RestoreRules));
    }
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Teletransporte

struct TeleportState {
    char name[64] = {};
    float forward = 500;
    int rename = -1;
} teleport;

void PageTeleport(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    const bool ok = Ok(s, Feature::Teleport);

    Theme::BeginCard("##pos", Icon::Crosshairs, "Tu posición", C::Blue);
    if (s.hasPosition) {
        ImGui::PushFont(Theme::GetFonts().title);
        ImGui::Text("X %.0f   Y %.0f   Z %.0f", s.x, s.y, s.z);
        ImGui::PopFont();
    } else {
        Theme::TextDim("Posición no disponible.");
    }
    const float third = (ImGui::GetContentRegionAvail().x - 20) / 3;
    if (Theme::Button((std::string(Icon::Undo) + "  Volver atrás").c_str(), ImVec2(third, 0), C::Blue, ok && s.hasBackPosition))
        Send(Make(Action::TeleportBack));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) Theme::Tooltip("Regresa al punto anterior a tu último teletransporte.");
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(third);
    ImGui::SliderFloat("##fwd", &teleport.forward, 100, 3000, "%.0f u");
    ImGui::SameLine(0, 10);
    if (Theme::Button((std::string(Icon::ArrowRight) + "  Avanzar").c_str(), ImVec2(third, 0), C::Blue, ok)) {
        auto r = Make(Action::TeleportForward);
        r.value = teleport.forward;
        Send(r);
    }
    WhyTooltip(s, Feature::Teleport);
    Theme::EndCard();

    Theme::BeginCard("##waypoints", Icon::MapLocation, "Puntos guardados", C::Accent);
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail - ControlWidth(190));
    ImGui::InputTextWithHint("##wpname", "Nombre del punto (por ejemplo: Base, Laboratorio…)", teleport.name, sizeof(teleport.name));
    ImGui::SameLine(0, 10);
    if (Theme::Button((std::string(Icon::Save) + "  Guardar aquí").c_str(), ImVec2(-1, 0), C::Accent, s.hasPosition)) {
        Settings::Waypoint w;
        w.name = teleport.name[0] ? Paths::Wide(teleport.name) : L"Punto " + std::to_wstring(c.settings.waypoints.size() + 1);
        w.x = s.x;
        w.y = s.y;
        w.z = s.z;
        c.settings.waypoints.push_back(w);
        c.settingsChanged = true;
        teleport.name[0] = 0;
        Theme::PushToast("Posición guardada: " + Utf8(w.name), Theme::ToastKind::Success);
    }
    if (c.settings.waypoints.empty()) {
        Theme::TextWrappedDim("Guarda lugares importantes (tu base, laboratorios, zonas de recursos) para volver a ellos con un clic. Se guardan en tu equipo y sirven en cualquier partida del mismo mapa.");
    }
    int remove = -1;
    for (int i = 0; i < static_cast<int>(c.settings.waypoints.size()); ++i) {
        const auto& w = c.settings.waypoints[i];
        ImGui::PushID(i);
        char coords[96];
        std::snprintf(coords, sizeof(coords), "X %.0f · Y %.0f · Z %.0f", w.x, w.y, w.z);
        Theme::BeginRow(Icon::LocationDot, C::Accent, Utf8(w.name).c_str(), coords, ControlWidth(170), true);
        if (Theme::Button("Ir", ImVec2(ControlWidth(110), 0), C::Accent, ok)) {
            auto r = Make(Action::TeleportTo);
            r.x = w.x;
            r.y = w.y;
            r.z = w.z;
            Send(r);
        }
        ImGui::SameLine(0, 10);
        if (Theme::IconButton(Icon::Trash, "del", "Borrar este punto", C::Red)) remove = i;
        Theme::EndRow();
        ImGui::PopID();
    }
    if (remove >= 0) {
        c.settings.waypoints.erase(c.settings.waypoints.begin() + remove);
        c.settingsChanged = true;
    }
    Theme::EndCard();

    Theme::BeginCard("##players", Icon::Users, "Jugadores", C::Purple);
    int others = 0;
    for (const auto& p : s.players) {
        if (p.local) continue;
        ++others;
        ImGui::PushID(p.id);
        Theme::BeginRow(Icon::User, C::Purple, Utf8(p.name).c_str(), p.available ? "En la partida" : "Sin personaje cargado",
                        ControlWidth(250), ok && p.available);
        const float half = (ControlWidth(250) - 10) * 0.5f;
        auto go = Make(Action::TeleportToPlayer);
        go.playerId = p.id;
        if (Theme::Button("Ir con él/ella", ImVec2(half, 0), C::Purple, ok && p.available)) Send(go);
        ImGui::SameLine(0, 10);
        auto bring = Make(Action::BringPlayer);
        bring.playerId = p.id;
        if (Theme::Button("Traer", ImVec2(half, 0), C::Purple, ok && p.available)) Send(bring);
        Theme::EndRow();
        ImGui::PopID();
    }
    if (!others) Theme::TextWrappedDim("No hay otros jugadores en la partida.");
    if (ButtonRow(s, Feature::TeleportParty, Icon::PeopleGroup, C::Purple, "Traer a todo el grupo",
                  "Teletransporta a todos los jugadores a tu posición.", "Traer a todos", C::Purple))
        Send(Make(Action::BringParty));
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Criaturas

struct Mount {
    const char* id;
    const char* name;
    const char* icon;
};
const Mount kMounts[] = {
    {"RedSoldierAnt", "Hormiga soldado roja", Icon::Bug},
    {"BlackSoldierAnt", "Hormiga soldado negra", Icon::Bug},
    {"Ladybug", "Mariquita", Icon::Bug},
    {"DivingBellSpider", "Araña campana de buceo", Icon::Spider},
    {"Orbweaver", "Araña tejedora", Icon::Spider},
    {"ToeBiter", "Chinche muerdededos", Icon::Bug},
};
int mountTier[6] = {1, 1, 1, 1, 1, 1};
int arachnophobia = 0;

void PageCreatures(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    Theme::BeginCard("##pets", Icon::Paw, "Mascotas y monturas", C::Amber);
    if (ButtonRow(s, Feature::TameNearest, Icon::HandHeart, C::Amber, "Domar la criatura más cercana",
                  "Convierte en mascota a la criatura domable más próxima.", "Domar", C::Amber))
        Send(Make(Action::TameNearest));
    if (ButtonRow(s, Feature::ReviveMount, Icon::Horse, C::Amber, "Reanimar tu montura", "Revive a tu montura si ha caído.",
                  "Reanimar", C::Amber))
        Send(Make(Action::ReviveMount));
    Theme::EndCard();

    Theme::BeginCard("##mounts", Icon::Horse, "Generar y montar", C::Accent);
    Theme::TextWrappedDim("Aparece una montura del nivel elegido y te subes a ella. Úsalo en un lugar despejado.");
    const bool ok = Ok(s, Feature::SpawnMount);
    for (int i = 0; i < 6; ++i) {
        ImGui::PushID(i);
        const float w = ControlWidth(280);
        Theme::BeginRow(kMounts[i].icon, C::Accent, kMounts[i].name, nullptr, w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(130));
        ImGui::SliderInt("##tier", &mountTier[i], 1, 3, "Nivel %d");
        ImGui::SameLine(0, 10);
        if (Theme::Button("Montar", ImVec2(ControlWidth(120), 0), C::Accent, ok)) {
            auto r = Make(Action::SpawnMount);
            r.id = kMounts[i].id;
            r.count = mountTier[i];
            Send(r);
        }
        WhyTooltip(s, Feature::SpawnMount);
        Theme::EndRow();
        ImGui::PopID();
    }
    Theme::EndCard();

    Theme::BeginCard("##arachno", Icon::Spider, "Aracnofobia", C::Purple);
    {
        const bool okA = Ok(s, Feature::Arachnophobia);
        const float w = ControlWidth(300);
        Theme::BeginRow(Icon::EyeSlash, C::Purple, "Modo seguro de aracnofobia",
                        "0 lo desactiva; los niveles superiores hacen las arañas menos realistas.", w, okA);
        ImGui::SetNextItemWidth(w - ControlWidth(130));
        ImGui::SliderInt("##arach", &arachnophobia, 0, 3);
        ImGui::SameLine(0, 10);
        if (Theme::Button("Aplicar", ImVec2(ControlWidth(120), 0), C::Purple, okA)) {
            auto r = Make(Action::Arachnophobia);
            r.count = arachnophobia;
            Send(r);
        }
        WhyTooltip(s, Feature::Arachnophobia);
        Theme::EndRow();
    }
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Progreso

struct ProgressState {
    int white = 1, gold = 1, buggy = 1, science = 1000, omni = 1;
    Action pendingUnlock = Action::Refresh;
    const char* pendingName = "";
} progress;

void AmountRow(const Snapshot& s, Feature f, const char* icon, ImU32 color, const char* title, const char* desc, int* value,
               int max, Action action) {
    const bool ok = Ok(s, f);
    const float w = ControlWidth(300);
    Theme::BeginRow(icon, color, title, desc, w, ok);
    ImGui::SetNextItemWidth(w - ControlWidth(130));
    ImGui::InputInt("##amount", value, 1, 100);
    *value = std::clamp(*value, 1, max);
    ImGui::SameLine(0, 10);
    if (Theme::Button("Añadir", ImVec2(ControlWidth(120), 0), color, ok)) {
        auto r = Make(action);
        r.count = *value;
        Send(r);
    }
    WhyTooltip(s, f);
    Theme::EndRow();
}

void PageProgress(Context& c) {
    const auto& s = c.snapshot;
    if (!RequireSession(s)) return;
    Theme::BeginCard("##points", Icon::Tooth, "Molares y puntos", C::Amber);
    ImGui::PushID("white");
    AmountRow(s, Feature::WhiteMolars, Icon::Tooth, C::Text, "Molares normales", "Mejoras personales.", &progress.white, 999, Action::AddWhiteMolars);
    ImGui::PopID();
    ImGui::PushID("gold");
    AmountRow(s, Feature::GoldMolars, Icon::Tooth, C::Amber, "Molares dorados (grupo)", "Mejoras del grupo.", &progress.gold, 999, Action::AddGoldMolars);
    ImGui::PopID();
    ImGui::PushID("science");
    AmountRow(s, Feature::Science, Icon::Flask, C::Blue, "Ciencia (mundo)", "Para comprar recetas en el laboratorio.", &progress.science, 1000000, Action::AddScience);
    ImGui::PopID();
    ImGui::PushID("buggy");
    AmountRow(s, Feature::BuggyPoints, Icon::Horse, C::Accent, "Puntos de montura", "Mejoras de tus monturas.", &progress.buggy, 999, Action::AddBuggyPoints);
    ImGui::PopID();
    Theme::TextWrappedDim("Los molares, puntos y ciencia pueden guardarse en la partida. Restaurar ajustes no revierte estos cambios. Molares y mejoras usan los puntos internos del juego, no generan piezas físicas en el inventario.");
    Theme::EndCard();

    Theme::BeginCard("##unlocks", Icon::Unlock, "Desbloqueos permanentes", C::Purple);
    Theme::TextWrappedDim("Estos cambios se guardan en tu partida y no se pueden deshacer desde el panel. Haz una copia de seguridad antes (el iniciador lo hace por ti).");
    auto unlock = [&](Feature f, const char* icon, const char* title, const char* desc, Action action) {
        if (ButtonRow(s, f, icon, C::Purple, title, desc, "Desbloquear", C::Purple)) {
            progress.pendingUnlock = action;
            progress.pendingName = title;
            if (ConfirmPermanent(c, "¿Desbloquear?")) Send(Make(action));
        }
    };
    unlock(Feature::UnlockRecipes, Icon::Hammer2, "Todas las recetas", "Incluye recetas que todavía no has descubierto.", Action::UnlockRecipes);
    unlock(Feature::UnlockTechTrees, Icon::Flask, "Árboles tecnológicos", "Desbloquea los árboles del laboratorio.", Action::UnlockTechTrees);
    unlock(Feature::UnlockScabs, Icon::Palette, "Temas de color de la SCA.B", "Todos los fondos y colores del reloj.", Action::UnlockScabs);
    unlock(Feature::UnlockLandmarks, Icon::Map, "Puntos de interés y mapa", "Descubre los lugares importantes y revela el mapa.", Action::UnlockLandmarks);
    {
        const bool ok = Ok(s, Feature::OmniTool);
        const float w = ControlWidth(300);
        Theme::BeginRow(Icon::Wrench, C::Purple, "Nivel de la omniherramienta", "Mejora tu herramienta hasta el nivel elegido.", w, ok);
        ImGui::SetNextItemWidth(w - ControlWidth(130));
        ImGui::SliderInt("##omni", &progress.omni, 1, 6, "Nivel %d");
        ImGui::SameLine(0, 10);
        if (Theme::Button("Fijar", ImVec2(ControlWidth(120), 0), C::Purple, ok)) {
            auto r = Make(Action::OmniToolTier);
            r.count = progress.omni;
            Send(r);
        }
        WhyTooltip(s, Feature::OmniTool);
        Theme::EndRow();
    }
    Theme::EndCard();

    Theme::BeginCard("##gear", Icon::Wrench, "Equipo", C::Accent);
    if (ButtonRow(s, Feature::RepairItems, Icon::Wrench, C::Accent, "Reparar todo el equipo", "Restaura la durabilidad de tus objetos.", "Reparar"))
        Send(Make(Action::RepairItems));
    if (ButtonRow(s, Feature::RecoverBackpacks, Icon::Suitcase, C::Accent, "Recuperar mochilas",
                  "Trae las mochilas que dejaste al morir.", "Recuperar"))
        Send(Make(Action::RecoverBackpacks));
    Theme::EndCard();

    std::string text = std::string("«") + progress.pendingName + "» quedará desbloqueado en tu partida de forma permanente. ¿Continuar?";
    if (Theme::Confirm("¿Desbloquear?", "Desbloqueo permanente", text.c_str(), "Desbloquear", C::Purple) == 1)
        Send(Make(progress.pendingUnlock));
}

// ------------------------------------------------------------------------------------- Ajustes

struct KeyOption {
    int vk;
    const char* name;
};
const KeyOption kKeys[] = {
    {0x70, "F1"}, {0x71, "F2"}, {0x72, "F3"}, {0x73, "F4"}, {0x74, "F5"}, {0x75, "F6"}, {0x76, "F7"}, {0x77, "F8"},
    {0x78, "F9"}, {0x79, "F10"}, {0x7A, "F11"}, {0x7B, "F12"}, {0x2D, "Insert"}, {0x24, "Inicio"}, {0x23, "Fin"},
    {0x21, "Re Pág"}, {0x22, "Av Pág"}, {0x13, "Pausa"}, {0x91, "Bloq Despl"},
};

float pendingScale = 0;

void PageOptions(Context& c) {
    const auto& s = c.snapshot;
    Theme::BeginCard("##panelopts", Icon::Gear, "Panel", C::Accent);
    {
        const float w = ControlWidth(160);
        Theme::BeginRow(Icon::Keyboard, C::Accent, "Tecla para mostrar u ocultar el panel",
                        "Funciona mientras el juego o el panel están en primer plano. La X de la ventana también lo oculta.", w, true);
        const char* current = "?";
        for (const auto& k : kKeys)
            if (k.vk == c.settings.hotkey) current = k.name;
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("##key", current)) {
            for (const auto& k : kKeys)
                if (ImGui::Selectable(k.name, k.vk == c.settings.hotkey)) {
                    c.settings.hotkey = k.vk;
                    c.settingsChanged = true;
                }
            ImGui::EndCombo();
        }
        Theme::EndRow();
    }
    {
        Theme::BeginRow(Icon::ArrowUp, C::Accent, "Mantener siempre encima", "Útil con el juego en ventana sin bordes.", ControlWidth(56), true);
        bool topmost = c.settings.alwaysOnTop;
        if (Theme::Toggle("##top", &topmost)) {
            c.settings.alwaysOnTop = topmost;
            c.settingsChanged = true;
            c.window.SetTopmost(topmost);
        }
        Theme::EndRow();
    }
    {
        const float w = ControlWidth(220);
        Theme::BeginRow(Icon::Contrast, C::Accent, "Opacidad de la ventana", nullptr, w, true);
        int opacity = static_cast<int>(c.settings.opacity * 100 + 0.5f);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderInt("##op", &opacity, 50, 100, "%d %%")) {
            c.settings.opacity = opacity / 100.0f;
            c.settingsChanged = true;
            c.window.SetOpacity(c.settings.opacity);
        }
        Theme::EndRow();
    }
    {
        const float w = ControlWidth(220);
        Theme::BeginRow(Icon::Search, C::Accent, "Tamaño de la interfaz", "Se aplica al soltar el control.", w, true);
        if (pendingScale <= 0) pendingScale = c.settings.uiScale;
        int percent = static_cast<int>(pendingScale * 100 + 0.5f);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderInt("##scale", &percent, 80, 160, "%d %%")) pendingScale = percent / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingScale != c.settings.uiScale) {
            c.settings.uiScale = pendingScale;
            c.settingsChanged = true;
            c.scaleChanged = true;
        }
        Theme::EndRow();
    }
    {
        Theme::BeginRow(Icon::Lock, C::Accent, "Confirmar los desbloqueos permanentes", nullptr, ControlWidth(56), true);
        bool confirm = c.settings.confirmPermanent;
        if (Theme::Toggle("##confirm", &confirm)) {
            c.settings.confirmPermanent = confirm;
            c.settingsChanged = true;
        }
        Theme::EndRow();
    }
    Theme::EndCard();

    Theme::BeginCard("##session", Icon::Undo, "Sesión de juego", C::Amber);
    if (ButtonRow(s, Feature::WorldRules, Icon::Undo, C::Amber, "Restaurar ajustes de sesión",
                  "Devuelve a su valor inicial fabricación y construcción gratis, colocación libre, daño, reglas del mundo, ranuras de mutaciones, velocidad, salto, vuelo, modo dios y recogida automática. No borra objetos ni revierte puntos, ciencia, hora, desbloqueos o eliminaciones.",
                  "Restaurar", C::Amber))
        Send(Make(Action::Restore));
    Theme::EndCard();

    Theme::BeginCard("##files", Icon::FolderOpen, "Archivos", C::Blue);
    Theme::TextWrappedDim("El registro, tus ajustes y tus puntos guardados están en tu carpeta de usuario (no en la carpeta del juego), así Vortex no los toca.");
    const std::string dir = Utf8(Paths::DataDir());
    ImGui::PushFont(Theme::GetFonts().small);
    Theme::TextDim(dir.c_str());
    ImGui::PopFont();
    if (Theme::GhostButton((std::string(Icon::FolderOpen) + "  Abrir carpeta").c_str())) {
        ShellExecuteW(nullptr, L"open", Paths::DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine(0, 10);
    if (Theme::GhostButton((std::string(Icon::FileLines) + "  Abrir registro").c_str())) {
        const auto file = Log::FilePath();
        if (!file.empty()) ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    Theme::EndCard();

    Theme::BeginCard("##compatdetail", Icon::Seedling, "Compatibilidad", C::Accent);
    ImGui::Text("Grounded 2: %s", s.gameVersion.empty() ? "versión no detectada" : s.gameVersion.c_str());
    ImGui::Text("Motor: Unreal Engine %s", s.engineVersion.empty() ? "?" : s.engineVersion.c_str());
    ImGui::Text("Estructuras del motor: %s", s.resolved ? "localizadas automáticamente" : "buscando…");
    if (!s.ready) {
        Theme::TextWrappedDim("La comprobación de cada función se hace al entrar en una partida.");
    } else {
        int missing = 0;
        for (const auto& f : s.features) missing += f.ok ? 0 : 1;
        if (!missing) {
            Theme::Pill((std::string(Icon::CircleCheck) + "  Todas las funciones verificadas").c_str(), C::Accent);
        } else {
            Theme::TextWrappedDim("Estas comprobaciones fallaron en tu versión; sus funciones están desactivadas para no dañar la partida:");
            for (const auto& note : s.compatibilityNotes) {
                ImGui::PushFont(Theme::GetFonts().small);
                ImGui::BulletText("%s", Utf8(note).c_str());
                ImGui::PopFont();
            }
        }
    }
    Theme::EndCard();

    Theme::BeginCard("##about", Icon::Info, "Acerca de", C::TextDim);
    ImGui::Text("Grounded 2 · Panel en español %s", G2P_VERSION);
    Theme::TextWrappedDim("Basado en Grounded2Minimal de x0reaxeax (GPL-3.0). SDK generado con Dumper-7. Interfaz con Dear ImGui. Fuentes Nunito y Fredoka (OFL) e iconos Font Awesome Free. Licencia GPL-3.0-or-later; el código fuente se incluye en el paquete.");
    Theme::TextWrappedDim("Haz copias de seguridad de tus partidas. Solo el anfitrión puede usar el panel en multijugador.");
    Theme::EndCard();
}

// ------------------------------------------------------------------------------------- Registro

bool autoScroll = true;

void PageLog(Context&) {
    Theme::BeginCard("##logcard", Icon::Scroll, "Registro", C::TextDim);
    uint64_t revision = 0;
    auto lines = Log::Tail(400, &revision);
    if (Theme::GhostButton((std::string(Icon::Copy) + "  Copiar todo").c_str())) {
        std::string all;
        for (const auto& l : lines) all += l.text + "\n";
        ImGui::SetClipboardText(all.c_str());
        Theme::PushToast("Registro copiado al portapapeles.", Theme::ToastKind::Success);
    }
    ImGui::SameLine(0, 10);
    ImGui::Checkbox("Desplazar automáticamente", &autoScroll);
    const float height = std::max(200.0f, ImGui::GetMainViewport()->WorkSize.y - ImGui::GetCursorScreenPos().y - 40);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec(IM_COL32(10, 15, 12, 255)));
    ImGui::BeginChild("##logtext", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(Theme::GetFonts().small);
    for (const auto& line : lines) {
        ImU32 color = C::Text;
        if (line.level == Log::Level::Warn) color = C::Amber;
        else if (line.level == Log::Level::Error) color = C::Red;
        else if (line.level == Log::Level::Debug) color = C::TextDim;
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec(color));
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    static uint64_t lastRevision = 0;
    if (autoScroll && revision != lastRevision) ImGui::SetScrollHereY(1.0f);
    lastRevision = revision;
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Theme::EndCard();
}

} // namespace

// ------------------------------------------------------------------------------------- public

std::string Utf8(const std::wstring& text) { return Paths::Utf8(text); }

void Send(Request request) {
    if (!Bridge::Submit(std::move(request)))
        Theme::PushToast("El panel está ocupado o el valor no es válido. Inténtalo de nuevo.", Theme::ToastKind::Warning);
}

const char* IconOf(Page page) {
    switch (page) {
    case Page::Home: return Icon::Home;
    case Page::Items: return Icon::BoxOpen;
    case Page::Player: return Icon::User;
    case Page::Damage: return Icon::Fist;
    case Page::Mutations: return Icon::Dna;
    case Page::Build: return Icon::Hammer;
    case Page::Pickup: return Icon::Magnet;
    case Page::World: return Icon::Earth;
    case Page::Teleport: return Icon::MapLocation;
    case Page::Creatures: return Icon::Paw;
    case Page::Progress: return Icon::Tooth;
    case Page::Options: return Icon::Gear;
    case Page::Log: return Icon::Scroll;
    default: return Icon::Question;
    }
}

const char* LabelOf(Page page) {
    switch (page) {
    case Page::Home: return "Inicio";
    case Page::Items: return "Objetos";
    case Page::Player: return "Jugador";
    case Page::Damage: return "Daño";
    case Page::Mutations: return "Mutaciones";
    case Page::Build: return "Construcción";
    case Page::Pickup: return "Recogida y limpieza";
    case Page::World: return "Mundo";
    case Page::Teleport: return "Teletransporte";
    case Page::Creatures: return "Criaturas";
    case Page::Progress: return "Progreso";
    case Page::Options: return "Ajustes";
    case Page::Log: return "Registro";
    default: return "";
    }
}

bool IsGameSection(Page page) { return page != Page::Options && page != Page::Log; }

void Draw(Context& c) {
    switch (c.page) {
    case Page::Home: PageHome(c); break;
    case Page::Items: PageItems(c); break;
    case Page::Player: PagePlayer(c); break;
    case Page::Damage: PageDamage(c); break;
    case Page::Mutations: PageMutations(c); break;
    case Page::Build: PageBuild(c); break;
    case Page::Pickup: PagePickup(c); break;
    case Page::World: PageWorld(c); break;
    case Page::Teleport: PageTeleport(c); break;
    case Page::Creatures: PageCreatures(c); break;
    case Page::Progress: PageProgress(c); break;
    case Page::Options: PageOptions(c); break;
    case Page::Log: PageLog(c); break;
    default: break;
    }
}

} // namespace Pages
