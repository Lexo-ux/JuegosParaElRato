// SPDX-License-Identifier: GPL-3.0-or-later
#include "Reflect.hpp"

#include "Log.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Reflect {
namespace {

std::recursive_mutex mutex;
std::unordered_map<std::string, std::vector<SDK::UStruct*>> structIndex;  // name -> structs/classes
int indexedObjects = 0;
std::unordered_map<std::string, SDK::FProperty*> propertyCache;
std::unordered_map<std::string, SDK::UFunction*> functionCache;

std::string Key(const void* owner, std::string_view name) {
    return std::to_string(reinterpret_cast<uintptr_t>(owner)) + ":" + std::string(name);
}

void BuildIndex() {
    structIndex.clear();
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class) continue;
        if (!object->HasTypeFlag(SDK::EClassCastFlags::Struct) || object->HasTypeFlag(SDK::EClassCastFlags::Function)) continue;
        structIndex[object->GetName()].push_back(static_cast<SDK::UStruct*>(object));
    }
    indexedObjects = count;
}

bool MatchesKind(SDK::UStruct* type, Kind kind) {
    switch (kind) {
    case Kind::Class: return type->HasTypeFlag(SDK::EClassCastFlags::Class);
    case Kind::ScriptStruct: return type->HasTypeFlag(SDK::EClassCastFlags::ScriptStruct);
    default: return true;
    }
}

} // namespace

SDK::UStruct* FindStruct(std::string_view name, Kind kind) {
    std::lock_guard lock(mutex);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1 || structIndex.empty()) {
            // Rebuild only when new objects appeared since the last index (e.g. Blueprints loaded).
            if (attempt == 1 && SDK::UObject::GObjects->Num() == indexedObjects) break;
            BuildIndex();
        }
        auto it = structIndex.find(std::string(name));
        if (it == structIndex.end()) continue;
        for (auto* type : it->second)
            if (MatchesKind(type, kind)) return type;
    }
    return nullptr;
}

SDK::UClass* FindClass(std::string_view name) {
    return static_cast<SDK::UClass*>(FindStruct(name, Kind::Class));
}

std::string Name(const SDK::FField* field) {
    return field ? field->Name.ToString() : std::string();
}

std::string FieldType(const SDK::FField* field) {
    return field && field->ClassPrivate ? field->ClassPrivate->Name.ToString() : std::string();
}

SDK::FProperty* FindProperty(const SDK::UStruct* type, std::string_view name) {
    if (!type) return nullptr;
    std::lock_guard lock(mutex);
    auto key = Key(type, name);
    if (auto it = propertyCache.find(key); it != propertyCache.end()) return it->second;
    SDK::FProperty* found = nullptr;
    for (const SDK::UStruct* s = type; s && !found; s = s->SuperStruct)
        for (SDK::FField* field = s->ChildProperties; field; field = field->Next)
            if (field->Name.ToString() == name) { found = static_cast<SDK::FProperty*>(field); break; }
    propertyCache[key] = found;
    return found;
}

SDK::UFunction* FindFunction(const SDK::UStruct* type, std::string_view name) {
    if (!type) return nullptr;
    std::lock_guard lock(mutex);
    auto key = Key(type, name);
    if (auto it = functionCache.find(key); it != functionCache.end()) return it->second;
    SDK::UFunction* found = nullptr;
    for (const SDK::UStruct* s = type; s && !found; s = s->SuperStruct)
        for (SDK::UField* field = s->Children; field; field = field->Next)
            if (field->HasTypeFlag(SDK::EClassCastFlags::Function) && field->GetName() == name) {
                found = static_cast<SDK::UFunction*>(field);
                break;
            }
    functionCache[key] = found;
    return found;
}

uint16_t ParmsSize(const SDK::UFunction* function) {
    uint16_t size = 0;
    if (function) std::memcpy(&size, reinterpret_cast<const uint8_t*>(function) + 0xB6, sizeof(size));
    return size;
}

