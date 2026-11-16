#pragma once
// Read-only LiteDB 4 data files (file format 7): Playnite 10 keeps its library in them (library/games.db,
// platforms.db, …). A port of Playnite's own reader (source/Playnite/Database/Collections/LiteDBFileReaderV7.cs,
// MIT): pages of 4 KiB, a collection's first index lists every document, documents are BSON.
// BSON becomes json::Value: GUIDs as text ("cb91dfc9-b977-43bf-8e70-55f46e410fab", .NET byte order), dates
// as Unix milliseconds, 64-bit integers and decimals as numbers, binary data and object IDs as hex text.
#include "../net/json.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace litedb_v7 {
inline constexpr size_t page_size = 4096;
inline constexpr uint64_t max_file = 1024ull * 1024 * 1024;

// The file's bytes: nullopt when it is no LiteDB 4 file, needs a password or cannot be read (Playnite
// running holds it exclusively).
std::optional<std::string> load(const std::filesystem::path& file, std::wstring& error);
std::vector<std::string> collections(std::string_view file);
// Every document of `collection` (empty: the file's first collection); damaged pages and documents are skipped.
std::vector<json::Value> documents(std::string_view file, std::string_view collection = {});
// One BSON document; nullopt when it is malformed.
std::optional<json::Value> bson(std::string_view bytes);
// A .NET GUID's 16 bytes as text (Data1-3 little-endian).
std::string guidText(const uint8_t* bytes);
}
