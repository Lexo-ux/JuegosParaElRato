// SPDX-License-Identifier: GPL-3.0-or-later
#include "Catalog.hpp"

#include "core/Log.hpp"
#include "core/Reflect.hpp"
#include "common/Paths.hpp"

#include "SDK/Maine_structs.hpp"
#include "SDK/OEIText_classes.hpp"
#include "SDK/OEIText_structs.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace Catalog {
namespace {

struct FieldRef {
    int32_t offset = -1;
    int32_t size = 0;
    uint8_t mask = 0xFF;   // bool fields
    bool Ok() const { return offset >= 0; }
};

FieldRef Field(SDK::UStruct* type, std::string_view name, std::string_view kind) {
    FieldRef ref;
    auto* property = Reflect::FindProperty(type, name);
    if (!property) return ref;
    const auto actual = Reflect::FieldType(property);
    if (kind == "Bool") {
        if (actual != "BoolProperty") return ref;
        auto* boolean = static_cast<SDK::FBoolProperty*>(property);
        ref.offset = property->Offset + boolean->ByteOffset;
        ref.mask = boolean->FieldMask;
        ref.size = 1;
        return ref;
    }
    if (kind == "Byte" && actual != "ByteProperty" && actual != "EnumProperty") return ref;
    if (kind == "Int" && actual != "IntProperty") return ref;
    if (kind == "Struct" && actual != "StructProperty") return ref;
    if (kind == "Array" && actual != "ArrayProperty") return ref;
    ref.offset = property->Offset;
    ref.size = property->ElementSize;
    return ref;
}

bool StructIs(SDK::UStruct* type, const std::string& name) {
    for (auto* s = type; s; s = s->SuperStruct)
        if (s->GetName() == name) return true;
    return false;
}

struct LocString {   // OEIText.LocString
    int32_t tableId;
    int32_t stringId;
    SDK::FName tableName;
};
static_assert(sizeof(LocString) == 16);

std::unordered_map<std::string, std::wstring> spanish;
bool gameInSpanish = false;
std::unordered_map<std::string, Row> itemRows;
std::map<std::pair<uintptr_t, uint64_t>, bool> inventoryCache;
std::unordered_map<std::string, Row> mutationRows;
std::unordered_map<std::string, std::wstring> mutationNames;
bool mutationsIndexed = false;

uint64_t NameKey(SDK::FName name) {
    uint64_t key;
    std::memcpy(&key, &name, sizeof(key));
    return key;
}

// Spanish string tables shipped with the game, loaded without changing the game language.
// Uses SDK container types only after checking their layout against this game build.
void LoadSpanishTables() {
    spanish.clear();
    if (!(G2_MEMBER(UOEITextSettings, StringTableBundleSetMap) && G2_SIZE(FStringTableBundleSets) &&
          G2_MEMBER(FStringTableBundleSets, StringTableBundleSets) && G2_MEMBER(UStringTableBundleSet, StringTables) &&
          G2_SIZE(FOEIStringTable) && G2_MEMBER(FOEIStringTable, Entries) && G2_SIZE(FOEIStringEntry) &&
          G2_MEMBER(FOEIStringEntry, DefaultText))) {
        Log::Warn("Catálogo", "Las tablas de texto del juego cambiaron; no se cargan los nombres en español por separado");
        return;
    }
    auto* config = SDK::UOEITextSettings::GetDefaultObj();
    if (!config) return;
    for (const auto& language : config->StringTableBundleSetMap) {
        const std::string code = language.Key().ToString();
        if (code != "es" && !code.starts_with("es-") && !code.starts_with("es_")) continue;
        for (const auto& soft : language.Value().StringTableBundleSets) {
            SDK::TSoftObjectPtr<SDK::UObject> asset;
            static_assert(sizeof(asset) == sizeof(soft));
            std::memcpy(static_cast<void*>(&asset), static_cast<const void*>(&soft), sizeof(asset));
            auto* object = SDK::UKismetSystemLibrary::LoadAsset_Blocking(asset);
            if (!object || !object->IsA(SDK::UStringTableBundleSet::StaticClass())) continue;
            auto* bundle = static_cast<SDK::UStringTableBundleSet*>(object);
            for (const auto& table : bundle->StringTables) {
                const std::string name = table.Key().ToString();
                for (const auto& entry : table.Value().Entries) {
                    std::wstring text = Reflect::ReadString(&entry.Value().DefaultText);
                    if (!text.empty()) spanish.try_emplace(name + ":" + std::to_string(entry.Key()), std::move(text));
                }
            }
        }
        if (!spanish.empty()) break;
    }
    Log::Info("Catálogo", "Textos en español cargados: " + std::to_string(spanish.size()));
}

std::wstring Localize(const LocString& loc, const std::string& id, bool* translated) {
    auto it = spanish.find(loc.tableName.ToString() + ":" + std::to_string(loc.stringId));
    if (it != spanish.end()) {
        if (translated) *translated = true;
        return it->second;
    }
    if (gameInSpanish) {
        auto call = Reflect::Call::Static("UserInterfaceStatics", "GetLocalizedString");
        call.Raw("LocString", &loc, sizeof(loc));
        if (call.Run()) {
            std::wstring text = call.GetString();
            if (!text.empty() && text.find(L"Invalid") == std::wstring::npos) {
                if (translated) *translated = true;
                return text;
            }
        }
    }
    if (translated) *translated = false;
    return L"Sin traducción: " + Paths::Wide(id);
}

void IndexMutations() {
    if (mutationsIndexed) return;
    mutationsIndexed = true;
    mutationRows.clear();
    auto* tableClass = SDK::UDataTable::StaticClass();
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || object->IsDefaultObject() || !object->IsA(tableClass)) continue;
        auto* table = static_cast<SDK::UDataTable*>(object);
        if (!table->RowStruct || !StructIs(table->RowStruct, "PerkData") || !Reflect::RowMapLooksValid(table)) continue;
        for (const auto& row : table->RowMap) {
            if (!row.Value()) continue;
            mutationRows.try_emplace(row.Key().ToString(), Row{table, row.Key(), row.Value()});
        }
    }
}

} // namespace