BoolField FindBool(const SDK::UStruct* type, std::string_view name) {
    auto* property = FindProperty(type, name);
    if (!property || FieldType(property) != "BoolProperty") return {};
    auto* boolean = static_cast<SDK::FBoolProperty*>(property);
    return {property->Offset + boolean->ByteOffset, boolean->FieldMask};
}

std::vector<SDK::UObject*> Instances(const SDK::UClass* type, size_t limit) {
    std::vector<SDK::UObject*> result;
    if (!type) return result;
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count && result.size() < limit; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (object && object->Class && !object->IsDefaultObject() && object->IsA(type)) result.push_back(object);
    }
    return result;
}

int64_t EnumValue(std::string_view enumName, std::string_view entry, int64_t fallback) {
    auto* enumClass = SDK::UEnum::StaticClass();
    if (!enumClass) return fallback;
    static std::unordered_map<std::string, int64_t> cache;
    std::lock_guard lock(mutex);
    std::string key = std::string(enumName) + "::" + std::string(entry);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || !object->IsA(enumClass) || object->GetName() != enumName) continue;
        auto* type = static_cast<SDK::UEnum*>(object);
        for (const auto& pair : type->Names) {
            std::string name = pair.First.ToString();
            auto colon = name.rfind("::");
            if (colon != std::string::npos) name = name.substr(colon + 2);
            if (name == entry) {
                cache[key] = pair.Second;
                return pair.Second;
            }
        }
        break;
    }
    cache[key] = fallback;
    Log::Warn("Reflexión", "No se encontró " + key + "; se usa el valor conocido " + std::to_string(fallback));
    return fallback;
}

std::wstring ReadString(const void* fstring) {
    if (!fstring) return {};
    const auto* text = static_cast<const SDK::FString*>(fstring);
    const wchar_t* data = text->CStr();
    if (!data || text->Num() <= 0 || text->Num() > 65536) return {};
    std::wstring out(data, static_cast<size_t>(text->Num()));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

SDK::UObject* FindObjectNamed(std::string_view name, const SDK::UClass* type) {
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || object->IsDefaultObject()) continue;
        if (type && !object->IsA(type)) continue;
        if (object->GetName() == name) return object;
    }
    return nullptr;
}

void Reset() {
    std::lock_guard lock(mutex);
    structIndex.clear();
    indexedObjects = 0;
    propertyCache.clear();
    functionCache.clear();
}

bool HasFunction(const SDK::UObject* target, std::string_view function) {
    return target && target->Class && FindFunction(target->Class, function);
}

// ---------------------------------------------------------------------------------------------
// Call

Call::Call(SDK::UObject* target, std::string_view function) : target_(target), name_(function) {
    if (!target_ || !target_->Class) {
        error_ = "objeto nulo para " + name_;
        return;
    }
    function_ = FindFunction(target_->Class, function);
    if (!function_) {
        error_ = "la función " + name_ + " no existe en " + target_->Class->GetName();
        return;
    }
    const size_t size = std::max<size_t>(ParmsSize(function_), static_cast<size_t>(std::max(function_->Size, 0)));
    buffer_.assign(size + 16, 0);
}

Call Call::Static(std::string_view className, std::string_view function) {
    auto* type = FindClass(className);
    SDK::UObject* defaults = type ? type->ClassDefaultObject : nullptr;
    Call call(defaults, function);
    if (!type) call.error_ = "no existe la clase " + std::string(className);
    return call;
}

