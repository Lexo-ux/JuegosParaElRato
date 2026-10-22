// SPDX-License-Identifier: GPL-3.0-or-later
#include "Log.hpp"

#include <windows.h>
#include <cstdio>
#include <deque>
#include <mutex>

namespace Log {
namespace {
std::mutex mutex;
std::deque<Line> tail;
uint64_t revision = 0;
HANDLE file = INVALID_HANDLE_VALUE;
std::wstring path;
constexpr size_t kTailMax = 600;

const char* LevelTag(Level level) {
    switch (level) {
    case Level::Warn: return "AVISO";
    case Level::Error: return "ERROR";
    case Level::Debug: return "DEBUG";
    default: return "INFO ";
    }
}
} // namespace

void Init(const std::wstring& directory) {
    std::lock_guard lock(mutex);
    path = directory + L"\\registro.txt";
    // Keep the previous session's log for bug reports.
    MoveFileExW(path.c_str(), (directory + L"\\registro-anterior.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        WriteFile(file, bom, sizeof(bom), &written, nullptr);
    }
}

void Write(Level level, const char* tag, const std::string& message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%02u:%02u:%02u %s [%s] ", now.wHour, now.wMinute, now.wSecond,
                  LevelTag(level), tag ? tag : "");
    std::string line = prefix + message;

    std::lock_guard lock(mutex);
    if (file != INVALID_HANDLE_VALUE) {
        std::string out = line + "\r\n";
        DWORD written = 0;
        WriteFile(file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    }
    tail.push_back({level, std::move(line)});
    while (tail.size() > kTailMax) tail.pop_front();
    ++revision;
}

std::vector<Line> Tail(size_t maxLines, uint64_t* rev) {
    std::lock_guard lock(mutex);
    if (rev) *rev = revision;
    size_t start = tail.size() > maxLines ? tail.size() - maxLines : 0;
    return {tail.begin() + static_cast<std::ptrdiff_t>(start), tail.end()};
}

uint64_t Revision() {
    std::lock_guard lock(mutex);
    return revision;
}

std::wstring FilePath() {
    std::lock_guard lock(mutex);
    return path;
}

std::string Hex(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace Log
