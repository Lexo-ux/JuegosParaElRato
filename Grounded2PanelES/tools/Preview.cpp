// SPDX-License-Identifier: GPL-3.0-or-later
// VistaPrevia.exe: the real panel interface fed with sample data, to review the design
// without the game. Usage: VistaPrevia.exe [--pagina N] [--esperando]
#include "game/Bridge.hpp"
#include "ui/PanelWindow.hpp"

#include <windows.h>
#include <shellapi.h>

#include <mutex>
#include <string>

namespace {

std::mutex mutex;
Bridge::Snapshot snapshot;

void Seed(bool waiting) {
    using namespace Bridge;
    Snapshot& s = snapshot;
    s.resolved = true;
    s.ready = !waiting;
    s.host = true;
    s.gameVersion = "0.6.0.0";
    s.engineVersion = "5.6.1";
    s.player = L"Rato";
    s.localPlayerId = 256;
    s.session = 1;
    for (auto& f : s.features) f.ok = true;
    s.features[static_cast<size_t>(Feature::Arachnophobia)] = {false, L"SurvivalCheatManager.SetArachnophobiaSafeMode() ya no existe"};
    s.compatibilityNotes = {L"SurvivalCheatManager.SetArachnophobiaSafeMode() ya no existe"};
    s.freeCraft = true;
    s.anywhere = true;
    s.god = true;
    s.infiniteStamina = true;
    s.autoPickup = true;
    s.collected = 37;
    s.health = 86;
    s.maxHealth = 100;
    s.food = 64;
    s.maxFood = 100;
    s.water = 41;
    s.maxWater = 100;
    s.hasPosition = true;
    s.hasBackPosition = true;
    s.x = 18234;
    s.y = -6420;
    s.z = 512;
    s.slots = 4;
    s.message = waiting ? L"Listo. Carga una partida en solitario o como anfitrión." : L"Conectado. 48 de 49 funciones verificadas en tu versión.";
    s.tone = Tone::Success;
    s.messageId = 1;
    auto items = std::make_shared<std::vector<Item>>();
    const std::pair<const char*, const wchar_t*> names[] = {
        {"StoneShale", L"Guijarro"}, {"FiberRaw", L"Fibra vegetal"}, {"Twig", L"Ramita"}, {"Web", L"Fibra de telaraña"},
        {"Sap", L"Savia"}, {"POllen", L"Polen"}, {"DewDrop", L"Gota de rocío"}, {"log", L"Tallo de hierba"},
        {"GrassPlank", L"Tablón de hierba"}, {"AcornTop", L"Copa de bellota"}, {"AcornShell", L"Cáscara de bellota"},
        {"AntPart", L"Parte de hormiga roja"}, {"AntHead", L"Cabeza de hormiga roja"}, {"Clay", L"Arcilla"},
        {"FiberDry", L"Trozo de hierba seca"}, {"ThistleNeedle", L"Aguja de cardo"}, {"HedgeBerry", L"Trozo de arándano"},
        {"BerryRed", L"Trozo de arándano rojo"}, {"MushroomChunk", L"Trozo de seta"}, {"Burr", L"Abrojo espinoso"},
        {"FiberWoven", L"Cuerda tosca"}, {"WebWoven", L"Cuerda de seda"}, {"MushroomBrick", L"Ladrillo de seta"},
        {"IngotQuartzite", L"Glóbulo poderoso"}, {"Honeydew", L"Melaza de pulgón"}, {"DriedAphidMeat", L"Cecina de pulgón"},
        {"BandageTier1", L"Venda de fibra"}, {"BandageTier2", L"Venda de arándano"}, {"Torch", L"Antorcha"},
        {"BombHeal", L"Bomba curativa"}, {"UpgradeWeapon1", L"Fragmento de cuarcita frágil"},
        {"UpgradeWeapon2", L"Fragmento de cuarcita robusta"}, {"UpgradeWeapon3", L"Fragmento de cuarcita suprema"},
        {"SmoothieFiber", L"Batido de fibra"}, {"Mushroom", L"Seta"}, {"Fabric", L"Cuero de arándano"},
        {"GranolaAcorn", L""},
    };
    for (const auto& [id, name] : names) {
        Item item;
        item.id = id;
        item.translated = name[0] != 0;
        item.name = item.translated ? name : L"Sin traducción: " + std::wstring(id, id + strlen(id));
        item.key = SearchKey(item.name + L" " + std::wstring(id, id + strlen(id)));
        items->push_back(item);
    }
    s.items = items;
    s.catalogRevision = 1;
    s.mutations = {
        {"Perk_Buff", L"Musculitos", 2, 3, true}, {"Perk_Sprint", L"Velocista", 1, 3, false},
        {"Perk_Mind", L"Mente sobre la materia", 3, 3, true}, {"Perk_Spider", L"Asesino de arañas", 0, 3, false},
        {"Perk_Cook", L"Chef de patio", 1, 2, false},
    };
    s.players = {{256, L"Rato", true, false, true, 1}, {257, L"Mapache", true, true, false, 2}};
    const std::pair<const char*, const wchar_t*> rules[] = {
        {"bCreaturesIgnorePlayers", L"Las criaturas ignoran a los jugadores"}, {"bFriendlyFire", L"Fuego amigo"},
        {"bSpoilingEnabled", L"La comida se estropea"}, {"bPetInvincible", L"Mascotas invencibles"},
        {"HungerMultiplier", L"Velocidad del hambre"}, {"ThirstMultiplier", L"Velocidad de la sed"},
        {"FallDamageMultiplier", L"Daño por caída"}, {"ResourceRespawnMultiplier", L"Reaparición de recursos"},
    };
    for (const auto& [id, label] : rules) {
        Rule r;
        r.id = id;
        r.label = label;
        r.isBool = id[0] == 'b';
        r.boolValue = std::string(id) == "bSpoilingEnabled";
        r.floatValue = std::string(id) == "FallDamageMultiplier" ? 0.0f : 1.0f;
        r.changed = std::string(id) == "FallDamageMultiplier";
        s.rules.push_back(r);
    }
    s.revision = 1;
}

} // namespace