SDK::FProperty* Call::Param(std::string_view name, size_t size, std::initializer_list<std::string_view> types) {
    if (!function_) return nullptr;
    for (SDK::FField* field = function_->ChildProperties; field; field = field->Next) {
        if (field->Name.ToString() != name) continue;
        auto* property = static_cast<SDK::FProperty*>(field);
        const auto type = FieldType(field);
        const bool typeOk = types.size() == 0 || std::find(types.begin(), types.end(), type) != types.end();
        if (!typeOk) {
            error_ = name_ + "." + std::string(name) + ": tipo " + type + " inesperado";
            return nullptr;
        }
        if (size && static_cast<size_t>(property->ElementSize) != size && type != "DoubleProperty") {
            error_ = name_ + "." + std::string(name) + ": tamaño " + std::to_string(property->ElementSize) +
                     " en lugar de " + std::to_string(size);
            return nullptr;
        }
        if (static_cast<size_t>(property->Offset + property->ElementSize) > buffer_.size()) {
            error_ = name_ + "." + std::string(name) + ": fuera del bloque de parámetros";
            return nullptr;
        }
        return property;
    }
    error_ = name_ + ": no tiene el parámetro " + std::string(name);
    return nullptr;
}

Call& Call::Int(std::string_view param, int32_t value) {
    if (auto* p = Param(param, 4, {"IntProperty", "UInt32Property"})) std::memcpy(buffer_.data() + p->Offset, &value, 4);
    return *this;
}

Call& Call::Float(std::string_view param, float value) {
    if (auto* p = Param(param, 4, {"FloatProperty", "DoubleProperty"})) {
        if (FieldType(p) == "DoubleProperty" && p->ElementSize == 8) {
            double wide = value;
            std::memcpy(buffer_.data() + p->Offset, &wide, 8);
        } else if (p->ElementSize == 4) {
            std::memcpy(buffer_.data() + p->Offset, &value, 4);
        } else {
            error_ = name_ + "." + std::string(param) + ": tamaño de número inesperado";
        }
    }
    return *this;
}

Call& Call::Bool(std::string_view param, bool value) {
    if (auto* p = Param(param, 1, {"BoolProperty"})) {
        auto* boolean = static_cast<SDK::FBoolProperty*>(p);
        uint8_t& byte = buffer_[p->Offset + boolean->ByteOffset];
        byte = value ? static_cast<uint8_t>(byte | boolean->FieldMask) : static_cast<uint8_t>(byte & ~boolean->FieldMask);
    }
    return *this;
}

Call& Call::Byte(std::string_view param, uint8_t value) {
    if (auto* p = Param(param, 1, {"ByteProperty", "EnumProperty", "UInt8Property", "Int8Property"}))
        buffer_[p->Offset] = value;
    return *this;
}

Call& Call::Name(std::string_view param, SDK::FName value) {
    if (auto* p = Param(param, sizeof(SDK::FName), {"NameProperty"})) std::memcpy(buffer_.data() + p->Offset, &value, sizeof(value));
    return *this;
}

Call& Call::Object(std::string_view param, const SDK::UObject* value) {
    if (auto* p = Param(param, 8, {"ObjectProperty", "ClassProperty", "WeakObjectProperty", "InterfaceProperty"}))
        std::memcpy(buffer_.data() + p->Offset, &value, 8);
    return *this;
}

Call& Call::Str(std::string_view param, const wchar_t* value) {
    if (auto* p = Param(param, sizeof(SDK::FString), {"StrProperty"})) {
        // The string memory stays owned by the caller for the duration of the call.
        SDK::FString text(value);
        std::memcpy(buffer_.data() + p->Offset, &text, sizeof(text));
    }
    return *this;
}

Call& Call::Raw(std::string_view param, const void* data, size_t size) {
    if (auto* p = Param(param, size, {})) std::memcpy(buffer_.data() + p->Offset, data, size);
    return *this;
}

bool Call::Run() {
    if (!Valid()) {
        if (!error_.empty()) Log::Warn("Llamada", error_);
        return false;
    }
    target_->ProcessEvent(function_, buffer_.data());
    return true;
}

