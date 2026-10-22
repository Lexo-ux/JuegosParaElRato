// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime reflection helpers on top of the Dumper-7 SDK.
//
// The SDK was generated from an older build of Grounded 2. Engine classes rarely change,
// but game classes can move members around in any update. Everything here looks types,
// properties and functions up *by name* in the running game and compares them with what
// the SDK expects, so a changed layout disables a feature instead of corrupting memory.
#pragma once
#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace SDK { class UDataTable; }

namespace Reflect {

enum class Kind : uint8_t { Any, Class, ScriptStruct };

SDK::UStruct* FindStruct(std::string_view name, Kind kind = Kind::Any);
SDK::UClass* FindClass(std::string_view name);
SDK::FProperty* FindProperty(const SDK::UStruct* type, std::string_view name);
SDK::UFunction* FindFunction(const SDK::UStruct* type, std::string_view name);
std::string FieldType(const SDK::FField* field);   // "FloatProperty", "BoolProperty", ...
std::string Name(const SDK::FField* field);
uint16_t ParmsSize(const SDK::UFunction* function);

// Bool properties may be bitfields; FBoolProperty knows the byte and mask.
struct BoolField {
    int32_t offset = -1;
    uint8_t mask = 0;
    explicit operator bool() const { return offset >= 0 && mask != 0; }
};
BoolField FindBool(const SDK::UStruct* type, std::string_view name);
inline bool GetBool(const void* object, BoolField field) {
    return (static_cast<const uint8_t*>(object)[field.offset] & field.mask) != 0;
}
inline void SetBool(void* object, BoolField field, bool value) {
    auto& byte = static_cast<uint8_t*>(object)[field.offset];
    byte = value ? static_cast<uint8_t>(byte | field.mask) : static_cast<uint8_t>(byte & ~field.mask);
}

// Plain scalar property located by name, with its size verified.
template <class T>
inline T* Field(void* object, const SDK::UStruct* type, std::string_view name, std::string_view expectedType) {
    auto* property = FindProperty(type, name);
    if (!object || !property || property->ElementSize != static_cast<int32_t>(sizeof(T))) return nullptr;
    if (!expectedType.empty() && FieldType(property) != expectedType) return nullptr;
    return reinterpret_cast<T*>(static_cast<uint8_t*>(object) + property->Offset);
}

// Object property of an object (by name), e.g. Obj(gameState, "SurvivalModeManagerComponent").
template <class T = SDK::UObject>
inline T* Obj(SDK::UObject* object, std::string_view name) {
    if (!object || !object->Class) return nullptr;
    auto* property = FindProperty(object->Class, name);
    if (!property || property->ElementSize != 8) return nullptr;
    const auto type = FieldType(property);
    if (type != "ObjectProperty" && type != "ClassProperty") return nullptr;
    return *reinterpret_cast<T**>(reinterpret_cast<uint8_t*>(object) + property->Offset);
}

// Scalar member of an object by name: Value<float>(character, "CustomDamageMultiplier", "FloatProperty").
template <class T>
inline T* Value(SDK::UObject* object, std::string_view name, std::string_view type) {
    return object && object->Class ? Field<T>(object, object->Class, name, type) : nullptr;
}

inline BoolField ObjBool(SDK::UObject* object, std::string_view name) {
    return object && object->Class ? FindBool(object->Class, name) : BoolField{};
}

// Entries of a UEnum object (names without the "EType::" prefix). Empty if unreadable.
struct EnumEntry { std::string name; int64_t value; };
std::vector<EnumEntry> EnumEntries(const SDK::UObject* enumObject);

// False when a data table's row map does not look like a TMap (engine layout changed).
bool RowMapLooksValid(const SDK::UDataTable* table);

// Value of an enum entry by name ("EPerkTarget", "Buggy"); `fallback` if not found.
int64_t EnumValue(std::string_view enumName, std::string_view entry, int64_t fallback);

// Copies an FString owned by the game into a std::wstring.
std::wstring ReadString(const void* fstring);

// Every object of a class whose name matches exactly (skips class default objects).
std::vector<SDK::UObject*> Instances(const SDK::UClass* type, size_t limit = 64);

// First non-default object whose short name matches (e.g. a DataTable "Table_AllItems").
SDK::UObject* FindObjectNamed(std::string_view name, const SDK::UClass* type);

void Reset();   // forget cached lookups (after a world change Blueprint classes may reload)

// Builds a ProcessEvent parameter buffer by parameter *name*, checking each type and size.
class Call {
public:
    Call(SDK::UObject* target, std::string_view function);
    static Call Static(std::string_view className, std::string_view function);