namespace Bridge {
bool Submit(Request request) {
    if (!Valid(request)) return false;
    std::lock_guard lock(mutex);
    switch (request.action) {
    case Action::GodMode: snapshot.god = request.enabled; break;
    case Action::InfiniteStamina: snapshot.infiniteStamina = request.enabled; break;
    case Action::NoHunger: snapshot.noHunger = request.enabled; break;
    case Action::FreeCraft: snapshot.freeCraft = request.enabled; break;
    case Action::FreeBuild: snapshot.freeBuild = request.enabled; break;
    case Action::BuildAnywhere: snapshot.anywhere = request.enabled; break;
    case Action::AutoPickup: snapshot.autoPickup = request.enabled; break;
    case Action::Fly: snapshot.fly = request.enabled; break;
    case Action::Noclip: snapshot.noclip = request.enabled; break;
    default: break;
    }
    snapshot.message = L"Vista previa: la acción se enviaría al juego.";
    snapshot.tone = Tone::Info;
    ++snapshot.messageId;
    ++snapshot.revision;
    return true;
}
Snapshot Read() {
    std::lock_guard lock(mutex);
    return snapshot;
}
uint64_t Revision() {
    std::lock_guard lock(mutex);
    return snapshot.revision;
}
} // namespace Bridge

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int page = -1;
    bool waiting = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--pagina" && i + 1 < argc) page = _wtoi(argv[++i]);
        else if (arg == L"--esperando") waiting = true;
    }
    if (argv) LocalFree(argv);
    Seed(waiting);
    return PanelWindow::Run(instance, true, page);
}