bool Call::GetRaw(std::string_view param, void* out, size_t size) const {
    if (!function_) return false;
    for (SDK::FField* field = function_->ChildProperties; field; field = field->Next) {
        if (field->Name.ToString() != param) continue;
        auto* property = static_cast<SDK::FProperty*>(field);
        if (static_cast<size_t>(property->ElementSize) != size) return false;
        std::memcpy(out, buffer_.data() + property->Offset, size);
        return true;
    }
    return false;
}

const uint8_t* Call::ParamData(std::string_view param) const {
    if (!function_) return nullptr;
    for (SDK::FField* field = function_->ChildProperties; field; field = field->Next)
        if (field->Name.ToString() == param) return buffer_.data() + static_cast<SDK::FProperty*>(field)->Offset;
    return nullptr;
}

std::wstring Call::GetString(std::string_view param) const {
    if (!function_) return {};
    for (SDK::FField* field = function_->ChildProperties; field; field = field->Next) {
        if (field->Name.ToString() != param || FieldType(field) != "StrProperty") continue;
        return ReadString(buffer_.data() + static_cast<SDK::FProperty*>(field)->Offset);
    }
    return {};
}

bool Call::GetBool(std::string_view param) const {
    if (!function_) return false;
    for (SDK::FField* field = function_->ChildProperties; field; field = field->Next) {
        if (field->Name.ToString() != param || FieldType(field) != "BoolProperty") continue;
        auto* property = static_cast<SDK::FBoolProperty*>(field);
        return (buffer_[property->Offset + property->ByteOffset] & property->FieldMask) != 0;
    }
    return false;
}

} // namespace Reflect

// -------------------------------------------------------------------------------------------------
// Guard

