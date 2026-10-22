// SPDX-License-Identifier: GPL-3.0-or-later
#include "Resolver.hpp"

#include "Log.hpp"
#include "Memory.hpp"
#include "NamePool.hpp"

#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"

#include <atomic>
#include <unordered_set>

namespace Resolver {
namespace {

// Engine-level layout (UE 5.6), checked against the SDK's own asserts at compile time.
constexpr size_t kObjectItemSize = sizeof(SDK::FUObjectItem);                    // 0x18
constexpr size_t kObjectIndexOffset = offsetof(SDK::UObject, Index);            // 0x0C
constexpr size_t kObjectClassOffset = offsetof(SDK::UObject, Class);            // 0x10
constexpr size_t kObjectNameOffset = offsetof(SDK::UObject, Name);              // 0x18
constexpr size_t kFunctionFlagsOffset = offsetof(SDK::UFunction, FunctionFlags); // 0xB0

Mem::Module game;
NamePool::View names;
std::atomic<bool> namesReady{false};

struct RawFName { int32_t comparisonIndex; uint32_t number; };
struct RawFString { wchar_t* data; int32_t num; int32_t max; };
static_assert(sizeof(RawFName) == sizeof(SDK::FName));
static_assert(sizeof(RawFString) == sizeof(SDK::FString));
static_assert(sizeof(wchar_t) == sizeof(char16_t));

// Replacement for FName::AppendString: same signature the SDK uses, but implemented by the
// panel, so no game function offset is required for names.
void AppendStringImpl(const RawFName* name, RawFString& out) {
    if (!name || !out.data || out.max <= 0) return;
    std::u16string text;
    if (!names.ToString(static_cast<uint32_t>(name->comparisonIndex), name->number, text)) text = u"None";
    int32_t start = out.num > 0 ? out.num - 1 : 0;
    int32_t room = out.max - start - 1;
    if (room <= 0) return;
    int32_t count = static_cast<int32_t>(text.size()) < room ? static_cast<int32_t>(text.size()) : room;
    std::memcpy(out.data + start, text.data(), static_cast<size_t>(count) * sizeof(wchar_t));
    out.data[start + count] = L'\0';
    out.num = start + count + 1;
}

std::vector<const Mem::Section*> DataSections() {
    std::vector<const Mem::Section*> result;
    for (const auto& section : game.sections) {
        const bool writable = section.characteristics & IMAGE_SCN_MEM_WRITE;
        const bool executable = section.characteristics & IMAGE_SCN_MEM_EXECUTE;
        if (writable && !executable && section.size >= 0x1000) result.push_back(&section);
    }
    return result;
}

bool ValidateObjectArray(uintptr_t candidate, Mem::RegionCache& cache, int& countOut) {
    uintptr_t objects;
    int32_t maxElements, numElements;
    std::memcpy(&objects, reinterpret_cast<const void*>(candidate), 8);
    std::memcpy(&maxElements, reinterpret_cast<const void*>(candidate + 8), 4);
    std::memcpy(&numElements, reinterpret_cast<const void*>(candidate + 12), 4);
    if (!objects || (objects & 7) || game.Contains(objects)) return false;
    if (numElements < 0x1000 || numElements > maxElements || maxElements > 0x4000000 || maxElements < 0x10000) return false;
    if (!cache.Readable(objects, kObjectItemSize * 64)) return false;
    for (int i = 0; i < 64; ++i) {
        uintptr_t object;
        std::memcpy(&object, reinterpret_cast<const void*>(objects + i * kObjectItemSize), 8);
        if (!object || (object & 7) || !Mem::IsReadable(reinterpret_cast<const void*>(object), 0x28)) return false;
        uintptr_t vtable, klass;
        int32_t index;
        std::memcpy(&vtable, reinterpret_cast<const void*>(object), 8);
        std::memcpy(&index, reinterpret_cast<const void*>(object + kObjectIndexOffset), 4);
        std::memcpy(&klass, reinterpret_cast<const void*>(object + kObjectClassOffset), 8);
        if (!game.Contains(vtable) || index != i || !klass) return false;
        if (!Mem::IsReadable(reinterpret_cast<const void*>(klass), 0x30)) return false;
    }
    countOut = numElements;
    return true;
}

bool FindObjects(Report& report) {
    // Previous build's offset first (cheap), then a full scan of the writable sections.
    Mem::RegionCache cache;
    int count = 0;
    uintptr_t hinted = game.base + static_cast<uintptr_t>(SDK::Offsets::GObjects);
    for (const auto* section : DataSections()) {
        if (section->Contains(hinted, 16) && ValidateObjectArray(hinted, cache, count)) {
            SDK::UObject::GObjects.InitManually(reinterpret_cast<void*>(hinted));
            report.objectsRva = hinted - game.base;
            report.objectCount = count;
            Log::Info("Resolver", "GObjects coincide con el offset conocido " + Log::Hex(report.objectsRva));
            return true;
        }
    }
    for (const auto* section : DataSections()) {
        for (uintptr_t p = section->start; p + 16 <= section->start + section->size; p += 8) {
            if (ValidateObjectArray(p, cache, count)) {
                SDK::UObject::GObjects.InitManually(reinterpret_cast<void*>(p));
                report.objectsRva = p - game.base;
                report.objectCount = count;
                Log::Info("Resolver", "GObjects localizado en " + Log::Hex(report.objectsRva) + " (" + section->name +
                          "), " + std::to_string(count) + " objetos");
                return true;
            }
        }
    }
    return false;
}

bool BlocksArrayLooksValid(uintptr_t blocksAddress, Mem::RegionCache& cache) {
    uint32_t currentBlock, cursor;
    std::memcpy(&currentBlock, reinterpret_cast<const void*>(blocksAddress - 8), 4);
    std::memcpy(&cursor, reinterpret_cast<const void*>(blocksAddress - 4), 4);
    if (currentBlock >= 8192 || cursor > (4u << 16)) return false;
    for (uint32_t b = 0; b <= currentBlock; ++b) {
        uintptr_t block;
        if (!Mem::Read(blocksAddress + 8ull * b, block) || !block || !cache.Readable(block, 64)) return false;
    }
    uintptr_t next = 0;
    if (currentBlock + 1 < 8192 && Mem::Read(blocksAddress + 8ull * (currentBlock + 1), next) && next != 0) return false;
    return true;
}

// Every object name must decode and the core class names must be present.
bool NamesDecodeObjects() {
    std::unordered_set<std::string> seen;
    int decoded = 0;
    const int count = std::min(SDK::UObject::GObjects->Num(), 4096);
    for (int i = 0; i < count; ++i) {
        auto* object = SDK::UObject::GObjects->GetByIndex(i);
        if (!object) continue;
        RawFName name;
        std::memcpy(&name, reinterpret_cast<const uint8_t*>(object) + kObjectNameOffset, sizeof(name));
        std::u16string text;
        if (!names.Decode(static_cast<uint32_t>(name.comparisonIndex), text) || text.empty()) return false;
        ++decoded;
        if (i < 4096) seen.insert(NamePool::ToUtf8(text));
    }
    return decoded > 256 && seen.contains("Object") && seen.contains("Class") && seen.contains("Function");
}

bool FindNames(Report& report) {
    Mem::RegionCache cache;
    for (const auto* section : DataSections()) {
        for (uintptr_t p = section->start + 8; p + 8 <= section->start + section->size; p += 8) {
            uintptr_t value;
            std::memcpy(&value, reinterpret_cast<const void*>(p), 8);
            if (!value || (value & 1) || value < 0x10000 || value > 0x7FFFFFFFFFFFull || game.Contains(value)) continue;
            if (!cache.Readable(value, 64)) continue;
            uint32_t stride;
            NamePool::HeaderLayout layout;
            if (!NamePool::LooksLikeFirstBlock(reinterpret_cast<const uint8_t*>(value), 64, stride, layout)) continue;
            if (!BlocksArrayLooksValid(p, cache)) continue;
            names.blocks = reinterpret_cast<const uint8_t* const*>(p);
            names.currentBlock = reinterpret_cast<const uint32_t*>(p - 8);
            names.stride = stride;
            names.layout = layout;
            names.blockBytes = stride << 16;
            if (!NamesDecodeObjects()) {
                names = {};
                continue;
            }
            namesReady = true;
            SDK::FName::InitManually(reinterpret_cast<void*>(&AppendStringImpl));
            report.namesRva = p - game.base;
            Log::Info("Resolver", "Tabla de nombres localizada en " + Log::Hex(report.namesRva) + " (paso " +
                      std::to_string(stride) + ", cabecera " +
                      (layout == NamePool::HeaderLayout::ProbeHash ? "ProbeHash" : "Len15") + ")");
            return true;
        }
    }
    return false;
}

const uint8_t* FollowJumps(const uint8_t* code) {
    for (int i = 0; i < 3 && code; ++i) {
        if (code[0] == 0xE9) {
            int32_t rel;
            std::memcpy(&rel, code + 1, 4);
            code = code + 5 + rel;
        } else if (code[0] == 0xFF && code[1] == 0x25) {
            int32_t rel;
            std::memcpy(&rel, code + 2, 4);
            uintptr_t target;
            if (!Mem::Read(reinterpret_cast<uintptr_t>(code + 6 + rel), target)) return nullptr;
            code = reinterpret_cast<const uint8_t*>(target);
        } else {
            break;
        }
    }
    return code;
}

bool TestsFunctionFlag(const uint8_t* code, size_t length, uint32_t flag) {
    const auto* text = game.Find(".text");
    if (!text || !text->Contains(reinterpret_cast<uintptr_t>(code))) return false;
    length = std::min<size_t>(length, text->start + text->size - reinterpret_cast<uintptr_t>(code));
    const int offset = static_cast<int>(kFunctionFlagsOffset);
    // test dword ptr [reg+FunctionFlags], flag
    std::vector<int> dword = {0xF7, -1, offset & 0xFF, (offset >> 8) & 0xFF, 0, 0,
                              static_cast<int>(flag & 0xFF), static_cast<int>((flag >> 8) & 0xFF),
                              static_cast<int>((flag >> 16) & 0xFF), static_cast<int>(flag >> 24)};
    if (Mem::FindPattern(code, length, dword)) return true;
    // test byte ptr [reg+FunctionFlags+n], flag>>(8n)
    for (int byte = 0; byte < 4; ++byte) {
        const uint32_t part = (flag >> (8 * byte)) & 0xFF;
        if (!part) continue;
        const int at = offset + byte;
        std::vector<int> pattern = {0xF6, -1, at & 0xFF, (at >> 8) & 0xFF, 0, 0, static_cast<int>(part)};
        if (Mem::FindPattern(code, length, pattern)) return true;
    }
    return false;
}

bool LooksLikeProcessEvent(void* function) {
    const auto* code = FollowJumps(static_cast<const uint8_t*>(function));
    return code && TestsFunctionFlag(code, 0x400, 0x400) && TestsFunctionFlag(code, 0xF00, 0x400000);
}

void FindProcessEvent(Report& report) {
    auto* object = SDK::UObject::GObjects->GetByIndex(0);
    void** vtable = object ? *reinterpret_cast<void***>(object) : nullptr;
    report.processEventIndex = SDK::Offsets::ProcessEventIdx;
    report.processEvent = true;
    if (!vtable || !Mem::IsReadable(vtable, 0x200 * sizeof(void*))) {
        Log::Warn("Resolver", "No se pudo leer la vtable de UObject; se usa el índice conocido de ProcessEvent");
        return;
    }
    if (LooksLikeProcessEvent(vtable[SDK::Offsets::ProcessEventIdx])) {
        report.processEventVerified = true;
        Log::Info("Resolver", "ProcessEvent verificado en el índice " + Log::Hex(SDK::Offsets::ProcessEventIdx));
        return;
    }
    for (int i = 0; i < 0x150; ++i) {
        if (!game.Contains(reinterpret_cast<uintptr_t>(vtable[i]))) break;
        if (LooksLikeProcessEvent(vtable[i])) {
            SDK::Offsets::ProcessEventIdx = i;
            report.processEventIndex = i;
            report.processEventVerified = true;
            Log::Warn("Resolver", "ProcessEvent cambió de posición: nuevo índice " + Log::Hex(i));
            return;
        }
    }
    Log::Warn("Resolver", "No se pudo verificar ProcessEvent; se usa el índice conocido " +
                          Log::Hex(SDK::Offsets::ProcessEventIdx));
}

std::string ReadVersionAfter(const Mem::Section& rdata, const std::u16string& needle) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(rdata.start);
    const size_t needleBytes = needle.size() * 2;
    for (size_t i = 0; i + needleBytes + 64 <= rdata.size; i += 2) {
        if (std::memcmp(bytes + i, needle.data(), needleBytes) != 0) continue;
        std::string version;
        for (size_t j = i + needleBytes; j + 2 <= rdata.size && version.size() < 24; j += 2) {
            char16_t c;
            std::memcpy(&c, bytes + j, 2);
            if ((c >= u'0' && c <= u'9') || c == u'.') version.push_back(static_cast<char>(c));
            else break;
        }
        if (!version.empty() && version.front() != '.') return version;
    }
    return {};
}

void ReadGameVersion(Report& report) {
    const auto* rdata = game.Find(".rdata");
    if (!rdata) return;
    report.gameVersion = ReadVersionAfter(*rdata, u"+Augusta+release-");
    if (report.gameVersion.empty()) report.gameVersion = ReadVersionAfter(*rdata, u"release-");
    std::u16string augusta = u"+++Augusta+";
    const auto* bytes = reinterpret_cast<const uint8_t*>(rdata->start);
    for (size_t i = 64; i + augusta.size() * 2 <= rdata->size; i += 2) {
        if (std::memcmp(bytes + i, augusta.data(), augusta.size() * 2) != 0) continue;
        // "5.6.1-3100008+++Augusta+..." -> walk back to the start of the engine version.
        size_t start = i;
        while (start >= 2) {
            char16_t c;
            std::memcpy(&c, bytes + start - 2, 2);
            if ((c >= u'0' && c <= u'9') || c == u'.' || c == u'-') start -= 2;
            else break;
        }
        std::string engine;
        for (size_t j = start; j < i; j += 2) {
            char16_t c;
            std::memcpy(&c, bytes + j, 2);
            if (c == u'-') break;
            engine.push_back(static_cast<char>(c));
        }
        report.engineVersion = engine;
        break;
    }
}

} // namespace

bool Run(Report& report) {
    if (!game.base && !Mem::ReadModule(GetModuleHandleW(nullptr), game)) {
        Log::Error("Resolver", "No se pudieron leer las secciones del ejecutable del juego");
        return false;
    }
    if (report.gameVersion.empty()) {
        ReadGameVersion(report);
        Log::Info("Resolver", "Versión del juego: " + (report.gameVersion.empty() ? std::string("desconocida") : report.gameVersion) +
                              ", motor " + (report.engineVersion.empty() ? std::string("?") : report.engineVersion));
    }
    // The SDK must never fall back to the stale GWorld offset of an older build.
    SDK::Offsets::GWorld = 0;
    if (!report.objects) report.objects = FindObjects(report);
    if (report.objects && !report.names) report.names = FindNames(report);
    if (report.objects && report.names && !report.processEvent) FindProcessEvent(report);
    return report.objects && report.names && report.processEvent;
}

std::string NameToString(int32_t comparisonIndex, uint32_t number) {
    std::u16string text;
    if (!namesReady || !names.ToString(static_cast<uint32_t>(comparisonIndex), number, text)) return {};
    return NamePool::ToUtf8(text);
}

} // namespace Resolver
