// SPDX-License-Identifier: GPL-3.0-or-later
// Thread-safe log: file in %LOCALAPPDATA%\Grounded2PanelES plus an in-memory tail for the panel.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Log {

enum class Level : uint8_t { Info, Warn, Error, Debug };

struct Line {
    Level level;
    std::string text;   // UTF-8, already prefixed with time and tag
};

void Init(const std::wstring& directory);
void Write(Level level, const char* tag, const std::string& message);
std::vector<Line> Tail(size_t maxLines, uint64_t* revision = nullptr);
uint64_t Revision();
std::wstring FilePath();

inline void Info(const char* tag, const std::string& m)  { Write(Level::Info, tag, m); }
inline void Warn(const char* tag, const std::string& m)  { Write(Level::Warn, tag, m); }
inline void Error(const char* tag, const std::string& m) { Write(Level::Error, tag, m); }
inline void Debug(const char* tag, const std::string& m) { Write(Level::Debug, tag, m); }

std::string Hex(uint64_t value);

} // namespace Log
