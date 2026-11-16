#pragma once
// ZIP archives read and written through streams (wide paths on Windows, files of any size, zip64 when
// needed): Launchme's profile copies (profile_transfer.hpp) and Playnite's backups (playnite_import.hpp).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zip_archive {
// Entries are named with '/' and relative ("library/custom-games.json").
class Writer {
public:
    Writer();
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    bool open(const std::filesystem::path& file);
    // `store`: kept as it is (JPEG, PNG and other compressed files), else deflated.
    bool add(std::string_view name, std::string_view bytes, bool store = false);
    bool addFile(std::string_view name, const std::filesystem::path& source, bool store = false);
    // Writes the central directory; false: the archive is incomplete and should be removed.
    bool finish();
    uint64_t written() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

struct Entry { std::string name; uint64_t size = 0; bool folder = false; };

class Reader {
public:
    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    bool open(const std::filesystem::path& file);
    const std::vector<Entry>& entries() const;
    // Index of an entry by name; -1: none.
    int find(std::string_view name) const;
    // The entry's bytes, CRC-checked; nullopt when it is missing, larger than `limit` or damaged.
    std::optional<std::string> read(std::string_view name, uint64_t limit) const;
    // Streams the entry into `target` (created with its folders; written atomically). CRC-checked.
    bool extract(int index, const std::filesystem::path& target) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

// A relative entry name that cannot leave its folder: no root, drive, "..", ".", backslash or control characters.
bool safeName(std::string_view name);
// Compressed already (images, video, archives): stored rather than deflated.
bool storedType(const std::filesystem::path& file);
}
