// SPDX-License-Identifier: GPL-3.0-or-later
// PE section parsing and guarded memory reads used by the runtime resolver.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Mem {

struct Section {
    std::string name;
    uintptr_t start = 0;
    size_t size = 0;
    uint32_t characteristics = 0;
    bool Contains(uintptr_t address, size_t length = 1) const {
        return address >= start && address + length <= start + size;
    }
};

struct Module {
    uintptr_t base = 0;
    size_t size = 0;
    std::vector<Section> sections;

    const Section* Find(const char* name) const;
    bool Contains(uintptr_t address) const { return address >= base && address < base + size; }
};

bool ReadModule(HMODULE module, Module& out);

// True when [address, address + length) is committed and readable.
bool IsReadable(const void* address, size_t length);

// Region cache for scans that dereference many candidate pointers.
class RegionCache {
public:
    bool Readable(uintptr_t address, size_t length);
private:
    uintptr_t start_ = 0, end_ = 0;
    bool readable_ = false;
};

template <class T>
inline bool Read(uintptr_t address, T& out) {
    if (!IsReadable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// Byte pattern with wildcards: -1 matches any byte.
bool MatchAt(const uint8_t* data, const std::vector<int>& pattern);
const uint8_t* FindPattern(const uint8_t* start, size_t length, const std::vector<int>& pattern);

} // namespace Mem