void Reset() {
    itemRows.clear();
    inventoryCache.clear();
    mutationRows.clear();
    mutationNames.clear();
    mutationsIndexed = false;
}

bool LoadItems(Game::Ctx&, std::shared_ptr<const std::vector<Bridge::Item>>& out, std::wstring& message) {
    itemRows.clear();
    auto* table = static_cast<SDK::UDataTable*>(Reflect::FindObjectNamed("Table_AllItems", SDK::UDataTable::StaticClass()));
    if (!table || !table->RowStruct || !StructIs(table->RowStruct, "BaseItemData") || !Reflect::RowMapLooksValid(table)) {
        message = L"No se encontró el catálogo de objetos de esta versión.";
        return false;
    }
    const FieldRef canAdd = Field(table->RowStruct, "bCanBeAddedToInventory", "Bool");
    const FieldRef display = Field(table->RowStruct, "LocalizedDisplayName", "Struct");
    if (!canAdd.Ok() || !display.Ok() || display.size != static_cast<int32_t>(sizeof(LocString))) {
        message = L"El formato del catálogo de objetos cambió en esta versión.";
        return false;
    }

    const std::string language = SDK::UKismetInternationalizationLibrary::GetCurrentLanguage().ToString();
    gameInSpanish = language.starts_with("es");
    Log::Info("Catálogo", "Idioma del juego: " + language);
    if (!gameInSpanish) LoadSpanishTables();
    else spanish.clear();

    auto items = std::make_shared<std::vector<Bridge::Item>>();
    for (const auto& row : table->RowMap) {
        uint8_t* data = row.Value();
        if (!data) continue;
        const std::string id = row.Key().ToString();
        itemRows[id] = Row{table, row.Key(), data};
        if (!(data[canAdd.offset] & canAdd.mask)) continue;
        LocString loc;
        std::memcpy(&loc, data + display.offset, sizeof(loc));
        if (loc.stringId <= 0) continue;
        Bridge::Item item;
        item.id = id;
        item.name = Localize(loc, id, &item.translated);
        item.key = Bridge::SearchKey(item.name + L" " + Paths::Wide(id));
        items->push_back(std::move(item));
    }
    std::sort(items->begin(), items->end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    const auto translated = std::count_if(items->begin(), items->end(), [](const auto& i) { return i.translated; });
    Log::Info("Catálogo", std::to_string(items->size()) + " objetos, " + std::to_string(translated) + " con nombre en español");
    if (translated == 0)
        message = L"No se cargaron los nombres en español. Pon el juego en español y reinícialo; también puedes mostrar los objetos sin traducción.";
    out = std::move(items);
    return true;
}

bool FindItem(const std::string& id, Row& row) {
    auto it = itemRows.find(id);
    if (it == itemRows.end()) return false;
    row = it->second;
    return true;
}

bool CanAddToInventory(SDK::UDataTable* table, SDK::FName rowName) {
    if (!table || !table->RowStruct || !Reflect::RowMapLooksValid(table)) return false;
    auto key = std::make_pair(reinterpret_cast<uintptr_t>(table), NameKey(rowName));
    if (auto it = inventoryCache.find(key); it != inventoryCache.end()) return it->second;
    bool result = false;
    const FieldRef canAdd = Field(table->RowStruct, "bCanBeAddedToInventory", "Bool");
    if (canAdd.Ok()) {
        for (const auto& row : table->RowMap) {
            if (row.Key() != rowName || !row.Value()) continue;
            result = (row.Value()[canAdd.offset] & canAdd.mask) != 0;
            break;
        }
    }
    inventoryCache[key] = result;
    return result;
}

bool FindMutation(const std::string& id, Row& row) {
    IndexMutations();
    auto it = mutationRows.find(id);
    if (it == mutationRows.end()) return false;
    row = it->second;
    return true;
}

int MutationTiers(const Row& row) {
    const FieldRef tiers = Field(row.table->RowStruct, "Tiers", "Array");
    if (!tiers.Ok()) return 0;
    int32_t count;
    std::memcpy(&count, row.data + tiers.offset + 8, 4);
    return std::clamp(count, 0, 100);
}

bool MutationIsUsable(const Row& row) {
    if (!row.table || !row.data) return false;
    const FieldRef passive = Field(row.table->RowStruct, "bIsPassive", "Bool");
    const FieldRef target = Field(row.table->RowStruct, "PerkTarget", "Byte");
    if (!passive.Ok() || !target.Ok()) return false;
    if (row.data[passive.offset] & passive.mask) return false;
    static const int64_t buggy = Reflect::EnumValue("EPerkTarget", "Buggy", static_cast<int64_t>(SDK::EPerkTarget::Buggy));
    if (row.data[target.offset] == static_cast<uint8_t>(buggy)) return false;
    return MutationTiers(row) >= 1;
}

bool GetPerkStatus(SDK::UObject* perks, const Row& row, PerkStatus& out) {
    out = {};
    auto* type = Reflect::FindStruct("PerkStatus", Reflect::Kind::ScriptStruct);
    if (!perks || !type) return false;
    SDK::FDataTableRowHandle handle(row.table, row.name);
    Reflect::Call call(perks, "GetPerkStatus");
    call.Raw("PerkRowHandle", &handle, sizeof(handle));
    if (!call.Run()) return false;
    const uint8_t* status = call.ParamData("OutPerkStatus");
    if (!status) return false;
    const FieldRef tier = Field(type, "Tier", "Int"), equipped = Field(type, "bEquipped", "Bool");
    const FieldRef passive = Field(type, "bIsPassive", "Bool"), name = Field(type, "PerkName", "Struct");
    if (!tier.Ok() || !equipped.Ok() || !name.Ok() || name.size > 16) return false;
    std::memcpy(&out.tier, status + tier.offset, 4);
    out.equipped = (status[equipped.offset] & equipped.mask) != 0;
    out.passive = passive.Ok() && (status[passive.offset] & passive.mask) != 0;
    out.nameSize = name.size;
    std::memcpy(out.name, status + name.offset, static_cast<size_t>(name.size));
    return true;
}

std::vector<PerkStatus> GetPerks(SDK::UObject* perks) {
    std::vector<PerkStatus> result;
    auto* type = Reflect::FindStruct("PerkStatus", Reflect::Kind::ScriptStruct);
    if (!perks || !type || type->Size <= 0) return result;
    Reflect::Call call(perks, "GetPerks");
    if (!call.Run()) return result;
    struct RawArray { uint8_t* data; int32_t num; int32_t max; } array{};
    if (!call.GetRaw("ReturnValue", &array, sizeof(array)) || !array.data || array.num <= 0 || array.num > 1000) return result;
    const FieldRef tier = Field(type, "Tier", "Int"), equipped = Field(type, "bEquipped", "Bool");
    const FieldRef passive = Field(type, "bIsPassive", "Bool"), name = Field(type, "PerkName", "Struct");
    if (!tier.Ok() || !equipped.Ok() || !name.Ok() || name.size > 16) return result;
    for (int i = 0; i < array.num; ++i) {
        const uint8_t* status = array.data + static_cast<size_t>(i) * static_cast<size_t>(type->Size);
        PerkStatus s;
        std::memcpy(&s.tier, status + tier.offset, 4);
        s.equipped = (status[equipped.offset] & equipped.mask) != 0;
        s.passive = passive.Ok() && (status[passive.offset] & passive.mask) != 0;
        s.nameSize = name.size;
        std::memcpy(s.name, status + name.offset, static_cast<size_t>(name.size));
        result.push_back(s);
    }
    return result;
}

bool PerkStatusName(const PerkStatus& status, std::string& id) {
    if (status.nameSize < static_cast<int>(sizeof(SDK::FName))) return false;
    SDK::FName name;
    std::memcpy(&name, status.name, sizeof(name));   // FName_NetCrc32 starts with the FName
    id = name.ToString();
    return !id.empty() && id != "None";
}

std::vector<Bridge::Mutation> Mutations(Game::Ctx& ctx) {
    std::vector<Bridge::Mutation> list;
    if (!ctx.perks) return list;
    IndexMutations();
    for (const auto& [id, row] : mutationRows) {
        if (!MutationIsUsable(row)) continue;
        PerkStatus status;
        if (!GetPerkStatus(ctx.perks, row, status)) continue;
        Bridge::Mutation m;
        m.id = id;
        auto name = mutationNames.find(id);
        if (name == mutationNames.end()) {
            const FieldRef display = Field(row.table->RowStruct, "LocalizedDisplayName", "Struct");
            std::wstring text = L"Sin traducción: " + Paths::Wide(id);
            if (display.Ok() && display.size == static_cast<int32_t>(sizeof(LocString))) {
                LocString loc;
                std::memcpy(&loc, row.data + display.offset, sizeof(loc));
                text = Localize(loc, id, nullptr);
            }
            name = mutationNames.emplace(id, text).first;
        }
        m.name = name->second;
        m.tier = status.tier;
        m.maxTier = MutationTiers(row);
        m.equipped = status.equipped;
        list.push_back(std::move(m));
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return Bridge::SearchKey(a.name) < Bridge::SearchKey(b.name); });
    return list;
}

} // namespace Catalog