    Call& Int(std::string_view param, int32_t value);
    Call& Float(std::string_view param, float value);
    Call& Bool(std::string_view param, bool value);
    Call& Byte(std::string_view param, uint8_t value);
    Call& Name(std::string_view param, SDK::FName value);
    Call& Object(std::string_view param, const SDK::UObject* value);
    Call& Str(std::string_view param, const wchar_t* value);
    Call& Raw(std::string_view param, const void* data, size_t size);

    bool Valid() const { return function_ && error_.empty(); }
    bool Run();
    const std::string& Error() const { return error_; }

    bool GetRaw(std::string_view param, void* out, size_t size) const;
    template <class T>
    T Get(std::string_view param = "ReturnValue", T fallback = {}) const {
        T value{};
        return GetRaw(param, &value, sizeof(T)) ? value : fallback;
    }
    bool GetBool(std::string_view param = "ReturnValue") const;
    std::wstring GetString(std::string_view param = "ReturnValue") const;
    const uint8_t* ParamData(std::string_view param) const;   // raw pointer into the buffer, or null

private:
    SDK::FProperty* Param(std::string_view name, size_t size, std::initializer_list<std::string_view> types);
    SDK::UObject* target_ = nullptr;
    SDK::UFunction* function_ = nullptr;
    std::vector<uint8_t> buffer_;
    std::string error_;
    std::string name_;
};

// True when the function exists on the object's class (cheap check for the UI).
bool HasFunction(const SDK::UObject* target, std::string_view function);

} // namespace Reflect

namespace Guard {

// Strips the SDK prefix: ASurvivalPlayerCharacter -> SurvivalPlayerCharacter.
std::string UeName(const char* sdkName);

bool Member(const char* sdkType, const char* member, size_t offset, size_t size);

// Offset of a member in the *running* game, looked up by name and checked against the SDK's
// size. Engine classes grew in Grounded 2 0.6.0.3 (AActor +0x10, UEngine +0x78...), so members
// are no longer read at the SDK's offsets. Returns -1 if the member is gone or changed size.
int32_t Offset(const char* sdkType, const char* member, size_t sdkOffset, size_t size);

template <class T>
inline T* At(const void* object, int32_t offset) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + static_cast<uintptr_t>(offset));
}
bool Function(const char* ueClass, const char* function, size_t paramsSize);
bool Size(const char* sdkType, size_t size);

std::vector<std::string> Failures();
void Reset();

} // namespace Guard

#define G2_MEMBER(T, M) ::Guard::Member(#T, #M, offsetof(SDK::T, M), sizeof(SDK::T::M))
#define G2_FUNC(C, F) ::Guard::Function(#C, #F, sizeof(SDK::Params::C##_##F))
#define G2_FUNC0(C, F) ::Guard::Function(#C, #F, 0)
#define G2_SIZE(T) ::Guard::Size(#T, sizeof(SDK::T))

// True when the member exists in the running game with the SDK's size (any offset).
#define G2_FIELD_OK(T, M) (::Guard::Offset(#T, #M, offsetof(SDK::T, M), sizeof(SDK::T::M)) >= 0)

// Reference to member M of `obj` (an SDK::T or subclass) at its runtime offset:
//   G2F(world, UWorld, GameState)  instead of  world->GameState
// The offset is resolved once per call site; the SDK offset is only a last-resort fallback
// (EngineLayoutOk() checks every member used this way before the panel starts).
#define G2F(obj, T, M)                                                                                              \
    (*::Guard::At<std::remove_reference_t<decltype(std::declval<SDK::T&>().M)>>(                                    \
        (obj), [] {                                                                                                  \
            static const int32_t offset = [] {                                                                       \
                const int32_t found = ::Guard::Offset(#T, #M, offsetof(SDK::T, M), sizeof(SDK::T::M));             \
                return found >= 0 ? found : static_cast<int32_t>(offsetof(SDK::T, M));                               \
            }();                                                                                                     \
            return offset;                                                                                           \
        }()))
