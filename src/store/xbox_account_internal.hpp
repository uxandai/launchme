#pragma once
// Private to src/store/xbox_account*.cpp: reading the helper's JSON files.
#include "../net/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace xbox_account::internal {
inline std::string field(const json::Value& value, const char* key) { const auto* p = value.get(key); return p ? p->stringOr() : std::string{}; }
inline std::string field(const json::Value* value, const char* key) { return value ? field(*value, key) : std::string{}; }
inline bool httpsArt(const std::string& url) {
    return url.starts_with("https://") && url.size() < 2048 && url.find_first_of(" \"\r\n\t") == std::string::npos;
}
inline std::optional<uint64_t> unsignedNumber(const json::Value* value, double limit) {
    if (!value || value->type != json::Value::Type::number || !std::isfinite(value->numeric) || value->numeric < 0 || value->numeric > limit) return {};
    return static_cast<uint64_t>(value->numeric);
}
// A decimal Xbox title id ("1234567890"), as the catalog and the title history write it.
inline uint32_t titleId(const std::string& text) {
    if (text.empty() || text.size() > 10 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return 0;
    const auto value = std::stoull(text);
    return value > 0 && value <= UINT32_MAX ? static_cast<uint32_t>(value) : 0;
}
}
