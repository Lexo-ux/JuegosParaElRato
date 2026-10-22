// SPDX-License-Identifier: GPL-3.0-or-later
// Pure FNamePool decoding (no Windows/SDK dependencies so it can be unit tested).
//
// Unreal Engine 5 stores names in blocks of FNameEntry: a 16-bit header followed by
// the characters. In shipping builds the header is
//   bIsWide:1 | LowercaseProbeHash:5 | Len:10      (layout "ProbeHash")
// and in case-preserving builds
//   bIsWide:1 | Len:15                             (layout "Len15")
// An FName's ComparisonIndex encodes (Block << 16) | (ByteOffset / Stride).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace NamePool {

enum class HeaderLayout : uint8_t { ProbeHash, Len15 };

struct View {
    const uint8_t* const* blocks = nullptr;  // FNameEntryAllocator::Blocks
    const uint32_t* currentBlock = nullptr;  // FNameEntryAllocator::CurrentBlock (read live)
    uint32_t stride = 2;
    HeaderLayout layout = HeaderLayout::ProbeHash;
    uint32_t blockBytes = 2u << 16;

    bool Valid() const { return blocks && currentBlock; }

    static uint32_t Length(uint16_t header, HeaderLayout layout) {
        return layout == HeaderLayout::ProbeHash ? (header >> 6) : (header >> 1);
    }

    // Decodes one entry into UTF-16. Returns false for indices outside the pool.
    bool Decode(uint32_t comparisonIndex, std::u16string& out) const {
        out.clear();
        if (!Valid()) return false;
        const uint32_t block = comparisonIndex >> 16;
        const uint32_t offset = (comparisonIndex & 0xFFFF) * stride;
        if (block > *currentBlock || block >= 8192) return false;
        const uint8_t* base = blocks[block];
        if (!base || offset + 2 > blockBytes) return false;
        uint16_t header;
        std::memcpy(&header, base + offset, 2);
        const bool wide = (header & 1) != 0;
        const uint32_t length = Length(header, layout);
        if (length == 0 || length > 1024) return false;
        const uint32_t bytes = length * (wide ? 2u : 1u);
        if (offset + 2 + bytes > blockBytes) return false;
        const uint8_t* chars = base + offset + 2;
        out.resize(length);
        if (wide) {
            std::memcpy(out.data(), chars, bytes);
        } else {
            for (uint32_t i = 0; i < length; ++i) out[i] = static_cast<char16_t>(chars[i]);
        }
        return true;
    }

    // Full FName text including the "_N" suffix used for numbered names.
    bool ToString(uint32_t comparisonIndex, uint32_t number, std::u16string& out) const {
        if (!Decode(comparisonIndex, out)) return false;
        if (number > 0) {
            std::string digits = std::to_string(number - 1);
            out.push_back(u'_');
            for (char c : digits) out.push_back(static_cast<char16_t>(c));
        }
        return true;
    }
};

// Checks whether `entry` points to "None" followed by "ByteProperty", the first two names
// every Unreal Engine name pool registers. Returns the stride/layout that matched.
inline bool LooksLikeFirstBlock(const uint8_t* entry, size_t available, uint32_t& strideOut, HeaderLayout& layoutOut) {
    if (available < 32) return false;
    uint16_t header0;
    std::memcpy(&header0, entry, 2);
    if (header0 & 1) return false;  // "None" is never wide
    if (std::memcmp(entry + 2, "None", 4) != 0) return false;
    for (HeaderLayout layout : {HeaderLayout::ProbeHash, HeaderLayout::Len15}) {
        if (View::Length(header0, layout) != 4) continue;
        for (uint32_t stride : {2u, 4u}) {
            const size_t next = (6 + stride - 1) / stride * stride;
            if (next + 2 + 12 > available) continue;
            uint16_t header1;
            std::memcpy(&header1, entry + next, 2);
            if ((header1 & 1) || View::Length(header1, layout) != 12) continue;
            if (std::memcmp(entry + next + 2, "ByteProperty", 12) != 0) continue;
            strideOut = stride;
            layoutOut = layout;
            return true;
        }
    }
    return false;
}

inline std::string ToUtf8(const std::u16string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return out;
}

} // namespace NamePool
