// SPDX-License-Identifier: GPL-3.0-or-later
#include "Memory.hpp"

namespace Mem {
namespace {
bool ProtectReadable(DWORD protect) {
    if (protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
} // namespace

const Section* Module::Find(const char* name) const {
    for (const auto& section : sections)
        if (section.name == name) return &section;
    return nullptr;
}

bool ReadModule(HMODULE module, Module& out) {
    out = {};
    if (!module) return false;
    auto base = reinterpret_cast<uintptr_t>(module);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.base = base;
    out.size = nt->OptionalHeader.SizeOfImage;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        Section s;
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        s.name = name;
        s.start = base + section->VirtualAddress;
        s.size = section->Misc.VirtualSize;
        s.characteristics = section->Characteristics;
        out.sections.push_back(std::move(s));
    }
    return true;
}

bool IsReadable(const void* address, size_t length) {
    auto current = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = current + length;
    if (current == 0 || end < current) return false;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info))) return false;
        if (info.State != MEM_COMMIT || !ProtectReadable(info.Protect)) return false;
        current = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

bool RegionCache::Readable(uintptr_t address, size_t length) {
    if (address == 0) return false;
    if (address >= start_ && address + length <= end_) return readable_;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) {
        start_ = end_ = 0;
        return false;
    }
    start_ = reinterpret_cast<uintptr_t>(info.BaseAddress);
    end_ = start_ + info.RegionSize;
    readable_ = info.State == MEM_COMMIT && ProtectReadable(info.Protect);
    if (address + length > end_) return readable_ && IsReadable(reinterpret_cast<const void*>(address), length);
    return readable_;
}

bool MatchAt(const uint8_t* data, const std::vector<int>& pattern) {
    for (size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] >= 0 && data[i] != static_cast<uint8_t>(pattern[i])) return false;
    return true;
}

const uint8_t* FindPattern(const uint8_t* start, size_t length, const std::vector<int>& pattern) {
    if (pattern.empty() || length < pattern.size()) return nullptr;
    for (size_t i = 0; i + pattern.size() <= length; ++i)
        if (MatchAt(start + i, pattern)) return start + i;
    return nullptr;
}

} // namespace Mem