namespace Guard {
namespace {
std::mutex mutex;
std::unordered_map<std::string, bool> results;
std::vector<std::string> failures;

bool Remember(const std::string& key, bool ok, const std::string& detail) {
    results[key] = ok;
    if (!ok && std::find(failures.begin(), failures.end(), detail) == failures.end()) {
        failures.push_back(detail);
        Log::Warn("Compatibilidad", detail);
    }
    return ok;
}
} // namespace

std::string UeName(const char* sdkName) {
    std::string name(sdkName);
    if (name.size() > 1 && (name[0] == 'A' || name[0] == 'U' || name[0] == 'F' || name[0] == 'E') &&
        (std::isupper(static_cast<unsigned char>(name[1])) || name.starts_with("ABP_") || name.starts_with("UBP_")))
        name.erase(0, 1);
    return name;
}

bool Member(const char* sdkType, const char* member, size_t offset, size_t size) {
    std::string key = std::string(sdkType) + "::" + member;
    {
        std::lock_guard lock(mutex);
        if (auto it = results.find(key); it != results.end()) return it->second;
    }
    const auto kind = sdkType[0] == 'F' ? Reflect::Kind::ScriptStruct : Reflect::Kind::Class;
    auto* type = Reflect::FindStruct(UeName(sdkType), kind);
    std::lock_guard lock(mutex);
    if (!type) return Remember(key, false, "No existe el tipo " + UeName(sdkType));
    auto* property = Reflect::FindProperty(type, member);
    if (!property) return Remember(key, false, key + " ya no existe");
    const size_t actualSize = static_cast<size_t>(property->ElementSize) * static_cast<size_t>(std::max(property->ArrayDim, 1));
    if (static_cast<size_t>(property->Offset) != offset || actualSize != size)
        return Remember(key, false, key + " cambió (offset " + Log::Hex(property->Offset) + " tamaño " +
                                        std::to_string(actualSize) + "; esperado " + Log::Hex(offset) + "/" + std::to_string(size) + ")");
    return Remember(key, true, {});
}

bool Function(const char* ueClass, const char* function, size_t paramsSize) {
    std::string key = std::string(ueClass) + "." + function + "()";
    {
        std::lock_guard lock(mutex);
        if (auto it = results.find(key); it != results.end()) return it->second;
    }
    auto* type = Reflect::FindClass(ueClass);
    auto* fn = type ? Reflect::FindFunction(type, function) : nullptr;
    std::lock_guard lock(mutex);
    if (!type) return Remember(key, false, std::string("No existe la clase ") + ueClass);
    if (!fn) return Remember(key, false, key + " ya no existe");
    const size_t parms = Reflect::ParmsSize(fn);
    const size_t props = static_cast<size_t>(std::max(fn->Size, 0));
    const size_t align = static_cast<size_t>(std::max<int16_t>(fn->MinAlignment, 1));
    auto alignUp = [&](size_t v) { return (v + align - 1) / align * align; };
    bool ok = paramsSize == 0 ? parms == 0
                              : (paramsSize == parms || paramsSize == props || paramsSize == alignUp(parms) || paramsSize == alignUp(props));
    if (!ok)
        return Remember(key, false, key + " cambió sus parámetros (" + std::to_string(parms) + "/" + std::to_string(props) +
                                        " bytes; esperado " + std::to_string(paramsSize) + ")");
    return Remember(key, true, {});
}

bool Size(const char* sdkType, size_t size) {
    std::string key = std::string("sizeof ") + sdkType;
    {
        std::lock_guard lock(mutex);
        if (auto it = results.find(key); it != results.end()) return it->second;
    }
    const auto kind = sdkType[0] == 'F' ? Reflect::Kind::ScriptStruct : Reflect::Kind::Class;
    auto* type = Reflect::FindStruct(UeName(sdkType), kind);
    std::lock_guard lock(mutex);
    if (!type) return Remember(key, false, "No existe el tipo " + UeName(sdkType));
    const size_t align = static_cast<size_t>(std::max<int16_t>(type->MinAlignment, 1));
    const size_t actual = static_cast<size_t>(type->Size);
    const size_t aligned = (actual + align - 1) / align * align;
    if (actual != size && aligned != size)
        return Remember(key, false, "El tamaño de " + UeName(sdkType) + " cambió (" + std::to_string(actual) +
                                        " bytes; esperado " + std::to_string(size) + ")");
    return Remember(key, true, {});
}

std::vector<std::string> Failures() {
    std::lock_guard lock(mutex);
    return failures;
}

int64_t EnumValue(std::string_view enumName, std::string_view entry, int64_t fallback) {
    auto* enumClass = SDK::UEnum::StaticClass();
    if (!enumClass) return fallback;
    static std::unordered_map<std::string, int64_t> cache;
    std::lock_guard lock(mutex);
    std::string key = std::string(enumName) + "::" + std::string(entry);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || !object->IsA(enumClass) || object->GetName() != enumName) continue;
        auto* type = static_cast<SDK::UEnum*>(object);
        for (const auto& pair : type->Names) {
            std::string name = pair.First.ToString();
            auto colon = name.rfind("::");
            if (colon != std::string::npos) name = name.substr(colon + 2);
            if (name == entry) {
                cache[key] = pair.Second;
                return pair.Second;
            }
        }
        break;
    }
    cache[key] = fallback;
    Log::Warn("Reflexión", "No se encontró " + key + "; se usa el valor conocido " + std::to_string(fallback));
    return fallback;
}

std::wstring ReadString(const void* fstring) {
    if (!fstring) return {};
    const auto* text = static_cast<const SDK::FString*>(fstring);
    const wchar_t* data = text->CStr();
    if (!data || text->Num() <= 0 || text->Num() > 65536) return {};
    std::wstring out(data, static_cast<size_t>(text->Num()));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

SDK::UObject* FindObjectNamed(std::string_view name, const SDK::UClass* type) {
    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object || !object->Class || object->IsDefaultObject()) continue;
        if (type && !object->IsA(type)) continue;
        if (object->GetName() == name) return object;
    }
    return nullptr;
}

void Reset() {
    std::lock_guard lock(mutex);
    results.clear();
    failures.clear();
}

} // namespace Guard
